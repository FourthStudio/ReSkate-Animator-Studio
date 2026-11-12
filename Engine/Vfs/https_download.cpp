#include "https_download.h"
#include "https_trust.h"
#include "Engine/Core/Platform/clock.h"
#include <Windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace dingosdk::https {
namespace {

struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    const std::filesystem::path& path;
    bool complete{};
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
            if (!complete) DeleteFileW(path.c_str());
        }
    }
};
struct Internet {
    HINTERNET value{};
    ~Internet() { if (value) WinHttpCloseHandle(value); }
};
struct AsyncRequest {
    HINTERNET value{};
    std::mutex mutex;
    std::condition_variable changed;
    DWORD expected{}, error{}, read{};
    bool completed{}, closed{};

    ~AsyncRequest() { close(); }

    void close() {
        if (!value) return;
        // Async closure interrupts pending I/O. Keep the context and read buffer
        // alive until WinHTTP's final callback, including on cancellation.
        WinHttpCloseHandle(value);
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return closed; });
        value = nullptr;
    }

    static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, void* info, DWORD size) {
        if (!context) return;
        auto& request = *reinterpret_cast<AsyncRequest*>(context);
        std::lock_guard<std::mutex> lock(request.mutex);
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
            request.closed = true;
        } else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
            request.error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
            request.completed = true;
        } else if (status == request.expected) {
            if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) request.read = size;
            request.completed = true;
        }
        request.changed.notify_all();
    }

    void prepare(DWORD status) {
        std::lock_guard<std::mutex> lock(mutex);
        expected = status;
        completed = false;
        error = read = 0;
    }

    template<class Stop>
    DWORD wait(const Stop& stop) {
        std::unique_lock<std::mutex> lock(mutex);
        for (;;) {
            if (const auto stopped = stop()) return stopped;
            if (completed) return error;
            changed.wait_for(lock, std::chrono::milliseconds(20));
        }
    }
};
}

namespace {
// One GET. `builtin_roots`: the second try after Windows refused the server's certificate. Windows'
// verdict is set aside for the handshake, redirects are not followed, and the answer is used only
// when the certificate passes chains_to_builtin_root (https_trust.h) for this host; nothing is
// read from it before that.
Download get_once(std::wstring_view url, const std::filesystem::path& destination, std::uint64_t max_bytes,
    std::uint64_t timeout_deadline, const wchar_t* agent, Watch* watch, bool builtin_roots) {
    Download result;
    const auto stop = [&]() -> DWORD {
        if (watch && watch->cancel.load()) return ERROR_CANCELLED;
        const auto external = watch ? watch->deadline_us.load() : 0;
        const auto deadline = external && external < timeout_deadline ? external : timeout_deadline;
        return steady_us() >= deadline ? ERROR_TIMEOUT : ERROR_SUCCESS;
    };
    const auto fail = [&](DWORD error) {
        const auto stopped = stop();
        result.error = stopped ? stopped : error;
        return result;
    };
    if (const auto error = stop()) return fail(error);
    if (url.empty() || url.size() >= 4096 || url.find_first_of(L"\r\n\t #") != std::wstring_view::npos)
        return fail(ERROR_INVALID_PARAMETER);
    const std::wstring address(url);
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(address.c_str(), static_cast<DWORD>(address.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength)
        return fail(ERROR_INVALID_PARAMETER);
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring resource = parts.dwUrlPathLength ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/";
    if (parts.dwExtraInfoLength) resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Internet session{WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, WINHTTP_FLAG_ASYNC)};
    if (!session.value) return fail(GetLastError());
    const int patience = watch ? static_cast<int>(watch->patience_ms.load()) : 0;
    if (!WinHttpSetTimeouts(session.value, patience ? patience : 5000, patience ? patience : 5000, patience ? patience : 10000,
                            patience ? patience : 10000)) return fail(GetLastError());
    Internet connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
    if (!connection.value) return fail(GetLastError());
    Internet request{WinHttpOpenRequest(connection.value, L"GET", resource.c_str(), nullptr, nullptr,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value) return fail(GetLastError());
    // Declared before AsyncRequest so pending reads cannot outlive their buffer.
    std::array<char, 65536> buffer{};
    AsyncRequest async;
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon))) return fail(GetLastError());
    if (builtin_roots) {
        DWORD ignore = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                       SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        DWORD no_redirects = WINHTTP_DISABLE_REDIRECTS;
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_SECURITY_FLAGS, &ignore, sizeof(ignore)) ||
            !WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &no_redirects, sizeof(no_redirects)))
            return fail(GetLastError());
    }
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&async);
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
        return fail(GetLastError());
    if (WinHttpSetStatusCallback(request.value, AsyncRequest::callback,
            WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
            WINHTTP_CALLBACK_FLAG_READ_COMPLETE | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
            WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
        return fail(GetLastError());
    async.value = std::exchange(request.value, nullptr);
    const auto finish = [&](BOOL accepted) -> DWORD {
        if (!accepted) {
            const auto error = GetLastError();
            if (error != ERROR_IO_PENDING) return error;
        }
        return async.wait(stop);
    };
    if (const auto error = stop()) return fail(error);
    async.prepare(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    if (const auto error = finish(WinHttpSendRequest(async.value, nullptr, 0, nullptr, 0, 0, context)))
        return fail(error);
    async.prepare(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    if (const auto error = stop()) return fail(error);
    if (const auto error = finish(WinHttpReceiveResponse(async.value, nullptr))) return fail(error);
    if (builtin_roots) {
        PCCERT_CONTEXT certificate{};
        DWORD bytes = sizeof(certificate);
        const bool trusted = WinHttpQueryOption(async.value, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &certificate, &bytes) &&
                             chains_to_builtin_root(certificate, host);
        if (certificate) CertFreeCertificateContext(certificate);
        if (!trusted) return fail(ERROR_WINHTTP_SECURE_FAILURE);
    }
    DWORD size = sizeof(result.http_status);
    if (!WinHttpQueryHeaders(async.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            nullptr, &result.http_status, &size, nullptr)) return fail(GetLastError());
    if (result.http_status != 200) return fail(ERROR_BAD_NET_RESP);
    DWORD length{}; size = sizeof(length);
    if (WinHttpQueryHeaders(async.value, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                           nullptr, &length, &size, nullptr) && length > max_bytes) return fail(ERROR_FILE_TOO_LARGE);
    if (watch) watch->total.store(length), watch->received.store(0);
    if (const auto error = stop()) return fail(error);
    Handle output{CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr),
                  destination};
    if (output.value == INVALID_HANDLE_VALUE) return fail(GetLastError());
    std::uint64_t total{};
    for (;;) {
        if (const auto error = stop()) return fail(error);
        async.prepare(WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
        if (const auto error = finish(WinHttpReadData(async.value, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr)))
            return fail(error);
        const auto read = async.read;
        DWORD written{};
        if (!read) break;
        if (total + read > max_bytes) return fail(ERROR_FILE_TOO_LARGE);
        if (!WriteFile(output.value, buffer.data(), read, &written, nullptr) || written != read) return fail(ERROR_WRITE_FAULT);
        total += read;
        if (watch) watch->received.store(total);
    }
    if (!total) return fail(ERROR_BAD_LENGTH);
    if (!FlushFileBuffers(output.value)) return fail(GetLastError());
    async.close();
    if (const auto error = stop()) return fail(error);
    output.complete = true;
    result.ok = true;
    return result;
}
}

Download get(std::wstring_view url, const std::filesystem::path& destination,
    std::uint64_t max_bytes, std::uint32_t timeout_seconds, const wchar_t* agent, Watch* watch) {
    const auto timeout_deadline = steady_us() + std::uint64_t{timeout_seconds} * 1000000;
    const auto first = get_once(url, destination, max_bytes, timeout_deadline, agent, watch, false);
    // 12175: Windows refused the certificate. Tried again against ReSkate's own roots, within
    // the same time limit (a refused first try writes nothing to `destination`).
    if (first.ok || first.error != ERROR_WINHTTP_SECURE_FAILURE) return first;
    return get_once(url, destination, max_bytes, timeout_deadline, agent, watch, true);
}

// ---- fetch: large files, resumed and tried again -------------------------------------------------
namespace {
struct Target {
    std::wstring host, resource;
    INTERNET_PORT port{};
};
std::optional<Target> crack(std::wstring_view url) {
    if (url.empty() || url.size() >= 4096 || url.find_first_of(L"\r\n\t #") != std::wstring_view::npos) return std::nullopt;
    const std::wstring address(url);
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(address.c_str(), static_cast<DWORD>(address.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength)
        return std::nullopt;
    Target out;
    out.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    out.resource = parts.dwUrlPathLength ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/";
    if (parts.dwExtraInfoLength) out.resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    out.port = parts.nPort;
    return out;
}
// A response header as text ("" when it is not there): numbers as text, as a file can be larger than 32 bits.
std::wstring header_text(HINTERNET request, DWORD query) {
    std::array<wchar_t, 512> text{};
    DWORD size = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, text.data(), &size, WINHTTP_NO_HEADER_INDEX)) return {};
    return std::wstring(text.data(), std::min<std::size_t>(size / sizeof(wchar_t), text.size()));
}
std::optional<std::uint64_t> whole_number(std::wstring_view text) {
    while (!text.empty() && text.front() == L' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == L' ') text.remove_suffix(1);
    if (text.empty() || text.size() > 19) return std::nullopt;
    std::uint64_t value = 0;
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') return std::nullopt;
        value = value * 10 + static_cast<std::uint64_t>(c - L'0');
    }
    return value;
}
// "bytes 100-199/1000": where the part starts and the whole file's size (0 when the server writes "*").
bool content_range(std::wstring_view text, std::uint64_t& first, std::uint64_t& total) {
    if (!text.starts_with(L"bytes ")) return false;
    text.remove_prefix(6);
    const auto dash = text.find(L'-'), slash = text.find(L'/');
    if (dash == std::wstring_view::npos || slash == std::wstring_view::npos || dash > slash) return false;
    const auto start = whole_number(text.substr(0, dash));
    if (!start) return false;
    first = *start;
    const auto all = text.substr(slash + 1);
    if (all == L"*") total = 0;
    else if (const auto size = whole_number(all)) total = *size;
    else return false;
    return true;
}
std::uint64_t file_size_of(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}
struct Attempt {
    Download result;
    std::uint64_t total{}; // the whole file's size as the server said it, 0 not said
    std::wstring etag;     // a strong ETag, to go on against
};
// One request into `part`, from `offset` (its bytes before that kept). Stops on `stop`, a stall included.
template<class Stop>
Attempt fetch_once(const Target& target, const std::filesystem::path& part, std::uint64_t offset, std::uint64_t known_total,
                   const std::wstring& etag, const Fetch& options, const wchar_t* agent, Watch& watch, const Stop& stop) {
    Attempt out;
    auto& result = out.result;
    const auto fail = [&](DWORD error) {
        const auto stopped = stop();
        result.error = stopped ? stopped : error;
        return out;
    };
    Internet session{WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, WINHTTP_FLAG_ASYNC)};
    if (!session.value) return fail(GetLastError());
    const int connect = static_cast<int>(std::clamp<std::uint32_t>(options.connect_ms, 1000, 600000));
    const int read = static_cast<int>(std::clamp<std::uint32_t>(options.stall_ms, 1000, 3600000));
    if (!WinHttpSetTimeouts(session.value, connect, connect, read, read)) return fail(GetLastError());
    Internet connection{WinHttpConnect(session.value, target.host.c_str(), target.port, 0)};
    if (!connection.value) return fail(GetLastError());
    Internet request{WinHttpOpenRequest(connection.value, L"GET", target.resource.c_str(), nullptr, nullptr,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value) return fail(GetLastError());
    // Declared before AsyncRequest so pending reads cannot outlive their buffer.
    std::array<char, 65536> buffer{};
    AsyncRequest async;
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon))) return fail(GetLastError());
    // Going on from `offset`: only the rest, and only of the same file (If-Range: a changed file comes whole, from 0).
    if (offset) {
        std::wstring headers = L"Range: bytes=" + std::to_wstring(offset) + L"-\r\n";
        if (!etag.empty()) headers += L"If-Range: " + etag + L"\r\n";
        if (!WinHttpAddRequestHeaders(request.value, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
            return fail(GetLastError());
    }
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&async);
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) return fail(GetLastError());
    if (WinHttpSetStatusCallback(request.value, AsyncRequest::callback,
            WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
            WINHTTP_CALLBACK_FLAG_READ_COMPLETE | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
            WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
        return fail(GetLastError());
    async.value = std::exchange(request.value, nullptr);
    const auto finish = [&](BOOL accepted) -> DWORD {
        if (!accepted) {
            const auto error = GetLastError();
            if (error != ERROR_IO_PENDING) return error;
        }
        return async.wait(stop);
    };
    if (const auto error = stop()) return fail(error);
    async.prepare(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    if (const auto error = finish(WinHttpSendRequest(async.value, nullptr, 0, nullptr, 0, 0, context))) return fail(error);
    async.prepare(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    if (const auto error = stop()) return fail(error);
    if (const auto error = finish(WinHttpReceiveResponse(async.value, nullptr))) return fail(error);
    DWORD size = sizeof(result.http_status);
    if (!WinHttpQueryHeaders(async.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             nullptr, &result.http_status, &size, nullptr)) return fail(GetLastError());
    if (const auto tag = header_text(async.value, WINHTTP_QUERY_ETAG); tag.size() > 2 && !tag.starts_with(L"W/") && tag.size() < 200)
        out.etag = tag;
    const auto length = whole_number(header_text(async.value, WINHTTP_QUERY_CONTENT_LENGTH));
    std::uint64_t start = 0;
    if (result.http_status == 206 && offset) {
        std::uint64_t total = 0;
        if (!content_range(header_text(async.value, WINHTTP_QUERY_CONTENT_RANGE), start, total) || start != offset) {
            DeleteFileW(part.c_str()); // not the part asked for: the next request starts from the beginning
            return fail(ERROR_WINHTTP_INVALID_SERVER_RESPONSE);
        }
        out.total = total ? total : length ? offset + *length : 0;
    } else if (result.http_status == 416 && offset && known_total && offset == known_total) {
        // All of it was there already (the last request was cut at its very end).
        out.total = known_total;
        result.ok = true;
        return out;
    } else if (result.http_status == 200) {
        out.total = length.value_or(0); // the whole file, from the start (a server that will not send part of it)
    } else {
        // 416 on a part that is not the whole file: what was kept is not of this file, and the next request starts again.
        if (result.http_status == 416) {
            DeleteFileW(part.c_str());
            return fail(ERROR_WINHTTP_INVALID_SERVER_RESPONSE);
        }
        return fail(ERROR_BAD_NET_RESP);
    }
    if (out.total > options.max_bytes) return fail(ERROR_FILE_TOO_LARGE);
    if (options.expected_bytes && out.total && out.total != options.expected_bytes) return fail(ERROR_BAD_LENGTH);
    Handle output{CreateFileW(part.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr), part};
    output.complete = true; // kept on failure: the bytes that came are where the next request starts
    if (output.value == INVALID_HANDLE_VALUE) return fail(GetLastError());
    LARGE_INTEGER at{};
    at.QuadPart = static_cast<LONGLONG>(start);
    if (!SetFilePointerEx(output.value, at, nullptr, FILE_BEGIN) || !SetEndOfFile(output.value)) return fail(GetLastError());
    std::uint64_t got = start;
    watch.total.store(out.total);
    watch.received.store(got);
    for (;;) {
        if (const auto error = stop()) return fail(error);
        async.prepare(WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
        if (const auto error = finish(WinHttpReadData(async.value, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr)))
            return fail(error);
        const auto bytes = async.read;
        if (!bytes) break;
        if (got + bytes > options.max_bytes) return fail(ERROR_FILE_TOO_LARGE);
        DWORD written{};
        if (!WriteFile(output.value, buffer.data(), bytes, &written, nullptr) || written != bytes) {
            const auto error = GetLastError();
            return fail(error == ERROR_DISK_FULL || error == ERROR_HANDLE_DISK_FULL ? error : ERROR_WRITE_FAULT);
        }
        got += bytes;
        watch.received.store(got);
    }
    if (!FlushFileBuffers(output.value)) return fail(GetLastError());
    // The server's own count: an answer cut short (a dropped connection that WinHTTP read as its end) goes on from here.
    if (out.total && got != out.total) return fail(got < out.total ? ERROR_WINHTTP_CONNECTION_ERROR : ERROR_BAD_LENGTH);
    if (!got) return fail(ERROR_BAD_LENGTH);
    if (!out.total) out.total = got;
    result.ok = true;
    return out;
}
}

bool worth_trying_again(const Download& got) noexcept {
    if (got.http_status == 408 || got.http_status == 429 || (got.http_status >= 500 && got.http_status < 600)) return true;
    switch (got.error) {
    case ERROR_WINHTTP_TIMEOUT:
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
    case ERROR_WINHTTP_RESEND_REQUEST:
    case ERROR_WINHTTP_SECURE_FAILURE: return true;
    default: return false;
    }
}

Download fetch(std::wstring_view url, const std::filesystem::path& destination, const Fetch& options, const wchar_t* agent,
               Watch* watch) {
    Watch own;
    if (!watch) watch = &own;
    Download result;
    auto part = destination;
    part += L".part";
    const auto started = steady_us();
    const auto total_deadline = options.total_seconds ? started + std::uint64_t{options.total_seconds} * 1000000 : 0;
    const auto stall_us = std::uint64_t{std::max<std::uint32_t>(options.stall_ms, 1000)} * 1000;
    // Stopped: asked to, or out of time (its own limit or the caller's).
    const auto stopped = [&]() -> DWORD {
        if (watch->cancel.load()) return ERROR_CANCELLED;
        const auto now = steady_us();
        const auto external = watch->deadline_us.load();
        if ((external && now >= external) || (total_deadline && now >= total_deadline)) return ERROR_TIMEOUT;
        return ERROR_SUCCESS;
    };
    // The ETag the kept part was downloaded with, beside it: a part kept from an earlier call goes on only against it (a
    // part of one file and the rest of a changed one would be neither).
    auto tag_file = part;
    tag_file += L".etag";
    const auto give_up = [&](DWORD error) {
        result.ok = false;
        result.error = error;
        if (!options.keep_partial) {
            DeleteFileW(part.c_str());
            DeleteFileW(tag_file.c_str());
        }
        watch->retry_at_us.store(0);
        return result;
    };
    const auto target = crack(url);
    if (!target) return give_up(ERROR_INVALID_PARAMETER);
    if (const auto error = stopped()) return give_up(error);
    std::wstring etag;
    if (options.keep_partial && file_size_of(part)) {
        std::ifstream in(tag_file, std::ios::binary);
        const std::string kept((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (kept.size() > 2 && kept.size() < 200) etag.assign(kept.begin(), kept.end());
    }
    if (etag.empty()) {
        DeleteFileW(part.c_str());
        DeleteFileW(tag_file.c_str());
    }
    std::uint64_t known_total = 0;
    int barren = 0;             // requests in a row that brought nothing
    bool checked_again = false; // a pieced-together file that failed its check was downloaded once more from the start
    for (;;) {
        const auto offset = file_size_of(part);
        if (offset) result.resumed = true;
        watch->attempt.store(++result.attempts);
        watch->retry_at_us.store(0);
        // Stalled: less than 16 KB in a stall's time (ERROR_WINHTTP_TIMEOUT, tried again from where it got to). Not a limit
        // on the whole download: a slow line that keeps moving finishes.
        std::uint64_t mark = offset, moved_us = steady_us();
        watch->received.store(offset);
        const auto stop = [&]() -> DWORD {
            if (const auto error = stopped()) return error;
            const auto now = steady_us();
            if (const auto received = watch->received.load(); received >= mark + 16384 || received < mark) {
                mark = received;
                moved_us = now;
            }
            return now - moved_us > stall_us ? ERROR_WINHTTP_TIMEOUT : ERROR_SUCCESS;
        };
        const auto attempt = fetch_once(*target, part, offset, known_total, etag, options, agent, *watch, stop);
        result.http_status = attempt.result.http_status;
        if (attempt.total) known_total = attempt.total;
        if (!attempt.etag.empty() && attempt.etag != etag) {
            etag = attempt.etag;
            if (options.keep_partial) {
                std::ofstream out(tag_file, std::ios::binary | std::ios::trunc);
                for (const wchar_t c : etag) out.put(static_cast<char>(c & 0x7f));
            }
        }
        if (attempt.result.ok) {
            const auto size = file_size_of(part);
            if ((options.expected_bytes && size != options.expected_bytes) || (known_total && size != known_total)) {
                DeleteFileW(part.c_str());
                return give_up(ERROR_BAD_LENGTH);
            }
            if (options.verify && !options.verify(part)) {
                DeleteFileW(part.c_str());
                if (result.resumed && !checked_again) {
                    // Pieced together from more than one request: once more, whole, before calling it damaged.
                    checked_again = true;
                    result.resumed = false;
                    etag.clear();
                    DeleteFileW(tag_file.c_str());
                    known_total = 0;
                    continue;
                }
                return give_up(ERROR_INVALID_DATA);
            }
            if (!MoveFileExW(part.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return give_up(GetLastError());
            DeleteFileW(tag_file.c_str());
            watch->received.store(size);
            watch->total.store(size);
            watch->retry_at_us.store(0);
            result.ok = true;
            result.error = 0;
            return result;
        }
        result.error = attempt.result.error;
        if (const auto error = stopped()) return give_up(error);
        if (!worth_trying_again(attempt.result)) return give_up(attempt.result.error);
        // Anything that came (or a part started over) counts as getting somewhere; only requests that bring nothing run out.
        barren = file_size_of(part) != offset ? 0 : barren + 1;
        if (barren >= std::max(options.tries, 1)) return give_up(attempt.result.error);
        // A wait that grows while nothing comes (1, 2, 4 ... to 30 s), short again after a request that brought bytes.
        const auto wait_ms = std::min<std::uint64_t>(30000, std::uint64_t{options.backoff_ms} << std::min(barren, 5));
        watch->retry_at_us.store(steady_us() + wait_ms * 1000);
        while (steady_us() < watch->retry_at_us.load()) {
            if (const auto error = stopped()) return give_up(error);
            Sleep(20);
        }
    }
}

std::optional<std::uint64_t> content_length(std::wstring_view url, std::uint32_t timeout_seconds,
    const wchar_t* agent, Watch* watch) {
    const auto started = steady_us();
    const auto timeout_deadline = started + std::uint64_t{timeout_seconds} * 1000000;
    const auto stop = [&]() -> DWORD {
        if (watch && watch->cancel.load()) return ERROR_CANCELLED;
        const auto external = watch ? watch->deadline_us.load() : 0;
        const auto deadline = external && external < timeout_deadline ? external : timeout_deadline;
        return steady_us() >= deadline ? ERROR_TIMEOUT : ERROR_SUCCESS;
    };
    if (stop() || url.empty() || url.size() >= 4096 || url.find_first_of(L"\r\n\t #") != std::wstring_view::npos)
        return std::nullopt;
    const std::wstring address(url);
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(address.c_str(), static_cast<DWORD>(address.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength)
        return std::nullopt;
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring resource = parts.dwUrlPathLength ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/";
    if (parts.dwExtraInfoLength) resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Internet session{WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, WINHTTP_FLAG_ASYNC)};
    if (!session.value || !WinHttpSetTimeouts(session.value, 5000, 5000, 10000, 10000)) return std::nullopt;
    Internet connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
    if (!connection.value) return std::nullopt;
    Internet request{WinHttpOpenRequest(connection.value, L"HEAD", resource.c_str(), nullptr, nullptr,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value) return std::nullopt;
    AsyncRequest async;
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon)))
        return std::nullopt;
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&async);
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) return std::nullopt;
    if (WinHttpSetStatusCallback(request.value, AsyncRequest::callback,
            WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
            WINHTTP_CALLBACK_FLAG_REQUEST_ERROR | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
        return std::nullopt;
    async.value = std::exchange(request.value, nullptr);
    const auto finish = [&](BOOL accepted) -> DWORD {
        if (!accepted && GetLastError() != ERROR_IO_PENDING) return GetLastError();
        return async.wait(stop);
    };
    if (stop()) return std::nullopt;
    async.prepare(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    if (finish(WinHttpSendRequest(async.value, nullptr, 0, nullptr, 0, 0, context))) return std::nullopt;
    async.prepare(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    if (stop() || finish(WinHttpReceiveResponse(async.value, nullptr))) return std::nullopt;
    DWORD status{}, size = sizeof(status);
    if (!WinHttpQueryHeaders(async.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            nullptr, &status, &size, nullptr) || status != 200)
        return std::nullopt;
    // As text: a number query is 32 bits, and a map can be larger than that.
    wchar_t digits[32]{};
    size = sizeof(digits);
    if (!WinHttpQueryHeaders(async.value, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            digits, &size, WINHTTP_NO_HEADER_INDEX) || !digits[0])
        return std::nullopt;
    wchar_t* end{};
    errno = 0;
    const auto length = std::wcstoull(digits, &end, 10);
    if (end == digits || *end || errno == ERANGE || !length) return std::nullopt;
    return length;
}

std::optional<std::string> get_text(std::wstring_view url, std::uint64_t max_bytes,
    std::uint32_t timeout_seconds, const wchar_t* agent, Download* result, Watch* watch) {
    static std::atomic<unsigned> serial{};
    wchar_t folder[MAX_PATH + 1]{};
    const auto length = GetTempPathW(MAX_PATH, folder);
    const auto path = std::filesystem::path(length ? std::wstring(folder, length) : L".") /
        (L"reskate-get-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial) + L".tmp");
    std::error_code error;
    std::filesystem::remove(path, error); // discard a stale file left by an earlier process
    const auto download = get(url, path, max_bytes, timeout_seconds, agent, watch);
    if (result) *result = download;
    std::optional<std::string> text;
    if (download.ok) {
        std::ifstream in(path, std::ios::binary);
        text.emplace(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (download.ok) std::filesystem::remove(path, error);
    return text;
}
}
