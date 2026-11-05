#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace dingosdk::https {
struct Download {
    bool ok{};
    unsigned long http_status{}, error{};
    std::uint32_t attempts{}; // fetch: how many requests it took
    bool resumed{};           // fetch: it went on from bytes an earlier request (or call) had brought
};
// A long download as another thread sees it: how far it is (`total` 0 when the server does
// not say), and a way to stop it (the download then fails with ERROR_CANCELLED).
struct Watch {
    std::atomic<std::uint64_t> received{}, total{};
    std::atomic<bool> cancel{};
    // Absolute steady_clock epoch microseconds; zero leaves only the GET timeout.
    std::atomic<std::uint64_t> deadline_us{};
    // How long one step (finding the server, connecting, each read) may wait, in milliseconds,
    // for slow connections; zero keeps the usual 5 seconds to connect and 10 per read.
    std::atomic<std::uint32_t> patience_ms{};
    // fetch: the request it is on (1 the first), and while it waits to try again, when it will (steady_clock epoch
    // microseconds; zero when not waiting). For saying "the connection dropped, trying again".
    std::atomic<std::uint32_t> attempt{};
    std::atomic<std::uint64_t> retry_at_us{};
};
// How fetch goes about a large file.
struct Fetch {
    std::uint64_t max_bytes{};       // refused past this, announced or as it streams
    std::uint64_t expected_bytes{};  // its size when known beforehand (a release asset's); 0 takes the server's word
    // A request that brings less than 16 KB in this long has stalled: it is dropped and the next one goes on from where it
    // got to. Not a limit on the whole download: a slow line that keeps moving finishes, however long it takes.
    std::uint32_t stall_ms{60000};
    std::uint32_t connect_ms{30000}; // finding the server and connecting, each request
    std::uint32_t total_seconds{};   // a limit on the whole download; 0 none (stalls and tries end it)
    int tries{8};                    // requests in a row that bring nothing new before it gives up
    std::uint32_t backoff_ms{1000};  // the wait before the first try again, doubled each time it brings nothing (to 30 s)
    // A failed download keeps what came (destination + ".part") so the next fetch of it goes on from there; otherwise a
    // failure leaves nothing behind.
    bool keep_partial{};
    // The whole file, checked before it takes the destination's place (a zip's CRCs, a SHA-256): false fails it with
    // ERROR_INVALID_DATA. One that went on from earlier bytes and fails is downloaded once more from the start.
    std::function<bool(const std::filesystem::path&)> verify;
};
// Downloads `url` to `destination`, patiently, for files of hundreds of MB on slow or dropping connections: into
// destination + ".part", going on with a Range request (If-Range on its ETag) after a dropped or stalled request where
// the server allows it (from the start where it does not), trying again with growing waits on connection errors and on
// 408, 429 and 5xx. `destination` is replaced only by the whole file, checked against its size (and `verify`).
// HTTPS only, as get.
Download fetch(std::wstring_view url, const std::filesystem::path& destination, const Fetch& options, const wchar_t* agent,
               Watch* watch = nullptr);
// Whether fetch would try a failed request again (a connection error, a stall, 408, 429, 5xx).
bool worth_trying_again(const Download& got) noexcept;
// Streams an HTTPS GET into `destination`, which must not exist yet. Follows
// redirects that stay on HTTPS; refuses bodies over `max_bytes` or transfers
// that take longer than `timeout_seconds` in total.
Download get(std::wstring_view url, const std::filesystem::path& destination,
    std::uint64_t max_bytes, std::uint32_t timeout_seconds, const wchar_t* agent, Watch* watch = nullptr);
// The body of the same GET, for an answer small enough to hold: nothing when it
// fails, and `result`, when given, says how.
std::optional<std::string> get_text(std::wstring_view url, std::uint64_t max_bytes,
    std::uint32_t timeout_seconds, const wchar_t* agent, Download* result = nullptr, Watch* watch = nullptr);
// Content-Length of an HTTPS HEAD, when the server gives one. The body is not read. Nothing
// when the request fails or the length is absent: a caller can go on without a size.
std::optional<std::uint64_t> content_length(std::wstring_view url, std::uint32_t timeout_seconds,
    const wchar_t* agent, Watch* watch = nullptr);
}
