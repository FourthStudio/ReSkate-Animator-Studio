// https::fetch against a scripted server (WinHTTP replaced, as in https_download_timeout_tests.cpp): a download that
// drops part way goes on with a Range request from where it got to, a server that ignores the Range is started over, a
// busy server is tried again, a stalled request is dropped and gone on from, and the destination only ever holds the
// whole, checked file.
#include <Windows.h>
#include <winhttp.h>
#include "../https_download.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fake_server {
struct Handle {
    DWORD_PTR context{};
    WINHTTP_STATUS_CALLBACK callback{};
    DWORD notifications{};
    bool closed{};
};
// What one request does: answer with `status` (0: the file, or its part), drop the connection after `cut` bytes of the
// body, or stop sending (stall) after `cut` bytes.
struct Plan {
    unsigned status{};
    std::size_t cut{std::string::npos};
    bool stall{};
};
struct State {
    std::string body;
    std::string etag{"\"v1\""};
    bool ranges{true};          // honours Range
    bool length{true};          // says Content-Length
    std::vector<Plan> plans;    // by request, in order; past the end: the file, whole
    std::vector<std::wstring> asked; // each request's added headers
    unsigned requests{};
    // the request going on
    Plan plan;
    unsigned status{};
    std::size_t from{}, sent{};
    std::wstring headers;
    Handle session, connection, request;
};
State state;
void notify(DWORD status, void* info = nullptr, DWORD size = 0) {
    state.request.callback(&state.request, state.request.context, status, info, size);
}
HINTERNET WINAPI open(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD) {
    state.session = {};
    return &state.session;
}
HINTERNET WINAPI connect(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD) {
    state.connection = {};
    return &state.connection;
}
HINTERNET WINAPI open_request(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD) {
    state.request = {};
    state.headers.clear();
    state.plan = state.requests < state.plans.size() ? state.plans[state.requests] : Plan{};
    ++state.requests;
    return &state.request;
}
BOOL WINAPI set_timeouts(HINTERNET, int, int, int, int) { return TRUE; }
BOOL WINAPI set_option(HINTERNET handle, DWORD option, LPVOID value, DWORD) {
    if (option == WINHTTP_OPTION_CONTEXT_VALUE) static_cast<Handle*>(handle)->context = *static_cast<DWORD_PTR*>(value);
    return TRUE;
}
WINHTTP_STATUS_CALLBACK WINAPI set_callback(HINTERNET handle, WINHTTP_STATUS_CALLBACK callback, DWORD notifications, DWORD_PTR) {
    auto& request = *static_cast<Handle*>(handle);
    const auto previous = request.callback;
    request.callback = callback;
    request.notifications = notifications;
    return previous;
}
BOOL WINAPI add_headers(HINTERNET, LPCWSTR headers, DWORD, DWORD) {
    state.headers += headers;
    return TRUE;
}
BOOL WINAPI send(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR context) {
    state.request.context = context;
    state.asked.push_back(state.headers);
    notify(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    return TRUE;
}
std::size_t asked_from() {
    const auto at = state.headers.find(L"Range: bytes=");
    if (at == std::wstring::npos) return 0;
    return static_cast<std::size_t>(std::stoull(state.headers.substr(at + 13)));
}
BOOL WINAPI receive(HINTERNET, LPVOID) {
    state.from = state.sent = 0;
    state.status = 200;
    const auto from = asked_from();
    const bool same = state.headers.find(L"If-Range: ") == std::wstring::npos ||
                      state.headers.find(L"If-Range: " + std::wstring(state.etag.begin(), state.etag.end())) != std::wstring::npos;
    if (state.plan.status) state.status = state.plan.status;
    else if (from && state.ranges && same) {
        if (from >= state.body.size()) state.status = 416;
        else state.status = 206, state.from = from;
    }
    notify(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    return TRUE;
}
BOOL WINAPI query(HINTERNET, DWORD query, LPCWSTR, LPVOID value, LPDWORD size, LPDWORD) {
    std::wstring text;
    if (query == (WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER)) {
        *static_cast<DWORD*>(value) = state.status;
        *size = sizeof(DWORD);
        return TRUE;
    }
    const bool body = state.status == 200 || state.status == 206;
    if (query == WINHTTP_QUERY_ETAG) text.assign(state.etag.begin(), state.etag.end());
    else if (query == WINHTTP_QUERY_CONTENT_LENGTH && state.length && body) text = std::to_wstring(state.body.size() - state.from);
    else if (query == WINHTTP_QUERY_CONTENT_RANGE && state.status == 206)
        text = L"bytes " + std::to_wstring(state.from) + L"-" + std::to_wstring(state.body.size() - 1) + L"/" + std::to_wstring(state.body.size());
    if (text.empty()) {
        SetLastError(ERROR_WINHTTP_HEADER_NOT_FOUND);
        return FALSE;
    }
    const DWORD bytes = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
    if (*size < bytes) {
        *size = bytes;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    std::memcpy(value, text.c_str(), bytes);
    *size = bytes - sizeof(wchar_t);
    return TRUE;
}
BOOL WINAPI read(HINTERNET, LPVOID buffer, DWORD capacity, LPDWORD) {
    const bool body = state.status == 200 || state.status == 206;
    const std::size_t left = body ? state.body.size() - state.from - state.sent : 0;
    if (state.plan.cut != std::string::npos && state.sent >= state.plan.cut && left) {
        if (state.plan.stall) return TRUE; // never answers: the fetch's stall check has to end it
        WINHTTP_ASYNC_RESULT error{};
        error.dwError = ERROR_WINHTTP_CONNECTION_ERROR;
        notify(WINHTTP_CALLBACK_STATUS_REQUEST_ERROR, &error, sizeof(error));
        return TRUE;
    }
    auto n = std::min<std::size_t>({left, capacity, 1000});
    if (state.plan.cut != std::string::npos) n = std::min(n, state.plan.cut - state.sent);
    std::memcpy(buffer, state.body.data() + state.from + state.sent, n);
    state.sent += n;
    notify(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, buffer, static_cast<DWORD>(n));
    return TRUE;
}
BOOL WINAPI close(HINTERNET handle) {
    auto& object = *static_cast<Handle*>(handle);
    if (object.closed) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    object.closed = true;
    if (handle == &state.request && object.callback && object.context && (object.notifications & WINHTTP_CALLBACK_FLAG_HANDLES))
        notify(WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING);
    return TRUE;
}
}

#define WinHttpOpen fake_server::open
#define WinHttpConnect fake_server::connect
#define WinHttpOpenRequest fake_server::open_request
#define WinHttpSetTimeouts fake_server::set_timeouts
#define WinHttpSetOption fake_server::set_option
#define WinHttpSetStatusCallback fake_server::set_callback
#define WinHttpAddRequestHeaders fake_server::add_headers
#define WinHttpSendRequest fake_server::send
#define WinHttpReceiveResponse fake_server::receive
#define WinHttpQueryHeaders fake_server::query
#define WinHttpReadData fake_server::read
#define WinHttpCloseHandle fake_server::close
#include "../https_download.cpp"

namespace {
int failures{};
void check(bool condition, const char* what) {
    if (condition) return;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}
std::string contents(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void reset(std::size_t size = 5000) {
    fake_server::state = {};
    for (std::size_t i = 0; i < size; ++i) fake_server::state.body += static_cast<char>('a' + (i * 7) % 26);
}
}

int main(int argc, char** argv) {
    using namespace dingosdk::https;
    using fake_server::Plan;
    auto& server = fake_server::state;
    const auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path() / ("https-fetch-fixtures-" + std::to_string(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);
    const auto file = root / "pack.zip";
    auto part = file;
    part += L".part";
    Fetch options;
    options.max_bytes = 1 << 20;
    options.backoff_ms = 1;
    const auto get = [&](Watch* watch = nullptr) { return fetch(L"https://example.invalid/pack.zip", file, options, L"test", watch); };
    const auto clean = [&] {
        std::filesystem::remove(file, error);
        std::filesystem::remove(part, error);
    };

    reset();
    auto got = get();
    check(got.ok && got.attempts == 1 && !got.resumed && contents(file) == server.body, "a plain download arrives whole");
    check(!std::filesystem::exists(part), "nothing is left beside it");
    clean();

    // Dropped twice part way: each next request asks for the rest only, and the file is whole.
    reset();
    server.plans = {Plan{0, 1500}, Plan{0, 2000}};
    got = get();
    check(got.ok && got.attempts == 3 && got.resumed && contents(file) == server.body, "a dropped download goes on and arrives whole");
    check(server.asked.size() == 3 && server.asked[1].find(L"Range: bytes=1500-") != std::wstring::npos &&
              server.asked[2].find(L"Range: bytes=3500-") != std::wstring::npos && server.asked[1].find(L"If-Range: \"v1\"") != std::wstring::npos,
          "each next request asks for the rest, against the file's ETag");
    clean();

    // A server that sends the whole file again for a Range: started over, not appended.
    reset();
    server.ranges = false;
    server.plans = {Plan{0, 1200}};
    got = get();
    check(got.ok && got.attempts == 2 && contents(file) == server.body, "a server that ignores the Range is started over");
    clean();

    // The file changed between requests (its ETag): the whole new file, not two halves.
    // Dropped, then refused (404, not tried again): nothing is left, unless asked to keep what came.
    reset();
    server.plans = {Plan{0, 1000}, Plan{404}};
    got = get();
    check(!got.ok && got.http_status == 404 && got.attempts == 2, "a refusal after a drop ends it");
    check(!std::filesystem::exists(part) && !std::filesystem::exists(file), "a failure leaves nothing behind unless asked to keep it");
    reset();
    server.plans = {Plan{0, 1000}, Plan{404}};
    options.keep_partial = true;
    got = get();
    check(!got.ok && std::filesystem::exists(part) && std::filesystem::file_size(part) == 1000 && !std::filesystem::exists(file),
          "keep_partial keeps what came, and only that");
    server.plans.clear();
    server.requests = 0;
    server.asked.clear();
    got = get();
    check(got.ok && got.resumed && contents(file) == server.body && server.asked.size() == 1 &&
              server.asked.front().find(L"Range: bytes=1000-") != std::wstring::npos &&
              server.asked.front().find(L"If-Range: \"v1\"") != std::wstring::npos,
          "a later call goes on from the kept part, against the ETag kept with it");
    check(!std::filesystem::exists(part) && !std::filesystem::exists(std::filesystem::path(part) += L".etag"), "nothing is left once it is whole");
    clean();
    // The file changed since the part was kept (another ETag): it comes whole, not as two halves of two files.
    reset();
    server.plans = {Plan{0, 1000}, Plan{404}};
    got = get();
    server.plans.clear();
    server.requests = 0;
    server.asked.clear();
    std::reverse(server.body.begin(), server.body.end());
    server.etag = "\"v2\"";
    got = get();
    check(got.ok && contents(file) == server.body && server.asked.size() == 1, "a file changed since the kept part comes whole");
    clean();
    // A part with no ETag kept beside it is not gone on from.
    reset();
    {
        std::ofstream(part, std::ios::binary) << std::string(1000, 'x');
    }
    got = get();
    check(got.ok && contents(file) == server.body && server.asked.size() == 1 && server.asked.front().find(L"Range:") == std::wstring::npos,
          "a part kept without its ETag is started over");
    options.keep_partial = false;
    clean();

    // Busy, then fine.
    reset();
    server.plans = {Plan{503}, Plan{429}};
    got = get();
    check(got.ok && got.attempts == 3 && contents(file) == server.body, "503 and 429 are tried again");
    clean();

    // Not found: not tried again.
    reset();
    server.plans = {Plan{404}};
    got = get();
    check(!got.ok && got.http_status == 404 && got.attempts == 1 && !std::filesystem::exists(file), "a 404 is not tried again");
    clean();

    // Stalled after 2000 bytes: dropped after the stall time, gone on from there.
    reset();
    server.plans = {Plan{0, 2000, true}};
    options.stall_ms = 1000;
    {
        Watch watch;
        const auto before = GetTickCount64();
        got = get(&watch);
        const auto took = GetTickCount64() - before;
        check(got.ok && got.attempts == 2 && contents(file) == server.body && took >= 900 && took < 5000, "a stalled request is dropped and gone on from");
        check(server.asked.size() == 2 && server.asked[1].find(L"Range: bytes=2000-") != std::wstring::npos, "the stall's bytes are kept");
    }
    options.stall_ms = 60000;
    clean();

    // Too big, announced: refused at once.
    reset(5000);
    options.max_bytes = 4000;
    got = get();
    check(!got.ok && got.error == ERROR_FILE_TOO_LARGE && got.attempts == 1, "an announced body over the limit is refused");
    server.length = false;
    reset(5000);
    server.length = false;
    got = get();
    check(!got.ok && got.error == ERROR_FILE_TOO_LARGE && !std::filesystem::exists(part), "a streamed body over the limit is refused");
    options.max_bytes = 1 << 20;
    clean();

    // The size it was said to be.
    reset();
    options.expected_bytes = 4999;
    got = get();
    check(!got.ok && got.error == ERROR_BAD_LENGTH && !std::filesystem::exists(file), "a file of another size than expected is refused");
    options.expected_bytes = 0;
    clean();

    // Checked: a pieced-together file that fails its check is downloaded once more, whole; a whole one that fails is damaged.
    reset();
    server.plans = {Plan{0, 2500}};
    int checks = 0;
    options.verify = [&](const std::filesystem::path& path) {
        ++checks;
        return checks > 1 && contents(path) == server.body;
    };
    got = get();
    check(got.ok && checks == 2 && got.attempts == 3 && contents(file) == server.body, "a resumed file that fails its check is fetched again whole");
    clean();
    reset();
    options.verify = [](const std::filesystem::path&) { return false; };
    got = get();
    check(!got.ok && got.error == ERROR_INVALID_DATA && !std::filesystem::exists(file) && !std::filesystem::exists(part),
          "a whole file that fails its check is refused and removed");
    options.verify = nullptr;
    clean();

    // An existing destination is replaced only by the whole new file.
    reset();
    {
        std::ofstream(file, std::ios::binary) << "old";
    }
    server.plans = {Plan{404}};
    got = get();
    check(!got.ok && contents(file) == "old", "a failed download leaves the old file as it was");
    reset();
    got = get();
    check(got.ok && contents(file) == server.body, "a finished download replaces it");
    clean();

    // Stopped by the caller.
    reset();
    {
        Watch watch;
        watch.cancel.store(true);
        got = get(&watch);
        check(!got.ok && got.error == ERROR_CANCELLED && server.requests == 0, "a cancelled fetch makes no request");
    }

    std::filesystem::remove_all(root, error);
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "https fetch: all checks passed\n";
    return 0;
}
