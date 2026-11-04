#include "marketplace.h"
#include "marketplace_listing.h"
#include "marketplace_month.h"
#include "anim_studio.h"
#include "anim_core.h"
#include "Engine/Core/Json/json.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Vfs/https_download.h"
#include "Extension/Skater/xray_skeleton.h"
#include "miniz.h"
#include <Windows.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <wincodec.h>
#include <winhttp.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <mutex>
#include <set>
#include <thread>

namespace dingosdk::anim_studio::market {
namespace {
using logging::Channel;
using logging::Level;
using Microsoft::WRL::ComPtr;
constexpr const wchar_t* agent = L"ReSkate Animator Studio";

std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::string((std::istreambuf_iterator<char>(in)), {}) : std::string{};
}
void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += L".saving";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("could not write " + utf8(path));
    }
    std::filesystem::rename(temporary, path);
}
std::filesystem::path folder() { return studio_folder() / L"market"; }

// ---- requests the download helper does not make: posts with a token, and the upload's parts ----------
struct Answer {
    unsigned long status{};
    std::string body, etag, error;
};
Answer request(const wchar_t* method, const std::string& url, const std::vector<std::wstring>& headers, std::string_view body, DWORD timeout_ms) {
    Answer answer;
    const auto wide = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        answer.error = "not an https address";
        return answer;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring object(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo) object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    struct Handle {
        HINTERNET h{};
        ~Handle() { if (h) WinHttpCloseHandle(h); }
    };
    Handle session{WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) return answer.error = "no connection", answer;
    WinHttpSetTimeouts(session.h, static_cast<int>(timeout_ms), static_cast<int>(timeout_ms), static_cast<int>(timeout_ms), static_cast<int>(timeout_ms));
    Handle connect{WinHttpConnect(session.h, host.c_str(), parts.nPort, 0)};
    if (!connect.h) return answer.error = "could not reach " + url.substr(0, 60), answer;
    Handle call{WinHttpOpenRequest(connect.h, method, object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!call.h) return answer.error = "could not make the request", answer;
    // A token never follows a redirect anywhere.
    DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(call.h, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
    std::wstring all;
    for (const auto& header : headers) all += header + L"\r\n";
    if (!all.empty()) WinHttpAddRequestHeaders(call.h, all.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    if (!WinHttpSendRequest(call.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(call.h, nullptr)) {
        answer.error = "the connection failed (" + std::to_string(GetLastError()) + ")";
        return answer;
    }
    DWORD size = sizeof(answer.status);
    WinHttpQueryHeaders(call.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &answer.status, &size, WINHTTP_NO_HEADER_INDEX);
    std::array<wchar_t, 256> etag{};
    DWORD etag_size = static_cast<DWORD>(etag.size() * sizeof(wchar_t));
    if (WinHttpQueryHeaders(call.h, WINHTTP_QUERY_ETAG, WINHTTP_HEADER_NAME_BY_INDEX, etag.data(), &etag_size, WINHTTP_NO_HEADER_INDEX))
        for (DWORD i = 0; i < etag_size / sizeof(wchar_t); ++i) answer.etag += static_cast<char>(etag[i] & 0x7f);
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(call.h, &available) || !available) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(call.h, chunk.data(), available, &read) || !read) break;
        answer.body.append(chunk.data(), read);
        if (answer.body.size() > 8 * 1024 * 1024) break;
    }
    return answer;
}
std::string base64(std::string_view bytes) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const auto n = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8) | static_cast<unsigned char>(bytes[i + 2]);
        out += table[(n >> 18) & 63], out += table[(n >> 12) & 63], out += table[(n >> 6) & 63], out += table[n & 63];
    }
    if (i + 1 == bytes.size()) {
        const auto n = static_cast<unsigned char>(bytes[i]) << 16;
        out += table[(n >> 18) & 63], out += table[(n >> 12) & 63], out += "==";
    } else if (i + 2 == bytes.size()) {
        const auto n = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
        out += table[(n >> 18) & 63], out += table[(n >> 12) & 63], out += table[(n >> 6) & 63], out += '=';
    }
    return out;
}
std::string md5(std::string_view bytes) {
    std::array<unsigned char, 16> digest{};
    BCryptHash(BCRYPT_MD5_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()),
               digest.data(), static_cast<ULONG>(digest.size()));
    return std::string(reinterpret_cast<const char*>(digest.data()), digest.size());
}
// What Thunderstore said when it said no: its message, short.
std::string said(const Answer& a) {
    if (!a.error.empty()) return a.error;
    std::string text = a.body;
    try {
        const auto root = Json::parse(a.body);
        if (root.is_object())
            for (const auto& [key, value] : root.items()) {
                text = key + ": " + (value.is_string() ? value.string() : value.dump());
                break;
            }
    } catch (...) {}
    if (text.size() > 240) text.resize(240);
    return "Thunderstore answered " + std::to_string(a.status) + (text.empty() ? std::string{} : " (" + text + ")");
}

// ---- downloads ------------------------------------------------------------------------------------
// Downloads go through https::fetch: a request that stalls (under 16 KB in a minute) or drops is dropped and the next one
// goes on from where it got to (a Range request, against the file's ETag), up to eight in a row that bring nothing, with
// growing waits between them; a slow line that keeps moving is never cut off however long it takes. Players saw a pack's
// download fail with "it took too long" (1.1.x had one 30 minute limit and three tries, each from the start). A pack's
// zip is checked whole (every file's CRC) before it takes its place.
constexpr std::uint32_t stall_ms = 60000;
// Packs are tens of MB now; hundreds are taken, up to what the import takes (read_pack_zip: 512 MB).
constexpr std::uint64_t most_pack_bytes = 512ull * 1024 * 1024;
// Why a download failed, in words. Thunderstore says 200 before the body comes, so a body cut short (the connection
// dropped, it stalled) needs its own words, not "the download failed (200)".
std::string download_failure(const https::Download& got) {
    std::string why;
    if (got.http_status && got.http_status != 200 && got.http_status != 206)
        why = got.http_status == 429 || got.http_status >= 500 ? "Thunderstore was busy (it answered " + std::to_string(got.http_status) + ")"
              : got.http_status == 404                         ? std::string("Thunderstore does not have it any more (404)")
                                                               : "Thunderstore answered " + std::to_string(got.http_status);
    else
        switch (got.error) {
        case ERROR_WINHTTP_TIMEOUT: why = "the connection stopped sending (nothing came for a minute)"; break;
        case ERROR_TIMEOUT: why = "it ran out of time"; break;
        case ERROR_FILE_TOO_LARGE: why = "it is bigger than the studio takes (512 MB)"; break;
        case ERROR_CANCELLED: why = "it was stopped"; break;
        case ERROR_INVALID_DATA: why = "it arrived damaged twice (its zip did not check out)"; break;
        case ERROR_BAD_LENGTH: why = "it arrived another size than Thunderstore said"; break;
        case ERROR_WRITE_FAULT:
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: why = "it could not be written to the disk (is it full?)"; break;
        case ERROR_ACCESS_DENIED: why = "the studio's folder could not be written to"; break;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        case ERROR_WINHTTP_CANNOT_CONNECT: why = "Thunderstore could not be reached (no connection?)"; break;
        case ERROR_WINHTTP_CONNECTION_ERROR:
        case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: why = "the connection kept dropping"; break;
        case ERROR_WINHTTP_SECURE_FAILURE: why = "a secure connection could not be made (a proxy, antivirus or the PC's clock?)"; break;
        default: why = "Windows error " + std::to_string(got.error); break;
        }
    if (got.attempts > 1 && got.error != ERROR_CANCELLED) why += ", after " + std::to_string(got.attempts) + " tries";
    return why;
}
// What a failed listing read says: unreachable, or reached and refused.
std::string listing_failure(const https::Download& got) {
    return (got.http_status && got.http_status != 200 ? "Thunderstore's listing could not be read: " : "Thunderstore could not be reached: ") +
           download_failure(got) + ".";
}
// A file in the studio's market folder that a download goes into before it is read.
std::filesystem::path scratch_file(const std::string& what) {
    static std::atomic<unsigned> serial{};
    return folder() / L"downloads" / widen("get-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++serial) + "-" + what + ".tmp");
}
// A small answer from Thunderstore (a listing page, a README, a package's details), patiently: no limit on the whole of it
// (the community's whole listing is several MB), only on stalls, four of them in a row.
std::optional<std::string> fetch_text(const std::string& url, std::uint64_t most, std::uint32_t /*seconds*/, https::Download* result = nullptr) {
    const auto file = scratch_file("text");
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    https::Fetch options;
    options.max_bytes = most;
    options.stall_ms = stall_ms;
    options.tries = 4;
    options.backoff_ms = 2000;
    const auto got = https::fetch(widen(url), file, options, agent);
    if (result) *result = got;
    std::optional<std::string> text;
    if (got.ok) text = read_file(file);
    std::filesystem::remove(file, error);
    return text;
}
// A downloaded file read as a zip without reading it all into memory (miniz is built without its own file reading).
struct ZipFile {
    HANDLE file{INVALID_HANDLE_VALUE};
    mz_zip_archive archive{};
    bool open{};
    explicit ZipFile(const std::filesystem::path& path) {
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        LARGE_INTEGER size{};
        if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size)) return;
        archive.m_pRead = [](void* opaque, mz_uint64 at, void* buffer, size_t n) -> size_t {
            size_t done = 0;
            while (done < n) {
                OVERLAPPED where{};
                const auto offset = at + done;
                where.Offset = static_cast<DWORD>(offset);
                where.OffsetHigh = static_cast<DWORD>(offset >> 32);
                DWORD got = 0;
                const auto want = static_cast<DWORD>(std::min<size_t>(n - done, 1u << 30));
                if (!ReadFile(static_cast<HANDLE>(opaque), static_cast<char*>(buffer) + done, want, &got, &where) || !got) break;
                done += got;
            }
            return done;
        };
        archive.m_pIO_opaque = file;
        open = mz_zip_reader_init(&archive, static_cast<mz_uint64>(size.QuadPart), 0);
    }
    ~ZipFile() {
        if (open) mz_zip_reader_end(&archive);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
    ZipFile(const ZipFile&) = delete;
    ZipFile& operator=(const ZipFile&) = delete;
    // Every file in it reads back with its CRC: the whole zip came.
    bool whole() { return open && mz_zip_validate_archive(&archive, 0); }
    // One of the studio's packs: its marker at the top.
    bool studio() { return open && mz_zip_reader_locate_file(&archive, "reskate-animations.json", nullptr, 0) >= 0; }
};
// A file (an icon) downloaded to `destination`, patiently.
https::Download download_file(const std::string& url, const std::filesystem::path& destination, std::uint64_t most, https::Watch* watch) {
    https::Fetch options;
    options.max_bytes = most;
    options.stall_ms = stall_ms;
    options.tries = 4;
    options.backoff_ms = 2000;
    const auto got = https::fetch(widen(url), destination, options, agent, watch);
    if (!got.ok && got.error != ERROR_CANCELLED)
        logging::log(Level::warning, Channel::skater, "Animator Studio market: a download failed: {}", download_failure(got));
    return got;
}
// A pack's zip, downloaded to `destination` (replaced only by the whole, checked zip). What came of a failed download is
// kept beside it (destination + ".part"), so TRY AGAIN goes on from there.
https::Download download_pack(const std::string& url, const std::filesystem::path& destination, https::Watch* watch) {
    https::Fetch options;
    options.max_bytes = most_pack_bytes;
    options.stall_ms = stall_ms;
    options.tries = 8;
    options.keep_partial = true;
    options.verify = [](const std::filesystem::path& zip) { return ZipFile(zip).whole(); };
    const auto started = std::chrono::steady_clock::now();
    const auto got = https::fetch(widen(url), destination, options, agent, watch);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::error_code error;
    const auto size = got.ok ? std::filesystem::file_size(destination, error) : 0;
    if (got.ok)
        logging::log(Level::info, Channel::skater, "Animator Studio market: downloaded {:.1f} MB in {:.0f} s ({} request{}{}), its zip checked.",
                     static_cast<double>(size) / 1048576.0, seconds, got.attempts, got.attempts == 1 ? "" : "s", got.resumed ? ", resumed" : "");
    else
        logging::log(Level::warning, Channel::skater, "Animator Studio market: a pack's download failed after {:.0f} s: {}", seconds,
                     download_failure(got));
    return got;
}
// How far a download is, as a pack's line says it while it runs: "12.3 of 53.8 MB", or what it waits for.
std::string download_words(const https::Watch& watch) {
    const auto mb = [](std::uint64_t bytes) { return std::format("{:.1f}", static_cast<double>(bytes) / 1048576.0); };
    const auto received = watch.received.load(), total = watch.total.load();
    if (const auto retry = watch.retry_at_us.load()) {
        const auto now = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        const auto in = retry > now ? (retry - now + 999999) / 1000000 : 0;
        return std::format("connection lost at {} MB, trying again in {} s", mb(received), in);
    }
    const auto attempt = watch.attempt.load();
    const std::string again = attempt > 1 ? std::format(" (try {})", attempt) : std::string{};
    if (total) return mb(received) + " of " + mb(total) + " MB" + again;
    return received ? mb(received) + " MB" + again : "connecting" + again;
}
// Parts of downloads that never finished (a pack not tried again): gone after a few days.
void prune_parts(const std::filesystem::path& in) {
    std::error_code error;
    const auto now = std::filesystem::file_time_type::clock::now();
    for (const auto& entry : std::filesystem::directory_iterator(in, error)) {
        const auto name = entry.path().filename().wstring();
        if (!(name.ends_with(L".part") || name.ends_with(L".part.etag") || name.ends_with(L".tmp"))) continue;
        if (now - entry.last_write_time(error) > std::chrono::hours(72)) std::filesystem::remove(entry.path(), error);
    }
}

// ---- the token, kept encrypted for this Windows user ---------------------------------------------
std::string protect(std::string_view plain, bool encrypt) {
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))}, out{};
    const BOOL ok = encrypt ? CryptProtectData(&in, L"ReSkate Animator Studio", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                            : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok) return {};
    std::string result(reinterpret_cast<const char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}
std::filesystem::path token_file() { return folder() / L"account.key"; }
std::filesystem::path account_file() { return folder() / L"account.json"; }
std::string read_token() { return protect(read_file(token_file()), false); }

// ---- the package's icon: Thunderstore wants a 256 pixel PNG ---------------------------------------
std::string encode_png(IWICImagingFactory* factory, IWICBitmapSource* source) {
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) || FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(256, 256)) || FAILED(frame->SetPixelFormat(&format)) ||
        FAILED(frame->WriteSource(source, nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
        return {};
    HGLOBAL memory{};
    if (FAILED(GetHGlobalFromStream(stream.Get(), &memory))) return {};
    STATSTG stat{};
    stream->Stat(&stat, STATFLAG_NONAME);
    const void* bytes = GlobalLock(memory);
    std::string out(static_cast<const char*>(bytes), static_cast<std::size_t>(stat.cbSize.QuadPart));
    GlobalUnlock(memory);
    return out;
}
// The pack's picture made 256 a side, or - with none - one drawn: its colour, a strip of tape, a print of dots.
std::string icon_png(const std::string& picture, const std::string& name) {
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return {};
    if (!picture.empty()) {
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICBitmapScaler> scaler;
        ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(factory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(picture.data())), static_cast<DWORD>(picture.size()))) &&
            SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(frame.Get(), 256, 256, WICBitmapInterpolationModeFant)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
            if (auto png = encode_png(factory.Get(), converter.Get()); !png.empty()) return png;
    }
    static constexpr std::array<std::uint32_t, 6> colours{0x0183FF, 0xFF8C00, 0x2EB85C, 0xAA5CFF, 0xEB4650, 0xF0C828};
    unsigned hash = 2166136261u;
    for (const char c : name) hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    const auto accent = colours[hash % colours.size()];
    std::vector<std::uint32_t> pixels(256 * 256, 0xFF16161A);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            auto& p = pixels[static_cast<std::size_t>(y * 256 + x)];
            const int band = x + y;
            if (band > 210 && band < 300) p = 0xFF000000 | accent; // the tape
            else if (x > 180 && y < 76 && (x % 12) < 5 && (y % 12) < 5) p = 0xFF3A3A40; // the print
        }
    ComPtr<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmapFromMemory(256, 256, GUID_WICPixelFormat32bppBGRA, 256 * 4, static_cast<UINT>(pixels.size() * 4),
                                               reinterpret_cast<BYTE*>(pixels.data()), &bitmap)))
        return {};
    return encode_png(factory.Get(), bitmap.Get());
}
std::string make_zip(const std::vector<std::pair<std::string, std::string>>& files) { return stored_zip(files); }
struct Package {
    std::string name, version, file_name, zip;
};
// A pack as a Thunderstore package: manifest.json, icon.png, README.md and the studio's marker at the top, the pack's
// own files (its pack file and picture, its tricks) under them, as EXPORT lays them out.
// The package a pack goes up as: the one it went up as before (an update, even after the pack is renamed), else one named
// after it.
std::string package_of(const SharedPack& pack, const std::string& team) {
    if (const auto dash = pack.published_as.find('-'); dash != std::string::npos) {
        const auto owner = pack.published_as.substr(0, dash), name = pack.published_as.substr(dash + 1);
        if (plain_name(owner) && plain_name(name) && (team.empty() || _stricmp(owner.c_str(), team.c_str()) == 0)) return name;
    }
    return package_name(pack.name);
}
Package build_package(const SharedPack& pack, const std::string& version, const std::string& team) {
    Package out;
    out.name = package_of(pack, team);
    out.version = version;
    std::string summary = pack.description.substr(0, pack.description.find('\n'));
    if (summary.empty()) summary = std::to_string(pack.tricks.size()) + " tricks made in the ReSkate Animator Studio.";
    const auto room = 250 - std::string_view(description_tag).size();
    if (summary.size() > room) summary = summary.substr(0, room - 3) + "...";
    Json manifest = Json::object();
    manifest["name"] = out.name;
    manifest["version_number"] = version;
    manifest["website_url"] = std::string(studio_url);
    manifest["description"] = std::string(description_tag) + summary;
    manifest["dependencies"] = Json::array();
    Json marker = Json::object();
    marker["format"] = "reskate-animator-pack";
    marker["version"] = 1;
    marker["pack"] = pack.name;
    marker["tricks"] = static_cast<std::int64_t>(pack.tricks.size());
    std::vector<std::pair<std::string, std::string>> files{
        {"manifest.json", manifest.dump(2)},
        {"icon.png", icon_png(pack.picture, pack.name)},
        {"README.md", readme(pack.name, pack.description, pack.tricks, pack.made_by)},
        {"reskate-animations.json", marker.dump(2)},
    };
    if (files[1].second.empty()) throw std::runtime_error("could not make its icon");
    for (const auto& file : pack.files) files.push_back(file);
    out.zip = make_zip(files);
    out.file_name = out.name + "-" + version + ".zip";
    return out;
}

// ---- the market's own state ---------------------------------------------------------------------
struct Market {
    std::mutex mutex;
    std::vector<Pack> packs;
    bool loading{}, cache_read{};
    std::string error;
    std::chrono::steady_clock::time_point fetched{};
    bool ever{};
    std::set<std::string> favorites;
    std::map<std::string, std::string> installed; // full name (or "mod:<folder>") -> version
    std::map<std::string, std::string> installed_as; // full name -> the pack it became in the library
    bool like_prompt{true};
    std::string team;
    int team_state{};          // View::team_state
    std::string team_checked;  // the team that state is of
    int account_state{};       // View::account_state
    std::string account_checked; // the team that state is of
    // The last check that said yes (account.json "verified"): the team and a fingerprint of its token (never the token).
    // While Thunderstore cannot be reached, or is still being asked, that yes stands for the same token.
    std::string verified_as, verified_token;
    std::string token_print; // the linked token's fingerprint, worked out once
    Job job;
    Detail detail;
    std::deque<std::string> icon_queue;
    std::set<std::string> icons_asked;
    // Each pack's icon on disk, by "Team-Name|version": its file (UTF-8), "" when it is not there. Looked at once and then
    // kept: view() and want_icons() run every frame the MARKET is open, and a look at the disk for every pack each frame
    // cost a few milliseconds. The icon worker fills in the ones it downloads.
    std::map<std::string, std::string> icon_files;
    bool icon_worker{};
    // Each pack version's icon file ("" none on the disk), asked of the disk once: view() is called several times a frame
    // while the MARKET is open, and asking the disk for 50 icons cost 4.7 ms a call (2026-11-04).
    std::map<std::string, std::string> icon_files;
    struct Readme {
        std::string version, about;
        std::vector<std::string> tricks;
        KindCounts kinds{}; // its tricks by kind, worked out once as it is read (a pack can list hundreds)
        int state{}; // 1 being read, 2 read, 3 could not be read
    };
    std::map<std::string, Readme> readmes; // full name -> its README, for the version read
    std::deque<std::string> readme_queue;
    bool readme_worker{};
    bool settings_read{};
    Preview preview;
    bool preview_worker{};
    // TOP OF THE MONTH: each pack's versions (their downloads, when each came out) and where this month's counting began.
    VersionCounts counts;
    std::chrono::steady_clock::time_point counts_fetched{};
    bool counts_fresh{}; // read from Thunderstore this run (not only from the copy on disk)
    MonthBase month_base;
    // The developers' VERIFIED list (read from GitHub every few minutes, kept on disk between runs).
    VerifiedList verified_list;
    std::chrono::steady_clock::time_point verified_fetched{};
    bool verified_ever{};
    int look{};
    int dev_state{};
    std::string dev_login, dev_error;
};
Market& market() {
    static auto* m = new Market;
    return *m;
}
void save_lists_locked(const Market& m) {
    try {
        Json favorites = Json::array(), installed = Json::object(), installed_as = Json::object();
        for (const auto& f : m.favorites) favorites.push_back(f);
        for (const auto& [name, version] : m.installed) installed[name] = version;
        for (const auto& [name, pack] : m.installed_as) installed_as[name] = pack;
        write_file(folder() / L"settings.json", Json{{"favorites", std::move(favorites)}, {"installed", std::move(installed)},
                                                     {"installed_as", std::move(installed_as)}, {"like_prompt", m.like_prompt},
                                                     {"look", m.look}}.dump(2));
    } catch (...) {}
}
void read_settings_locked(Market& m) {
    if (m.settings_read) return;
    m.settings_read = true;
    try {
        // market/settings.json; market/market.json in older builds (its shape made it look like a saved trick called market).
        auto saved = read_file(folder() / L"settings.json");
        const bool old_name = saved.empty() && !(saved = read_file(folder() / L"market.json")).empty();
        if (!saved.empty()) {
            const auto root = Json::parse(saved);
            if (root.contains("favorites") && root.at("favorites").is_array())
                for (const auto& f : root.at("favorites"))
                    if (f.is_string()) m.favorites.insert(f.string());
            if (root.contains("installed") && root.at("installed").is_object())
                for (const auto& [name, version] : root.at("installed").items())
                    if (version.is_string()) m.installed[name] = version.string();
            if (root.contains("installed_as") && root.at("installed_as").is_object())
                for (const auto& [name, pack] : root.at("installed_as").items())
                    if (pack.is_string()) m.installed_as[name] = pack.string();
            m.like_prompt = root.value("like_prompt", true);
            if (root.contains("look") && root.at("look").is_number()) m.look = std::clamp(static_cast<int>(root.at("look").get<double>()), 0, 16);
            if (old_name) {
                save_lists_locked(m);
                std::error_code error;
                if (std::filesystem::exists(folder() / L"settings.json", error)) std::filesystem::remove(folder() / L"market.json", error);
            }
        }
        // What was counted and read before: this month's numbers and the VERIFIED list at once, while they are read again.
        try {
            if (const auto text = read_file(folder() / L"versions.json"); !text.empty()) m.counts = read_version_counts_json(text);
        } catch (...) {}
        try {
            if (const auto text = read_file(folder() / L"month.json"); !text.empty()) m.month_base = read_month_base(text);
        } catch (...) {}
        try {
            if (const auto text = read_file(folder() / L"verified.json"); !text.empty()) m.verified_list = read_verified(text);
        } catch (...) {}
        // A developer key that GitHub said could write: remembered (VERIFY says so if it no longer can).
        try {
            std::error_code missing;
            if (std::filesystem::is_regular_file(folder() / L"dev.key", missing))
                if (const auto text = read_file(folder() / L"dev.json"); !text.empty()) {
                    const auto root = Json::parse(text);
                    m.dev_login = root.value("login", "");
                    if (!m.dev_login.empty()) m.dev_state = 2;
                }
        } catch (...) {}
        if (const auto text = read_file(account_file()); !text.empty()) {
            const auto root = Json::parse(text);
            m.team = root.value("team", "");
            if (root.contains("verified") && root.at("verified").is_object()) {
                m.verified_as = root.at("verified").value("team", "");
                m.verified_token = root.at("verified").value("token", "");
            }
        }
    } catch (...) {}
}
// A fingerprint of a token, to know a later check is of the same one: not the token, and not enough to make one.
std::string token_fingerprint(const std::string& token) {
    if (token.empty()) return {};
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (const unsigned char b : md5("reskate-animator-studio/token/" + token)) out += hex[b >> 4], out += hex[b & 15];
    return out.substr(0, 16);
}
// This month, in UTC as Thunderstore's dates are ("2026-11").
std::string this_month() {
    const auto days = std::chrono::duration_cast<std::chrono::hours>(std::chrono::system_clock::now().time_since_epoch()).count() / 24;
    return month_of_day(days);
}
// The VERIFIED list as GitHub has it now, every few minutes (or when asked): kept on disk, and kept as it was when GitHub
// cannot be reached. Not there at all (no developer has verified anything yet) is an empty list.
void refresh_verified(bool force) {
    auto& m = market();
    {
        std::lock_guard lock(m.mutex);
        if (!force && m.verified_ever && std::chrono::steady_clock::now() - m.verified_fetched < std::chrono::minutes(3)) return;
        m.verified_ever = true;
        m.verified_fetched = std::chrono::steady_clock::now();
    }
    https::Download got{};
    const auto text = https::get_text(widen(verified_list_url()), 1024 * 1024, 30, agent, &got);
    std::optional<VerifiedList> list;
    try {
        if (text) list = read_verified(*text);
        else if (got.http_status == 404) list = VerifiedList{};
    } catch (...) {}
    if (!list) return;
    std::lock_guard lock(m.mutex);
    if (list->packs == m.verified_list.packs && list->makers == m.verified_list.makers) return;
    m.verified_list = std::move(*list);
    try {
        write_file(folder() / L"verified.json", verified_json(m.verified_list));
    } catch (...) {}
}
// A pack's newest version and its download, asked of the package itself. False when it cannot be had.
bool fill_version(Pack& p) {
    try {
        const auto text = fetch_text(std::string(site) + "/api/experimental/package/" + p.owner + "/" + p.name + "/", 4 * 1024 * 1024, 90);
        if (!text) return false;
        const auto root = Json::parse(*text);
        const auto& latest = root.at("latest");
        const auto version = latest.value("version_number", "");
        const auto download = latest.value("download_url", "");
        if (!plain_version(version) || !thunderstore_url(download)) return false;
        p.version = version;
        p.download_url = download;
        if (p.icon_url.empty() && thunderstore_url(latest.value("icon", ""))) p.icon_url = latest.value("icon", "");
        return true;
    } catch (...) { return false; }
}
// The packs as the studio keeps them between runs (packs.json), to show at once while the listing is fetched again.
std::string packs_json(const std::vector<Pack>& packs) {
    Json out = Json::array();
    for (const auto& p : packs)
        out.push_back(Json{{"full_name", p.full_name}, {"owner", p.owner}, {"name", p.name}, {"version", p.version},
                           {"description", p.description}, {"icon_url", p.icon_url}, {"download_url", p.download_url},
                           {"package_url", p.package_url}, {"updated", p.updated}, {"created", p.created}, {"downloads", p.downloads},
                           {"rating", p.rating}, {"section", p.section}});
    return out.dump(2);
}
std::vector<Pack> read_packs_json(const std::string& text) {
    std::vector<Pack> out;
    const auto root = Json::parse(text, JsonLimits{8 * 1024 * 1024, 16, 8 * 1024 * 1024});
    if (!root.is_array()) return out;
    for (const auto& j : root) {
        try {
            Pack p;
            p.full_name = j.value("full_name", "");
            p.owner = j.value("owner", "");
            p.name = j.value("name", "");
            p.version = j.value("version", "");
            if (!plain_name(p.owner) || !plain_name(p.name) || p.full_name != p.owner + "-" + p.name) continue;
            p.title = p.name;
            std::replace(p.title.begin(), p.title.end(), '_', ' ');
            p.description = j.value("description", "");
            p.icon_url = j.value("icon_url", "");
            p.download_url = j.value("download_url", "");
            p.package_url = j.value("package_url", "");
            p.updated = j.value("updated", "");
            p.created = j.value("created", p.updated);
            p.downloads = static_cast<std::uint64_t>(std::max(0.0, j.value("downloads", 0.0)));
            p.rating = static_cast<int>(j.value("rating", 0.0));
            p.section = j.value("section", false);
            if (!thunderstore_url(p.icon_url)) p.icon_url.clear();
            if (!thunderstore_url(p.download_url) || !plain_version(p.version)) continue;
            out.push_back(std::move(p));
        } catch (...) {}
    }
    return out;
}
bool listing_pages(std::string url, std::vector<Pack>& packs, std::string& error);
// The Animation Packs section's number on Thunderstore (its listing filters by number, not by name), "" when it cannot be
// found out.
std::string section_number() {
    const auto text = fetch_text(std::string(site) + "/api/cyberstorm/community/" + community + "/filters/", 512 * 1024, 60);
    if (!text) return {};
    try {
        const auto root = Json::parse(*text);
        for (const auto& c : root.at("package_categories"))
            if (c.value("slug", "") == category) {
                const auto id = c.value("id", "");
                if (!id.empty() && std::all_of(id.begin(), id.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) return id;
            }
    } catch (...) {}
    return {};
}
// The studio's packs from the search, every page of it, and every pack in the Animation Packs section (found by its
// section even where the search misses it). False when the search could not be had (the whole listing is read then, as
// before); the section is only added to it.
bool search_packs(std::vector<Pack>& packs, std::string& error) {
    if (!listing_pages(std::string(site) + "/api/cyberstorm/listing/" + community + "/?q=Animator%20Studio%20pack&ordering=last-updated", packs, error))
        return false;
    if (const auto section = section_number(); !section.empty()) {
        std::string ignored;
        if (!listing_pages(std::string(site) + "/api/cyberstorm/listing/" + community + "/?included_categories=" + section + "&ordering=last-updated",
                           packs, ignored))
            logging::log(Level::warning, Channel::skater, "Animator Studio market: the Animation Packs section could not be read ({}).", ignored);
    }
    for (auto& p : packs)
        if (p.version.empty()) fill_version(p);
    std::erase_if(packs, [](const Pack& p) { return p.version.empty() || p.download_url.empty(); });
    return true;
}
// One listing of Thunderstore's, every page of it, into `packs` (a pack already there only takes its section mark).
bool listing_pages(std::string url, std::vector<Pack>& packs, std::string& error) {
    const std::string pages = std::string(site) + "/api/cyberstorm/listing/" + community + "/";
    for (int page = 0; page < 50 && !url.empty(); ++page) {
        https::Download result{};
        const auto text = fetch_text(url, 8 * 1024 * 1024, 120, &result);
        if (!text) {
            error = listing_failure(result);
            return false;
        }
        std::string next;
        try {
            auto found = read_search(*text, next);
            for (auto& p : found) {
                const auto there = std::find_if(packs.begin(), packs.end(), [&](const Pack& q) { return q.full_name == p.full_name; });
                if (there == packs.end()) packs.push_back(std::move(p));
                else there->section = there->section || p.section;
            }
        } catch (const std::exception& e) {
            error = std::string("Thunderstore's listing could not be read: ") + e.what();
            return false;
        }
        // Only further pages of the same listing are followed.
        url = next.starts_with(pages) ? next : std::string{};
    }
    return true;
}
std::filesystem::path icon_path(const Pack& p) { return folder() / L"icons" / widen(p.full_name + "-" + p.version + ".png"); }
void icon_worker() {
    auto& m = market();
    for (;;) {
        std::string url, key;
        std::filesystem::path path;
        {
            std::lock_guard lock(m.mutex);
            if (m.icon_queue.empty()) {
                m.icon_worker = false;
                return;
            }
            const auto name = m.icon_queue.front();
            m.icon_queue.pop_front();
            const auto found = std::find_if(m.packs.begin(), m.packs.end(), [&](const Pack& p) { return p.full_name == name; });
            if (found == m.packs.end() || found->icon_url.empty()) continue;
            url = found->icon_url;
            path = icon_path(*found);
            key = found->full_name + "|" + found->version;
        }
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        const auto got = download_file(url, path, 2 * 1024 * 1024, nullptr);
        std::lock_guard lock(m.mutex);
        m.icon_files[key] = got.ok ? utf8(path) : std::string{};
    }
}
// The pack's icon file, "" when it is not on disk: looked at the first time only (the icon worker says when it writes one).
const std::string& icon_file_locked(Market& m, const Pack& p) {
    const auto key = p.full_name + "|" + p.version;
    auto found = m.icon_files.find(key);
    if (found == m.icon_files.end()) {
        std::error_code error;
        const auto path = icon_path(p);
        found = m.icon_files.emplace(key, std::filesystem::is_regular_file(path, error) ? utf8(path) : std::string{}).first;
    }
    return found->second;
}
// The READMEs, one after another: from disk when this version's was read before, else from Thunderstore.
std::filesystem::path readme_path(const std::string& full_name, const std::string& version) {
    return folder() / L"readmes" / widen(full_name + "-" + version + ".md");
}
void readme_worker() {
    auto& m = market();
    for (;;) {
        std::string full_name, owner, name, version;
        {
            std::lock_guard lock(m.mutex);
            if (m.readme_queue.empty()) {
                m.readme_worker = false;
                return;
            }
            full_name = m.readme_queue.front();
            m.readme_queue.pop_front();
            const auto found = std::find_if(m.packs.begin(), m.packs.end(), [&](const Pack& p) { return p.full_name == full_name; });
            if (found == m.packs.end() || !plain_name(found->owner) || !plain_name(found->name) || !plain_version(found->version)) continue;
            owner = found->owner, name = found->name, version = found->version;
        }
        Market::Readme readme;
        readme.version = version;
        const auto path = readme_path(full_name, version);
        std::string markdown = read_file(path);
        if (markdown.empty()) {
            const auto text = fetch_text(std::string(site) + "/api/experimental/package/" + owner + "/" + name + "/" + version + "/readme/",
                                         1024 * 1024, 90);
            try {
                if (text) markdown = Json::parse(*text).value("markdown", "");
                if (!markdown.empty()) write_file(path, markdown);
            } catch (...) { markdown.clear(); }
        }
        if (markdown.empty()) readme.state = 3;
        else {
            read_readme(markdown, readme.about, readme.tricks);
            readme.kinds = trick_kinds(readme.tricks);
            readme.state = 2;
        }
        std::lock_guard lock(m.mutex);
        m.readmes[full_name] = std::move(readme);
    }
}
// PREVIEW: a pack's zip, kept in market/previews by version (the last few), and its tricks unpacked beside it.
constexpr std::size_t previews_kept = 5;
std::filesystem::path previews_folder() { return folder() / L"previews"; }
std::wstring preview_stem(const Pack& pack) { return widen(pack.owner + "-" + pack.name + "-" + pack.version); }
// The ones looked at longest ago go, with their tricks, past the last few.
void prune_previews(const std::filesystem::path& keep) {
    std::error_code error;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> zips;
    for (const auto& entry : std::filesystem::directory_iterator(previews_folder(), error))
        if (entry.is_regular_file(error) && entry.path().extension() == L".zip" && entry.path() != keep)
            zips.emplace_back(entry.last_write_time(error), entry.path());
    std::sort(zips.begin(), zips.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t i = previews_kept - 1; i < zips.size(); ++i) {
        auto unpacked = zips[i].second;
        unpacked.replace_extension();
        std::filesystem::remove_all(unpacked, error);
        std::filesystem::remove(zips[i].second, error);
    }
}
std::vector<PreviewTrick> fetch_preview(const Pack& pack) {
    if (!plain_name(pack.owner) || !plain_name(pack.name) || !plain_version(pack.version) || pack.download_url.empty())
        throw std::runtime_error("it cannot be downloaded");
    const auto stem = preview_stem(pack);
    const auto zip = previews_folder() / (stem + L".zip");
    std::error_code error;
    std::filesystem::create_directories(zip.parent_path(), error);
    if (!std::filesystem::is_regular_file(zip, error)) {
        https::Watch watch;
        std::atomic<bool> done{};
        // How far it is, while it runs (waited for before going on: it reads `watch` and `done`).
        std::thread watcher([&] {
            while (!done.load()) {
                {
                    auto& m = market();
                    std::lock_guard lock(m.mutex);
                    // Another pack's preview asked for: this one stops (what came is kept, to go on from), and that one's
                    // download starts.
                    if (m.preview.full_name != pack.full_name) watch.cancel.store(true);
                    else if (const auto total = watch.total.load())
                        m.preview.fraction = static_cast<float>(watch.received.load()) / static_cast<float>(total);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        struct Join {
            std::atomic<bool>& done;
            std::thread& thread;
            ~Join() {
                done.store(true);
                if (thread.joinable()) thread.join();
            }
        } join_watcher{done, watcher};
        const auto got = download_pack(pack.download_url, zip, &watch);
        done.store(true);
        watcher.join();
        if (!got.ok) throw std::runtime_error("the download failed: " + download_failure(got));
        if (!ZipFile(zip).studio()) {
            std::filesystem::remove(zip, error);
            throw std::runtime_error(pack.title + " is not an Animator Studio pack");
        }
    }
    // Looked at now: kept the longest.
    std::filesystem::last_write_time(zip, std::filesystem::file_time_type::clock::now(), error);
    std::vector<UnpackedTrick> unpacked;
    std::string message;
    if (!unpack_preview(zip, previews_folder() / stem, unpacked, message)) {
        std::filesystem::remove(zip, error); // downloaded again next time
        throw std::runtime_error(message);
    }
    prune_previews(zip);
    std::vector<PreviewTrick> tricks;
    for (auto& t : unpacked)
        tricks.push_back({std::move(t.name), std::move(t.label), std::move(t.trick), std::move(t.pop), t.switch_stance, t.fakie, std::move(t.json), std::move(t.clip)});
    return tricks;
}
// The pack asked for last, until it is ready: one asked for while another downloads comes next.
void preview_worker() {
    auto& m = market();
    for (;;) {
        Pack pack;
        {
            std::lock_guard lock(m.mutex);
            const auto found = std::find_if(m.packs.begin(), m.packs.end(), [&](const Pack& p) { return p.full_name == m.preview.full_name; });
            if (m.preview.state != 1 || found == m.packs.end()) {
                if (m.preview.state == 1) m.preview.state = 3, m.preview.error = "It is not in the MARKET any more.";
                m.preview_worker = false;
                return;
            }
            pack = *found;
        }
        Preview done;
        done.full_name = pack.full_name;
        done.version = pack.version;
        try {
            done.tricks = fetch_preview(pack);
            done.state = 2;
        } catch (const std::exception& e) {
            done.state = 3;
            done.error = e.what();
            logging::log(Level::warning, Channel::skater, "Animator Studio market: {} could not be previewed: {}", pack.full_name, e.what());
        }
        std::lock_guard lock(m.mutex);
        if (m.preview.full_name == done.full_name && m.preview.state == 1) {
            done.fraction = 1;
            m.preview = std::move(done);
            m.preview_worker = false;
            return;
        }
    }
}
void start_job(const std::string& what, std::function<void(Job&)> work) {
    auto& m = market();
    {
        std::lock_guard lock(m.mutex);
        if (m.job.running) return;
        m.job = {};
        m.job.running = true;
        m.job.what = what;
    }
    std::thread([work = std::move(work)] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Job result;
        try {
            work(result);
        } catch (const std::exception& e) {
            result.failed = true;
            result.message = e.what();
        }
        if (SUCCEEDED(com)) CoUninitialize();
        auto& m = market();
        std::lock_guard lock(m.mutex);
        m.job.running = false;
        m.job.failed = result.failed;
        m.job.message = result.message;
        m.job.url = result.url;
        logging::log(result.failed ? Level::warning : Level::info, Channel::skater, "Animator Studio market: {}", result.message);
    }).detach();
}
void progress(float fraction) {
    auto& m = market();
    std::lock_guard lock(m.mutex);
    m.job.fraction = fraction;
}
} // namespace

View view() {
    auto& m = market();
    std::lock_guard lock(m.mutex);
    read_settings_locked(m);
    View v;
    v.loading = m.loading;
    v.error = m.error;
    v.team = m.team;
    v.team_state = m.team_checked == m.team ? m.team_state : 0;
    v.account_state = m.account_checked == m.team ? m.account_state : 0;
    v.like_prompt = m.like_prompt;
    v.dev_state = m.dev_state;
    v.dev_login = m.dev_login;
    v.dev_error = m.dev_error;
    v.month = this_month();
    v.month_known = !m.counts.empty();
    v.verified_count = m.verified_list.packs.size() + m.verified_list.makers.size();
    v.look = m.look;
    v.job = m.job;
    v.detail = m.detail;
    v.preview = m.preview;
    v.packs = m.packs;
    for (auto& p : v.packs) {
        p.favorite = m.favorites.contains(p.full_name);
        p.mine = !m.team.empty() && _stricmp(p.owner.c_str(), m.team.c_str()) == 0;
        if (const auto found = m.installed.find(p.full_name); found != m.installed.end()) p.installed = found->second;
        if (const auto found = m.installed_as.find(p.full_name); found != m.installed_as.end()) p.installed_as = found->second;
        if (const auto found = m.readmes.find(p.full_name); found != m.readmes.end() && found->second.version == p.version) {
            p.about = found->second.about;
            p.tricks = found->second.tricks;
            p.kinds = found->second.kinds;
            p.readme = found->second.state;
        }
        p.dev = builder_of(p.owner) != nullptr;                // BUILT BY, with a team: DEV
        p.crew = crew_member(p.owner) != nullptr || p.dev; // THE CREW: CREW CERTIFIED (and in the crew's lists, the devs too)
        p.verified = verified_by(m.verified_list, p.owner, p.full_name);
        {
            const auto found = m.counts.find(p.full_name);
            const auto n = month_numbers(p, found == m.counts.end() ? nullptr : &found->second, m.month_base, v.month);
            p.month_downloads = n.downloads;
            p.month_likes = n.likes;
            p.month_counted = n.counted;
            if (found != m.counts.end())
                for (const auto& version : found->second) p.versions.push_back({version.version, version.date, version.downloads});
        }
        p.icon_file = icon_file_locked(m, p);
    }
    return v;
}
void refresh(bool force) {
    auto& m = market();
    {
        std::lock_guard lock(m.mutex);
        read_settings_locked(m);
        if (!m.cache_read) {
            // What was fetched last time, at once, while the new listing comes.
            m.cache_read = true;
            try {
                if (const auto text = read_file(folder() / L"packs.json"); !text.empty()) m.packs = read_packs_json(text);
                else if (const auto old = read_file(folder() / L"listing.json"); !old.empty()) m.packs = read_listing(old);
            } catch (...) {}
            for (const auto& p : m.packs) {
                // What was read before, from disk, so the search has it at once.
                Market::Readme readme;
                readme.version = p.version;
                if (const auto markdown = read_file(readme_path(p.full_name, p.version)); !markdown.empty()) {
                    read_readme(markdown, readme.about, readme.tricks);
                    readme.kinds = trick_kinds(readme.tricks);
                    readme.state = 2;
                    m.readmes[p.full_name] = std::move(readme);
                }
            }
        }
        // Fetched again when the MARKET is opened a minute or more after the last time (Thunderstore's search is up to
        // date within a minute), or when asked.
        if (m.loading || (!force && m.ever && std::chrono::steady_clock::now() - m.fetched < std::chrono::minutes(1))) return;
        m.loading = true;
        m.error.clear();
    }
    std::thread([] {
        auto& m = market();
        std::vector<Pack> packs;
        std::string error;
        std::optional<VersionCounts> counts;
        const auto listing_url = std::string(site) + "/c/" + community + "/api/v1/package/";
        // Thunderstore's search first: only the studio's packs, and a new one shows within a minute. Its whole listing
        // (every package of the community, several MB, rebuilt only now and then) when the search cannot be had.
        if (!search_packs(packs, error)) {
            packs.clear();
            error.clear();
            https::Download result{};
            const auto text = fetch_text(listing_url, 96ull * 1024 * 1024, 300, &result);
            if (!text) error = listing_failure(result);
            else
                try {
                    packs = read_listing(*text);
                    counts = read_version_counts(*text);
                } catch (const std::exception& e) { error = std::string("Thunderstore's listing could not be read: ") + e.what(); }
        } else {
            // TOP OF THE MONTH needs each version's downloads, which only the whole listing has: read every twenty minutes.
            bool due = false;
            {
                std::lock_guard lock(m.mutex);
                due = !m.counts_fresh || std::chrono::steady_clock::now() - m.counts_fetched > std::chrono::minutes(20);
            }
            if (due)
                if (const auto text = fetch_text(listing_url, 96ull * 1024 * 1024, 300))
                    try {
                        counts = read_version_counts(*text);
                    } catch (...) {}
        }
        refresh_verified(false);
        if (error.empty()) {
            try {
                write_file(folder() / L"packs.json", packs_json(packs));
                // Older versions kept the whole listing here (several MB). Nothing reads it.
                std::error_code gone;
                std::filesystem::remove(folder() / L"listing.json", gone);
            } catch (...) {}
        }
        {
            std::lock_guard lock(m.mutex);
            m.loading = false;
            m.ever = true;
            m.fetched = std::chrono::steady_clock::now();
            m.error = error;
            if (error.empty()) m.packs = std::move(packs);
            if (counts && !counts->empty()) {
                m.counts = std::move(*counts);
                m.counts_fresh = true;
                m.counts_fetched = std::chrono::steady_clock::now();
                try {
                    write_file(folder() / L"versions.json", version_counts_json(m.counts));
                } catch (...) {}
            }
            // Where this month's counting starts: a new month starts it again, a pack seen for the first time this month
            // starts from what it has now.
            if (error.empty() && update_month_base(m.month_base, m.packs, m.counts, this_month()))
                try {
                    write_file(folder() / L"month.json", month_base_json(m.month_base));
                } catch (...) {}
        }
        {
            std::vector<std::string> names;
            {
                std::lock_guard lock(m.mutex);
                for (const auto& p : m.packs)
                    if (names.size() < 400) names.push_back(p.full_name);
            }
            want_readmes(names);
        }
        take_installed_mods();
    }).detach();
}
void want_icons(const std::vector<std::string>& full_names) {
    auto& m = market();
    std::lock_guard lock(m.mutex);
    for (const auto& name : full_names) {
        const auto found = std::find_if(m.packs.begin(), m.packs.end(), [&](const Pack& p) { return p.full_name == name; });
        if (found == m.packs.end() || found->icon_url.empty()) continue;
        const auto key = name + "|" + found->version;
        if (m.icons_asked.contains(key) || !icon_file_locked(m, *found).empty()) continue;
        m.icons_asked.insert(key);
        m.icon_queue.push_back(name);
    }
    if (!m.icon_queue.empty() && !m.icon_worker) {
        m.icon_worker = true;
        std::thread(icon_worker).detach();
    }
}
void want_readmes(const std::vector<std::string>& full_names) {
    auto& m = market();
    std::lock_guard lock(m.mutex);
    for (const auto& name : full_names) {
        const auto found = std::find_if(m.packs.begin(), m.packs.end(), [&](const Pack& p) { return p.full_name == name; });
        if (found == m.packs.end()) continue;
        auto& readme = m.readmes[name];
        if (readme.version == found->version && readme.state != 0) continue; // read, or being read
        readme = {};
        readme.version = found->version;
        readme.state = 1;
        m.readme_queue.push_back(name);
    }
    if (!m.readme_queue.empty() && !m.readme_worker) {
        m.readme_worker = true;
        std::thread(readme_worker).detach();
    }
}
void set_favorite(const std::string& full_name, bool on) {
    auto& m = market();
    std::lock_guard lock(m.mutex);
    read_settings_locked(m);
    if (on) m.favorites.insert(full_name);
    else m.favorites.erase(full_name);
    save_lists_locked(m);
}
void forget_installed(const std::string& library_pack) {
    if (library_pack.empty()) return;
    auto& m = market();
    std::lock_guard lock(m.mutex);
    read_settings_locked(m);
    bool changed = false;
    for (auto it = m.installed_as.begin(); it != m.installed_as.end();) {
        if (it->second != library_pack || it->first.starts_with("mod:")) {
            ++it;
            continue;
        }
        m.installed.erase(it->first);
        for (auto& p : m.packs)
            if (p.full_name == it->first) {
                p.installed.clear();
                p.installed_as.clear();
            }
        logging::log(Level::info, Channel::skater, "Animation Studio: {} is gone, so {} is not installed any more.", library_pack, it->first);
        it = m.installed_as.erase(it);
        changed = true;
    }
    if (changed) save_lists_locked(m);
}
void installed_pack_renamed(const std::string& from, const std::string& to) {
    auto& m = market();
    std::lock_guard lock(m.mutex);
    read_settings_locked(m);
    if (!rename_installed_pack(m.installed_as, from, to)) return;
    for (auto& p : m.packs)
        if (p.installed_as == from) p.installed_as = to;
    save_lists_locked(m);
}
void set_like_prompt(bool on) {
    auto& m = market();
    std::lock_guard lock(m.mutex);
    read_settings_locked(m);
    m.like_prompt = on;
    save_lists_locked(m);
}
void open_page(const std::string& full_name) {
    std::string url;
    {
        auto& m = market();
        std::lock_guard lock(m.mutex);
        const auto found = std::find_if(m.packs.begin(), m.packs.end(), [&](const Pack& p) { return p.full_name == full_name; });
        if (found == m.packs.end()) return;
        url = found->package_url;
    }
    if (url.starts_with(std::string(site) + "/")) ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
void open_team_page(std::string_view team) {
    if (team.empty() || !std::all_of(team.begin(), team.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; })) return;
    ShellExecuteW(nullptr, L"open", widen(team_page(team)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
// The packs GET and UPDATE put in the library (by the names they have there), waiting for the LIBRARY to ask whether to
// add each to a playlist (take_fresh_installs). Kept apart from the market's own state: only the install writes it.
static std::mutex fresh_mutex;
static std::vector<std::string> fresh_packs;
static void note_fresh_install(const std::vector<std::string>& made) {
    std::lock_guard lock(fresh_mutex);
    for (const auto& pack : made)
        if (!pack.empty() && std::find(fresh_packs.begin(), fresh_packs.end(), pack) == fresh_packs.end() && fresh_packs.size() < 64)
            fresh_packs.push_back(pack);
}
std::vector<std::string> take_fresh_installs() {
    std::lock_guard lock(fresh_mutex);
    return std::exchange(fresh_packs, {});
}
void install(const std::string& full_name) {
    Pack pack;
    {
        auto& m = market();
        std::lock_guard lock(m.mutex);
        const auto found = std::find_if(m.packs.begin(), m.packs.end(), [&](const Pack& p) { return p.full_name == full_name; });
        if (found == m.packs.end()) return;
        pack = *found;
    }
    start_job("Installing " + pack.title, [pack](Job& result) {
        const auto zip = folder() / L"downloads" / widen(pack.full_name + "-" + pack.version + ".zip");
        std::error_code error;
        std::filesystem::create_directories(zip.parent_path(), error);
        std::filesystem::remove(zip, error);
        prune_parts(zip.parent_path());
        // The download goes when the install is done, whether it went in or not.
        struct Gone {
            const std::filesystem::path& zip;
            ~Gone() {
                std::error_code ignored;
                std::filesystem::remove(zip, ignored);
            }
        } gone{zip};
        https::Watch watch;
        std::atomic<bool> done{};
        // How far the download is, while it runs. Waited for before going on: it reads `watch` and `done`, which go
        // when this does.
        const std::string installing = "Installing " + pack.title;
        std::thread watcher([&] {
            while (!done.load()) {
                const auto total = watch.total.load();
                {
                    // How far, in words too: a line that keeps moving, and says so when it waits to try again.
                    auto& m = market();
                    std::lock_guard lock(m.mutex);
                    if (total) m.job.fraction = 0.9f * static_cast<float>(watch.received.load()) / static_cast<float>(total);
                    m.job.what = installing + " - " + download_words(watch);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        struct Join {
            std::atomic<bool>& done;
            std::thread& thread;
            ~Join() {
                done.store(true);
                if (thread.joinable()) thread.join();
            }
        } join_watcher{done, watcher};
        // Previewed first: that download, the same version's, is the one put in.
        bool previewed = false;
        if (plain_name(pack.owner) && plain_name(pack.name) && plain_version(pack.version)) {
            const auto kept = previews_folder() / (preview_stem(pack) + L".zip");
            previewed = std::filesystem::is_regular_file(kept, error) && std::filesystem::copy_file(kept, zip, error) && !error;
            if (!previewed) std::filesystem::remove(zip, error);
        }
        const auto got = previewed ? https::Download{} : download_pack(pack.download_url, zip, &watch);
        done.store(true);
        watcher.join();
        if (!previewed && !got.ok)
            throw std::runtime_error("the download failed: " + download_failure(got) +
                                     (got.error != ERROR_CANCELLED && got.error != ERROR_INVALID_DATA ? ". TRY AGAIN goes on from where it stopped." : "."));
        {
            auto& m = market();
            std::lock_guard lock(m.mutex);
            m.job.what = installing + " - checking it";
        }
        // It has to be one of the studio's (its marker at the top), and whole: a kept preview is checked again too.
        {
            ZipFile archive(zip);
            if (!archive.open) throw std::runtime_error("the download is not a zip");
            if (!archive.studio()) throw std::runtime_error(pack.title + " is not an Animator Studio pack");
            if (previewed && !archive.whole()) {
                std::error_code gone_error;
                std::filesystem::remove(previews_folder() / (preview_stem(pack) + L".zip"), gone_error);
                throw std::runtime_error("the kept preview of it is damaged, so it was removed: INSTALL again downloads it");
            }
        }
        progress(0.95f);
        {
            auto& m = market();
            std::lock_guard lock(m.mutex);
            m.job.what = installing + " - putting it in your LIBRARY";
        }
        std::string message;
        std::vector<std::string> made;
        // An UPDATE or a REINSTALL replaces the pack it became before, rather than adding another copy of it.
        std::string before;
        {
            auto& m = market();
            std::lock_guard lock(m.mutex);
            if (const auto found = m.installed_as.find(pack.full_name); found != m.installed_as.end()) before = found->second;
        }
        const bool ok = import_pack_zip(zip, message, &made, before);
        std::filesystem::remove(zip, error);
        if (!ok) throw std::runtime_error(message);
        auto& m = market();
        {
            std::lock_guard lock(m.mutex);
            m.installed[pack.full_name] = pack.version;
            if (!made.empty()) m.installed_as[pack.full_name] = made.front();
            save_lists_locked(m);
        }
        note_fresh_install(made); // the LIBRARY asks whether to add it to a playlist
        result.message = "Installed " + pack.title + " " + pack.version + ". " + message;
    });
}
void want_preview(const std::string& full_name) {
    auto& m = market();
    std::lock_guard lock(m.mutex);
    const auto found = std::find_if(m.packs.begin(), m.packs.end(), [&](const Pack& p) { return p.full_name == full_name; });
    if (found == m.packs.end()) return;
    // Asked for again: as it is, unless its tricks' files have gone since (then downloaded or unpacked again).
    std::error_code error;
    const bool still_there = m.preview.tricks.empty() || std::filesystem::is_regular_file(m.preview.tricks.front().json, error);
    if (m.preview.full_name == full_name && m.preview.version == found->version && (m.preview.state == 1 || (m.preview.state == 2 && still_there))) return;
    m.preview = {};
    m.preview.full_name = full_name;
    m.preview.version = found->version;
    m.preview.state = 1;
    if (m.preview_worker) return; // it takes this one next
    m.preview_worker = true;
    try {
        std::thread(preview_worker).detach();
    } catch (...) {
        m.preview_worker = false;
        m.preview.state = 3;
        m.preview.error = "It could not be started.";
    }
}
// ---- VERIFIED: the developer key and the list on GitHub ----------------------------------------------
static std::filesystem::path dev_key_file() { return folder() / L"dev.key"; }
static std::filesystem::path dev_file() { return folder() / L"dev.json"; }
static std::vector<std::wstring> github_headers(const std::string& token, bool json_body = false) {
    std::vector<std::wstring> out{L"Authorization: Bearer " + widen(token), L"Accept: application/vnd.github+json", L"X-GitHub-Api-Version: 2022-11-28"};
    if (json_body) out.push_back(L"Content-Type: application/json");
    return out;
}
// What GitHub said when it said no: its message, short.
static std::string github_said(const Answer& a) {
    if (!a.error.empty()) return "GitHub could not be reached: " + a.error;
    std::string text;
    try {
        const auto root = Json::parse(a.body);
        if (root.is_object()) text = root.value("message", "");
    } catch (...) {}
    if (text.size() > 200) text.resize(200);
    return "GitHub answered " + std::to_string(a.status) + (text.empty() ? std::string{} : " (" + text + ")");
}
static std::string github_api() { return std::string("https://api.github.com/repos/") + verified_repo; }
bool link_dev_key(const std::string& token, std::string& message) {
    if (token.size() < 20 || token.size() > 512 || token.find_first_of(" \r\n\t") != std::string::npos) {
        message = "That is not a GitHub token: make one on github.com under Settings > Developer settings > Personal access tokens, and paste it here.";
        return false;
    }
    const auto sealed = protect(token, true);
    if (sealed.empty()) {
        message = "The key could not be kept safely on this PC.";
        return false;
    }
    try {
        write_file(dev_key_file(), sealed);
        std::error_code error;
        std::filesystem::remove(dev_file(), error);
    } catch (const std::exception& e) {
        message = e.what();
        return false;
    }
    {
        auto& m = market();
        std::lock_guard lock(m.mutex);
        m.dev_state = 0;
        m.dev_login.clear();
        m.dev_error.clear();
    }
    check_dev_key(true);
    message = "Developer key kept. Asking GitHub whether it can write to the studio's repository...";
    return true;
}
void unlink_dev_key() {
    std::error_code error;
    std::filesystem::remove(dev_key_file(), error);
    std::filesystem::remove(dev_file(), error);
    auto& m = market();
    std::lock_guard lock(m.mutex);
    m.dev_state = 0;
    m.dev_login.clear();
    m.dev_error.clear();
}
void check_dev_key(bool again) {
    {
        auto& m = market();
        std::lock_guard lock(m.mutex);
        read_settings_locked(m);
        if (m.dev_state == 1 || (!again && m.dev_state != 0)) return;
        std::error_code missing;
        if (!std::filesystem::is_regular_file(dev_key_file(), missing)) {
            m.dev_state = 0;
            return;
        }
        m.dev_state = 1;
    }
    std::thread([] {
        int state = 4;
        std::string login, why;
        const auto token = protect(read_file(dev_key_file()), false);
        if (token.empty()) state = 3, why = "The key on this PC could not be read: add it again.";
        else {
            // The repository as the key's account sees it: whether it may push to it.
            const auto repo = request(L"GET", github_api(), github_headers(token), {}, 30000);
            if (repo.status == 200) {
                bool push = false;
                try {
                    const auto root = Json::parse(repo.body);
                    if (root.contains("permissions") && root.at("permissions").is_object()) push = root.at("permissions").value("push", false);
                } catch (...) {}
                if (push) {
                    state = 2;
                    const auto user = request(L"GET", "https://api.github.com/user", github_headers(token), {}, 30000);
                    try {
                        if (user.status == 200) login = Json::parse(user.body).value("login", "");
                    } catch (...) {}
                    if (login.empty()) login = "developer";
                } else {
                    state = 3;
                    why = std::string("This key's account cannot write to ") + verified_repo + ". Only the studio's developers can verify packs.";
                }
            } else if (repo.status == 401 || repo.status == 403 || repo.status == 404) {
                state = 3;
                why = "GitHub did not take the key (wrong, expired or taken back): " + github_said(repo);
            } else why = github_said(repo);
        }
        logging::log(Level::info, Channel::skater, "Animator Studio market: the developer key {}.",
                     state == 2 ? "can verify packs" : state == 3 ? "cannot verify packs" : "could not be checked");
        auto& m = market();
        std::lock_guard lock(m.mutex);
        m.dev_state = state;
        m.dev_login = login;
        m.dev_error = why;
        try {
            std::error_code error;
            if (state == 2) write_file(dev_file(), Json{{"login", login}}.dump(2));
            else if (state == 3) std::filesystem::remove(dev_file(), error);
        } catch (...) {}
    }).detach();
}
void set_verified(const std::string& full_name, bool on) {
    if (!plain_full_name(full_name)) return;
    std::string title = full_name;
    {
        auto& m = market();
        std::lock_guard lock(m.mutex);
        for (const auto& p : m.packs)
            if (p.full_name == full_name) title = p.title;
    }
    start_job((on ? "Verifying " : "Taking VERIFIED off ") + title, [full_name, title, on](Job& result) {
        const auto token = protect(read_file(dev_key_file()), false);
        if (token.empty()) throw std::runtime_error("No developer key on this PC: add one in SETTINGS > STUDIO > Developer key.");
        const auto api = github_api();
        const auto file = api + "/contents/" + verified_file;
        for (int attempt = 0; attempt < 2; ++attempt) {
            progress(0.15f);
            VerifiedList list;
            std::string sha;
            const auto got = request(L"GET", file + "?ref=" + verified_branch, github_headers(token), {}, 30000);
            if (got.status == 200) {
                const auto root = Json::parse(got.body);
                sha = root.value("sha", "");
                list = read_verified(base64_decode(root.value("content", "")));
            } else if (got.status == 404) {
                // Not there yet: the file, or its branch, made from the default branch's newest commit the first time.
                const auto ref = request(L"GET", api + "/git/ref/heads/" + verified_branch, github_headers(token), {}, 30000);
                if (ref.status == 404) {
                    const auto repo = request(L"GET", api, github_headers(token), {}, 30000);
                    if (repo.status != 200) throw std::runtime_error(github_said(repo));
                    const auto main = Json::parse(repo.body).value("default_branch", "main");
                    const auto head = request(L"GET", api + "/git/ref/heads/" + main, github_headers(token), {}, 30000);
                    if (head.status != 200) throw std::runtime_error(github_said(head));
                    const auto root = Json::parse(head.body);
                    const auto from = root.contains("object") && root.at("object").is_object() ? root.at("object").value("sha", "") : std::string{};
                    if (from.empty()) throw std::runtime_error("GitHub did not say where " + main + " is.");
                    const auto made = request(L"POST", api + "/git/refs", github_headers(token, true),
                                              Json{{"ref", std::string("refs/heads/") + verified_branch}, {"sha", from}}.dump(), 30000);
                    if (made.status != 201 && made.status != 422) throw std::runtime_error(github_said(made)); // 422: made meanwhile
                } else if (ref.status != 200) throw std::runtime_error(github_said(ref));
            } else throw std::runtime_error(github_said(got));
            progress(0.55f);
            std::erase_if(list.packs, [&](const std::string& p) { return same_name(p, full_name); });
            if (on) list.packs.insert(full_name);
            Json body{{"message", std::string("Market: ") + full_name + (on ? " is VERIFIED" : " is no longer VERIFIED")},
                      {"content", base64_encode(verified_json(list))},
                      {"branch", verified_branch}};
            if (!sha.empty()) body["sha"] = sha;
            const auto put = request(L"PUT", file, github_headers(token, true), body.dump(), 30000);
            if (put.status == 200 || put.status == 201) {
                auto& m = market();
                {
                    std::lock_guard lock(m.mutex);
                    m.verified_list = list;
                    m.verified_fetched = std::chrono::steady_clock::now();
                    try {
                        write_file(folder() / L"verified.json", verified_json(list));
                    } catch (...) {}
                }
                const auto dash = full_name.find('-');
                const bool by_maker = !on && std::any_of(list.makers.begin(), list.makers.end(),
                                                         [&](const std::string& t) { return same_name(t, std::string_view(full_name).substr(0, dash)); });
                result.message = on ? title + " is VERIFIED: it is on the VERIFIED shelf now, and in everyone's MARKET within a few minutes."
                               : by_maker ? title + " is off the list, but its maker is VERIFIED, so it still shows the tag."
                                          : title + " is no longer VERIFIED: everyone's MARKET has it within a few minutes.";
                return;
            }
            if ((put.status == 409 || put.status == 422) && attempt == 0) continue; // changed meanwhile: read it again
            if (put.status == 403 || put.status == 404)
                throw std::runtime_error(std::string("GitHub would not let the key write to ") + verified_repo +
                                         ": it needs Contents read and write there. " + github_said(put));
            throw std::runtime_error(github_said(put));
        }
        throw std::runtime_error("The VERIFIED list changed on GitHub while it was being written: try again.");
    });
}
void set_look(int look) {
    auto& m = market();
    std::lock_guard lock(m.mutex);
    read_settings_locked(m);
    m.look = std::clamp(look, 0, 16);
    save_lists_locked(m);
}
bool link_account(const std::string& team, const std::string& token, std::string& message) {
    const bool team_ok = !team.empty() && team.size() <= 64 &&
                         std::all_of(team.begin(), team.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    if (!team_ok) {
        message = "A team's name is letters, digits and _ only, as it is on Thunderstore.";
        return false;
    }
    if (token.size() < 16 || token.size() > 512 || token.find_first_of(" \r\n\t") != std::string::npos) {
        message = "That is not a service account token: make one on thunderstore.io under your team's Service accounts, and paste it here.";
        return false;
    }
    const auto sealed = protect(token, true);
    if (sealed.empty()) {
        message = "The token could not be kept safely on this PC.";
        return false;
    }
    try {
        write_file(token_file(), sealed);
        write_file(account_file(), Json{{"team", team}}.dump(2));
    } catch (const std::exception& e) {
        message = e.what();
        return false;
    }
    {
        auto& m = market();
        std::lock_guard lock(m.mutex);
        m.team = team;
        m.team_state = 0;
        m.team_checked.clear();
        m.account_state = 0;
        m.account_checked.clear();
        m.verified_as.clear(); m.verified_token.clear(); m.token_print.clear(); // a new token is checked afresh
    }
    message = "Linked to the Thunderstore team " + team + ". Your packs can be published from PACKS.";
    check_team(true); // a pack's name typed for the team is found out now, not when a publish fails (and its token checked)
    return true;
}
std::atomic<unsigned> account_changes{};
void account_changed() { account_changes.fetch_add(1, std::memory_order_acq_rel); }
void unlink_account() {
    std::error_code error;
    std::filesystem::remove(token_file(), error);
    std::filesystem::remove(account_file(), error);
    auto& m = market();
    std::lock_guard lock(m.mutex);
    m.team.clear();
    m.team_state = 0;
    m.team_checked.clear();
    m.account_state = 0;
    m.account_checked.clear();
    m.verified_as.clear(); m.verified_token.clear(); m.token_print.clear();
    account_changed();
}
void check_team(bool again) {
    verify_account(again);
    std::string team;
    {
        auto& m = market();
        std::lock_guard lock(m.mutex);
        read_settings_locked(m);
        if (m.team.empty() || (m.team_checked == m.team && m.team_state == 1)) return;
        if (!again && m.team_checked == m.team && m.team_state != 0) return;
        team = m.team;
        m.team_checked = team;
        // Not a Thunderstore team's name (letters, digits and _: account.json edited by hand): not looked up, it goes in the
        // address.
        if (!std::all_of(team.begin(), team.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; })) {
            m.team_state = 3;
            return;
        }
        m.team_state = 1;
    }
    std::thread([team] {
        https::Download result{};
        const auto text = fetch_text(std::string(site) + "/api/cyberstorm/team/" + team + "/", 64 * 1024, 60, &result);
        const int state = text ? 2 : result.http_status == 404 ? 3 : 4;
        logging::log(Level::info, Channel::skater, "Animation Studio: the Thunderstore team {} {}.", team,
                     state == 2 ? "is there" : state == 3 ? "does not exist" : "could not be looked up");
        auto& m = market();
        std::lock_guard lock(m.mutex);
        if (m.team_checked == team) m.team_state = state;
    }).detach();
}
void verify_account(bool again) {
    std::string team;
    {
        auto& m = market();
        std::lock_guard lock(m.mutex);
        read_settings_locked(m);
        if (m.team.empty() || (m.account_checked == m.team && m.account_state == 1)) return;
        if (!again && m.account_checked == m.team && m.account_state != 0) return;
        team = m.team;
        m.account_checked = team;
        if (!std::all_of(team.begin(), team.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; })) {
            m.account_state = 3;
            return;
        }
        m.account_state = 1;
    }
    std::thread([team] {
        int state = 3;
        std::string print;
        if (const auto token = read_token(); !token.empty()) {
            print = token_fingerprint(token);
            // Thunderstore's own check of a package before an upload: first of all whether the token may upload under the
            // team ("Missing privileges to upload under author ..."). A made-up package, so nothing else stands in its way;
            // nothing is uploaded.
            Json manifest = Json::object();
            manifest["name"] = "AnimatorStudioAccountCheck";
            manifest["version_number"] = "0.0.1";
            manifest["website_url"] = "";
            manifest["description"] = "Animator Studio account check";
            manifest["dependencies"] = Json::array();
            Json body = Json::object();
            body["namespace"] = team;
            body["manifest_data"] = base64(manifest.dump());
            const auto answer = request(L"POST", std::string(site) + "/api/experimental/submission/validate/manifest-v1/",
                                        {L"Authorization: Bearer " + widen(token), L"Content-Type: application/json"}, body.dump(), 30000);
            if (answer.status == 200) state = 2;
            else if (answer.status == 400)
                // Turned down for the team (not its token, or no such team): not yours. Anything else is checked after it.
                state = answer.body.find("privileges") != std::string::npos || answer.body.find("\"namespace\"") != std::string::npos ? 3 : 2;
            else if (answer.status == 401 || answer.status == 403) state = 3;
            else state = 4;
            logging::log(Level::info, Channel::skater, "Animation Studio: the linked token {} for the Thunderstore team {} ({}).",
                         state == 2 ? "publishes" : state == 3 ? "does not publish" : "could not be checked", team, answer.status);
        }
        auto& m = market();
        {
            std::lock_guard lock(m.mutex);
            if (m.account_checked != team) return;
            m.account_state = state;
            if (!print.empty()) m.token_print = print;
            // A yes is remembered for this token (account.json), a no forgets it; Thunderstore not reached changes nothing.
            if (state == 2 || state == 3) {
                m.verified_as = state == 2 ? team : std::string();
                m.verified_token = state == 2 ? print : std::string();
                try {
                    Json root{{"team", m.team}};
                    if (state == 2) root["verified"] = Json{{"team", team}, {"token", print}};
                    write_file(account_file(), root.dump(2));
                } catch (...) {}
            }
        }
        account_changed();
    }).detach();
}
// The team's earlier yes, for the token linked now (while Thunderstore is still being asked, or cannot be reached).
bool verified_before_locked(Market& m) {
    if (m.verified_as.empty() || m.verified_token.empty() || _stricmp(m.verified_as.c_str(), m.team.c_str()) != 0) return false;
    if (m.token_print.empty()) m.token_print = token_fingerprint(read_token());
    return m.token_print == m.verified_token;
}
std::string verified_team() {
    bool ask = false;
    std::string out;
    {
        auto& m = market();
        std::lock_guard lock(m.mutex);
        read_settings_locked(m);
        if (m.team.empty()) return {};
        if (m.account_checked != m.team || m.account_state == 0) ask = true;
        if (m.account_checked == m.team && m.account_state == 2) out = m.team;
        else if ((m.account_checked != m.team || m.account_state == 0 || m.account_state == 1 || m.account_state == 4) &&
                 verified_before_locked(m))
            out = m.team;
    }
    if (ask) verify_account();
    return out;
}
unsigned account_generation() { return account_changes.load(std::memory_order_acquire); }
std::string installed_from(const std::string& pack_name) {
    auto& m = market();
    std::lock_guard lock(m.mutex);
    read_settings_locked(m);
    for (const auto& [full_name, pack] : m.installed_as) {
        if (pack != pack_name) continue;
        std::string name = full_name.starts_with("mod:") ? full_name.substr(4) : full_name; // the launcher's Mods/<Team-Pack>
        const auto dash = name.find('-');
        return dash == std::string::npos ? std::string{} : name.substr(0, dash);
    }
    return {};
}
std::string export_package(const std::string& pack_name, std::string& message, const std::string& version) {
    try {
        SharedPack pack;
        if (!share_pack(pack_name, pack, message)) return {};
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Package package;
        try {
            package = build_package(pack, plain_version(version) ? version : next_version(pack.version, pack.published), market().team);
        } catch (...) {
            if (SUCCEEDED(com)) CoUninitialize();
            throw;
        }
        if (SUCCEEDED(com)) CoUninitialize();
        const auto path = pack_export_folder() / widen(market().team.empty() ? package.file_name : market().team + "-" + package.file_name);
        write_file(path, package.zip);
        message = "Made " + utf8(path.filename()) + " for Thunderstore: upload it on thunderstore.io/c/reskate in the Animation Packs category, or publish it from here with a linked team.";
        if (!pack.left_out.empty()) message += " " + left_out_note(pack.left_out);
        ShellExecuteW(nullptr, L"open", path.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return utf8(path);
    } catch (const std::exception& e) {
        message = std::string("Could not make the package: ") + e.what();
        return {};
    }
}
void publish(const std::string& pack_name, const std::string& wanted_version, const std::string& wanted_package) {
    std::string team;
    {
        auto& m = market();
        std::lock_guard lock(m.mutex);
        read_settings_locked(m);
        team = m.team;
    }
    start_job("Publishing " + pack_name, [pack_name, team, wanted_version, wanted_package](Job& result) {
        if (team.empty()) throw std::runtime_error("Link your Thunderstore team first (MARKET > SIGN IN).");
        const auto token = read_token();
        if (token.empty()) throw std::runtime_error("Your Thunderstore token is missing: link your team again.");
        const std::wstring auth = L"Authorization: Bearer " + widen(token);
        SharedPack pack;
        std::string message;
        if (!share_pack(pack_name, pack, message)) throw std::runtime_error(message);
        if (pack.published_as.empty() && !wanted_package.empty()) pack.published_as = wanted_package;
        const auto version = wanted_version.empty() ? next_version(pack.version, pack.published) : wanted_version;
        if (!plain_version(version) || std::count(version.begin(), version.end(), '.') != 2)
            throw std::runtime_error("A version is three numbers with dots between them, as 1.0.1.");
        if (!pack.published.empty() && !version_after(version, pack.published))
            throw std::runtime_error("Thunderstore takes an update only with a higher version than " + pack.published + ".");
        const auto package = build_package(pack, version, team);
        progress(0.1f);
        const auto api = std::string(site) + "/api/experimental/";
        // 1. Where to put it.
        Json start = Json::object();
        start["filename"] = package.file_name;
        start["file_size_bytes"] = static_cast<std::int64_t>(package.zip.size());
        const auto started = request(L"POST", api + "usermedia/initiate-upload/", {auth, L"Content-Type: application/json"}, start.dump(), 60000);
        if (started.status != 201 && started.status != 200)
            throw std::runtime_error(started.status == 401 || started.status == 403
                                         ? "Thunderstore did not take your token. Make a new one for your team on thunderstore.io and put it in MARKET > ACCOUNT."
                                         : said(started));
        const auto answer = Json::parse(started.body);
        const auto uuid = answer.at("user_media").value("uuid", "");
        if (uuid.empty() || !answer.contains("upload_urls") || !answer.at("upload_urls").is_array()) throw std::runtime_error("Thunderstore did not say where to upload");
        const auto abort = [&] { (void)request(L"POST", api + "usermedia/" + uuid + "/abort-upload/", {auth}, {}, 30000); };
        // 2. Its parts, each to where Thunderstore said (never with the token).
        Json parts = Json::array();
        const auto& urls = answer.at("upload_urls");
        for (std::size_t i = 0; i < urls.size(); ++i) {
            const auto& part = urls.at(i);
            const auto url = part.value("url", "");
            const auto offset = static_cast<std::size_t>(part.value("offset", 0.0));
            const auto length = static_cast<std::size_t>(part.value("length", 0.0));
            if (!url.starts_with("https://") || offset + length > package.zip.size()) {
                abort();
                throw std::runtime_error("Thunderstore gave an upload address that is not safe to use");
            }
            const std::string_view bytes(package.zip.data() + offset, length);
            const auto put = request(L"PUT", url, {L"Content-MD5: " + widen(base64(md5(bytes)))}, bytes, 300000);
            if (put.status < 200 || put.status >= 300 || put.etag.empty()) {
                abort();
                throw std::runtime_error("Uploading failed: " + said(put));
            }
            Json done = Json::object();
            done["ETag"] = put.etag;
            done["PartNumber"] = static_cast<std::int64_t>(part.value("part_number", 0.0));
            parts.push_back(std::move(done));
            progress(0.1f + 0.75f * static_cast<float>(i + 1) / static_cast<float>(urls.size()));
        }
        // 3. Uploaded.
        Json finish = Json::object();
        finish["parts"] = std::move(parts);
        const auto finished = request(L"POST", api + "usermedia/" + uuid + "/finish-upload/", {auth, L"Content-Type: application/json"}, finish.dump(), 120000);
        if (finished.status != 200) {
            abort();
            throw std::runtime_error("Finishing the upload failed: " + said(finished));
        }
        progress(0.9f);
        // 4. Published, in the ReSkate community's Animation Packs section (an update is put in it too).
        Json submit = Json::object();
        submit["author_name"] = team;
        submit["categories"] = Json::array();
        submit["communities"] = Json::array({Json(std::string(community))});
        Json categories = Json::object();
        categories[community] = Json::array({Json(std::string(category))});
        submit["community_categories"] = std::move(categories);
        submit["has_nsfw_content"] = false;
        submit["upload_uuid"] = uuid;
        const auto submitted = request(L"POST", api + "submission/submit/", {auth, L"Content-Type: application/json"}, submit.dump(), 180000);
        if (submitted.status != 200 && submitted.status != 201) {
            // A team Thunderstore does not know (a pack's name typed for it): said plainly, and ACCOUNT shows it.
            if (submitted.status == 400 && submitted.body.find("author_name") != std::string::npos) {
                {
                    auto& m = market();
                    std::lock_guard lock(m.mutex);
                    m.team_checked = team;
                    m.team_state = 3;
                }
                throw std::runtime_error("Thunderstore has no team called " + team + ". Sign in again in MARKET > ACCOUNT with your team's name "
                                         "(thunderstore.io > Settings > Teams), not a pack's name.");
            }
            throw std::runtime_error("Thunderstore did not take the pack: " + said(submitted));
        }
        pack_published(pack_name, version, team + "-" + package.name);
        result.url = std::string(site) + "/c/" + community + "/p/" + team + "/" + package.name + "/";
        result.message = (pack.published.empty() ? "Published " : "Updated ") + pack.name + " to " + version + " on Thunderstore as " + team + "-" +
                         package.name + ". It shows in the MARKET within a minute.";
        if (!pack.left_out.empty()) result.message += " " + left_out_note(pack.left_out);
        progress(1);
        refresh(true);
    });
}
void take_installed_mods() {
    std::filesystem::path game;
    try { game = xray::game_folder(); } catch (...) { return; }
    auto& m = market();
    {
        std::lock_guard lock(m.mutex);
        read_settings_locked(m);
    }
    std::error_code error;
    for (const auto& root : {game / L"ModData" / L"Default" / L"Mods", game / L"Mods"}) {
        if (!std::filesystem::is_directory(root, error)) continue;
        for (const auto& mod : std::filesystem::directory_iterator(root, error)) {
            if (!mod.is_directory(error) || !std::filesystem::is_regular_file(mod.path() / L"reskate-animations.json", error)) continue;
            std::string version = "?";
            try { version = Json::parse(read_file(mod.path() / L"manifest.json")).value("version_number", "?"); } catch (...) {}
            const auto key = "mod:" + utf8(mod.path().filename());
            {
                std::lock_guard lock(m.mutex);
                if (const auto found = m.installed.find(key); found != m.installed.end() && found->second == version) continue;
            }
            // The mod's files, as a zip, through the studio's import.
            try {
                std::vector<std::pair<std::string, std::string>> files;
                std::size_t total = 0;
                for (std::filesystem::recursive_directory_iterator it(mod.path(), error), end; !error && it != end; it.increment(error)) {
                    if (!it->is_regular_file(error)) continue;
                    const auto size = it->file_size(error);
                    if (size > 64ull * 1024 * 1024 || (total += size) > 512ull * 1024 * 1024) throw std::runtime_error("too big");
                    auto relative = std::filesystem::relative(it->path(), mod.path(), error).generic_u8string();
                    files.emplace_back(std::string(relative.begin(), relative.end()), read_file(it->path()));
                }
                const auto zip = folder() / L"downloads" / (mod.path().filename().wstring() + L".zip");
                write_file(zip, make_zip(files));
                std::string message, before;
                {
                    std::lock_guard lock(m.mutex);
                    if (const auto found = m.installed_as.find(key); found != m.installed_as.end()) before = found->second;
                }
                std::vector<std::string> made;
                const bool ok = import_pack_zip(zip, message, &made, before); // a new version of the mod replaces the old
                std::filesystem::remove(zip, error);
                logging::log(ok ? Level::info : Level::warning, Channel::skater, "Animator Studio market: {} from the Mods folder: {}", utf8(mod.path().filename()), message);
                if (ok) {
                    std::lock_guard lock(m.mutex);
                    m.installed[key] = version;
                    if (!made.empty()) m.installed_as[key] = made.front();
                    save_lists_locked(m);
                }
            } catch (const std::exception& e) {
                logging::log(Level::warning, Channel::skater, "Animator Studio market: {} not taken in: {}", utf8(mod.path().filename()), e.what());
            }
        }
    }
}
} // namespace dingosdk::anim_studio::market
