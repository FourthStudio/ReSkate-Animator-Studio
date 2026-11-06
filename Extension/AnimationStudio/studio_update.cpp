#include "studio_update.h"
#include "studio_update_files.h"
#include "anim_studio.h"
#include "reskate_version.h"
#include "Engine/Core/Json/json.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Platform/launcher_support.h"
#include "Engine/Vfs/https_download.h"
#include "Extension/Profile/local_profile_runtime.h"
#include "Extension/UI/Overlay/overlay.h"
#include <Windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <format>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace dingosdk::anim_studio::update {
namespace {
using logging::Channel;
using logging::Level;
constexpr const wchar_t* agent = L"ReSkate Animator Studio";
// The studio's own releases: the newest that is not a pre-release.
constexpr std::wstring_view latest_url = L"https://api.github.com/repos/FourthStudio/ReSkate-Animator-Studio/releases/latest";
constexpr const char* saved_checking = "AnimatorStudio.CheckUpdates";
constexpr std::uint64_t most_dll_bytes = 256ull * 1024 * 1024;
// While the game stays open it looks again: every hour (a quarter of an hour after a look that failed), and on opening the
// studio when the last look is over ten minutes old. Looked for only at the start, players who kept the game open all
// evening heard of an update at their next start (2.0.1 to 2.0.3 came out within hours, 2026-11-05). GitHub answers 60
// unsigned questions an hour from one address: this asks at most 7.
constexpr std::chrono::minutes look_again_every{60}, look_again_after_failing{15}, look_again_on_opening{10};

struct Shared {
    std::mutex mutex;
    State state;
    std::string dll_url, dll_digest, sum_url; // the release's ReSkate.dll, its SHA-256 as GitHub gives it, its .sha256 file
    std::uint64_t dll_size{};                 // its size as GitHub gives it, 0 not given
    std::chrono::steady_clock::time_point restart_asked;
    std::atomic<bool> loaded{}, checking{true}, dirty{}, started{}, busy{}, popup_closed{};
    https::Watch watch;
    std::chrono::steady_clock::time_point looked_at{}; // when the last look ended (guarded by mutex)
    bool looked{}, open_before{};                     // a look has ended; the studio was open at the last tick
    std::string said;                                 // the version the corner notice last told of: told once
};
Shared& shared() {
    static auto* value = new Shared;
    return *value;
}

std::string running_version_of_build() {
    const std::string full(reskate_mod_version);
    return full.substr(0, full.find('+'));
}
// Only the Animator Studio's own builds ask its releases (a ReSkate release or another mod has its own).
bool ours() noexcept { return reskate_modified_build && reskate_mod_id == "AnimatorStudio" && !running_version_of_build().empty(); }

std::vector<int> numbers(std::string_view v) {
    while (!v.empty() && (v.front() == 'v' || v.front() == 'V')) v.remove_prefix(1);
    v = v.substr(0, std::min(v.find('+'), v.find('-')));
    std::vector<int> out;
    int at = 0;
    bool any = false;
    for (const char c : v) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            at = std::min(at * 10 + (c - '0'), 1'000'000);
            any = true;
        } else if (c == '.') {
            out.push_back(at);
            at = 0;
            any = false;
        } else break;
    }
    if (any || !out.empty()) out.push_back(at);
    return out;
}

std::string lower_hex(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (std::isxdigit(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (out.size() == 64) break;
        else if (!out.empty()) out.clear();
        if (out.size() == 64) break;
    }
    return out.size() == 64 ? out : std::string{};
}
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}
std::uint64_t now_us() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// The ReSkate.dll this code runs from: the one the launcher put in the game, by the path it was loaded from, whatever its
// folder (the launcher's own, a dev build's, one reached through a junction) and whatever it is called.
std::filesystem::path own_dll() {
    static const int marker = 0;
    HMODULE self{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&marker), &self))
        return {};
    std::array<wchar_t, 32768> path{};
    const DWORD n = GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) return {};
    return std::filesystem::path(std::wstring(path.data(), n));
}
// Where an update is downloaded to: "Animator Studio updates\<version>" beside ReSkate.dll, or - when that folder takes no
// files (read-only, protected) - the player's own %LOCALAPPDATA%\ReSkate, so OPEN FOLDER still has it to copy by hand.
std::filesystem::path updates_folder(const std::filesystem::path& dll, std::string version) {
    for (auto& c : version)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '+' && c != '_') c = '_';
    if (version.empty()) version = "latest";
    std::error_code error;
    if (!dll.empty()) {
        const auto beside = dll.parent_path() / L"Animator Studio updates" / std::filesystem::path(version);
        std::filesystem::create_directories(beside, error);
        if (!error && files::writable(beside)) return beside;
    }
    std::array<wchar_t, 32768> local{};
    const auto n = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    const auto root = n && n < local.size() ? std::filesystem::path(std::wstring(local.data(), n)) : std::filesystem::temp_directory_path(error);
    const auto own = root / L"ReSkate" / L"Animator Studio updates" / std::filesystem::path(version);
    std::filesystem::create_directories(own, error);
    return own;
}
// An x64 DLL with ReSkate's initializer: the release's asset is a ReSkate runtime, not some other file under its name.
bool reskate_runtime(const std::filesystem::path& dll, std::string& why) {
    try {
        const auto info = launcher::inspect_pe_file(dll);
        if (!info.pe64 || info.machine != IMAGE_FILE_MACHINE_AMD64 || !(info.characteristics & IMAGE_FILE_DLL)) {
            why = "it is not an x64 Windows DLL";
            return false;
        }
        if (launcher::exported_function_rva(dll, "DingoSDKDebugInitialize") >= info.image_size) {
            why = "it is not a ReSkate runtime";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        why = std::string("it is not a ReSkate runtime (") + e.what() + ")";
        return false;
    }
}
// Why a download failed, in words.
std::string download_failure(const https::Download& got) {
    std::string why;
    if (got.http_status && got.http_status != 200 && got.http_status != 206) why = std::format("GitHub answered {}", got.http_status);
    else
        switch (got.error) {
        case ERROR_WINHTTP_TIMEOUT: why = "the connection stopped sending (nothing came for a minute)"; break;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        case ERROR_WINHTTP_CANNOT_CONNECT: why = "GitHub could not be reached (no connection?)"; break;
        case ERROR_WINHTTP_CONNECTION_ERROR:
        case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: why = "the connection kept dropping"; break;
        case ERROR_WINHTTP_SECURE_FAILURE: why = "a secure connection could not be made (a proxy, antivirus or the PC's clock?)"; break;
        case ERROR_INVALID_DATA: why = "it did not match its SHA-256 twice, so it was thrown away"; break;
        case ERROR_BAD_LENGTH: why = "it arrived another size than GitHub said"; break;
        case ERROR_FILE_TOO_LARGE: why = "it is far bigger than a ReSkate.dll"; break;
        case ERROR_CANCELLED: why = "it was stopped"; break;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL:
        case ERROR_WRITE_FAULT: why = "it could not be written to the disk (is it full?)"; break;
        case ERROR_ACCESS_DENIED: why = "its folder could not be written to"; break;
        default: why = std::format("Windows error {}", got.error); break;
        }
    if (got.attempts > 1 && got.error != ERROR_CANCELLED) why += std::format(", after {} tries", got.attempts);
    return why;
}

void set(const std::function<void(State&)>& change) {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    change(s.state);
}

void run_check_once();
void run_check() {
    run_check_once();
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    s.looked_at = std::chrono::steady_clock::now();
    s.looked = true;
}
void run_check_once() {
    auto& s = shared();
    set([](State& st) {
        st.step = Step::checking;
        st.error.clear();
        st.running = running_version_of_build();
    });
    https::Download result;
    const auto text = https::get_text(latest_url, 2 * 1024 * 1024, 20, agent, &result);
    if (!text) {
        const std::string why = result.http_status ? std::format("GitHub answered {}", result.http_status)
                                                   : std::format("no connection to GitHub ({})", result.error);
        set([&](State& st) { st.step = Step::failed; st.error = why; });
        logging::log(Level::info, Channel::runtime, "Animator Studio update check: {}", why);
        return;
    }
    try {
        const auto j = Json::parse(*text);
        const auto field = [&](const char* key) { return j.contains(key) && j[key].is_string() ? j[key].string() : std::string{}; };
        const auto tag = field("tag_name");
        std::string dll_url, digest, sum_url;
        std::uint64_t size = 0;
        if (j.contains("assets") && j["assets"].is_array()) {
            const auto& assets = j["assets"];
            for (std::size_t i = 0; i < assets.size(); ++i) {
                const auto& a = assets[i];
                const auto name = a.contains("name") && a["name"].is_string() ? a["name"].string() : std::string{};
                const auto url = a.contains("browser_download_url") && a["browser_download_url"].is_string() ? a["browser_download_url"].string()
                                                                                                             : std::string{};
                if (name == "ReSkate.dll") {
                    dll_url = url;
                    if (a.contains("digest") && a["digest"].is_string()) digest = lower_hex(a["digest"].string());
                    try {
                        const auto bytes = a.value("size", 0.0);
                        if (bytes > 0 && bytes < static_cast<double>(most_dll_bytes)) size = static_cast<std::uint64_t>(bytes);
                    } catch (...) {}
                }
                if (name == "ReSkate.dll.sha256") sum_url = url;
            }
        }
        const bool is_newer = !tag.empty() && newer(tag, running_version_of_build()) && !dll_url.empty();
        // Whether it can go in where ReSkate runs from, said with the offer rather than after the download.
        const auto dll = own_dll();
        const bool writable = dll.empty() || files::writable(dll.parent_path());
        {
            std::lock_guard lock(s.mutex);
            s.dll_url = dll_url;
            s.dll_digest = digest;
            s.sum_url = sum_url;
            s.dll_size = size;
            auto& st = s.state;
            st.latest = tag;
            st.title = field("name");
            st.notes = field("body");
            st.page = field("html_url");
            st.installed_to = dll;
            st.folder_writable = writable;
            st.step = is_newer ? Step::newer : Step::current;
        }
        logging::log(Level::info, Channel::runtime, "Animator Studio update check: running {}, newest release {}{}", running_version_of_build(), tag,
                     is_newer ? " (newer)" : "");
        bool tell = false;
        if (is_newer) {
            std::lock_guard lock(s.mutex);
            tell = s.said != tag;
            s.said = tag;
        }
        if (tell)
            overlay::notify(overlay::NoticeLevel::info, "Animator Studio " + tag + " is out",
                            "You have " + running_version_of_build() + ". Open the studio (F8) to see what is new and update in one click.");
    } catch (...) {
        set([](State& st) { st.step = Step::failed; st.error = "GitHub's answer could not be read"; });
    }
}

void run_download() {
    auto& s = shared();
    std::string url, digest, sum_url, version;
    std::uint64_t size{};
    {
        std::lock_guard lock(s.mutex);
        url = s.dll_url;
        digest = s.dll_digest;
        sum_url = s.sum_url;
        size = s.dll_size;
        version = s.state.latest;
        s.state.step = Step::downloading;
        s.state.error.clear();
        s.state.failed_at = -1;
        s.state.received = s.state.total = 0;
        s.state.checked = false;
    }
    s.popup_closed.store(false);
    const auto fail = [&](Step step, int at, const std::string& why) {
        set([&](State& st) {
            st.step = step;
            st.failed_at = at;
            st.error = why;
            // A download that failed its check is thrown away: nothing for OPEN FOLDER to show.
            if (at < 2) st.saved_to.clear();
        });
        logging::log(Level::warning, Channel::runtime, "Animator Studio update {}: {}", version, why);
    };
    const auto current = own_dll();
    if (current.empty()) return fail(Step::install_failed, 2, "the running ReSkate.dll could not be found");
    // The SHA-256 to check it against: GitHub's own, else the release's .sha256 file.
    if (digest.empty() && !sum_url.empty())
        if (const auto text = https::get_text(wide(sum_url), 4096, 20, agent)) digest = lower_hex(*text);
    // Without a SHA-256 nothing shows the file is the one the release published, and it replaces the DLL the game loads.
    if (digest.empty())
        return fail(Step::download_failed, 1,
                    "GitHub gave no SHA-256 for this release's ReSkate.dll, and its .sha256 file could not be read, so it was not downloaded. "
                    "TRY AGAIN reads the .sha256 file again, or get it from the releases page");
    const auto folder = updates_folder(current, version);
    const auto final_path = folder / L"ReSkate.dll";
    // 1. Downloading: patiently (a dropped or stalled connection goes on from where it got to), checked against its SHA-256
    // before it takes its name; downloaded before and whole, nothing to fetch.
    std::error_code error;
    if (!std::filesystem::exists(final_path, error) || files::sha256_of(final_path) != digest) {
        s.watch.cancel.store(false);
        s.watch.received.store(0);
        s.watch.total.store(size);
        s.watch.deadline_us.store(0);
        s.watch.retry_at_us.store(0);
        https::Fetch options;
        options.max_bytes = most_dll_bytes;
        options.expected_bytes = size;
        options.stall_ms = 60000;
        options.tries = 8;
        options.keep_partial = true; // TRY AGAIN goes on from what came
        options.verify = [&digest](const std::filesystem::path& file) { return files::sha256_of(file) == digest; };
        const auto got = https::fetch(wide(url), final_path, options, agent, &s.watch);
        if (!got.ok) return fail(Step::download_failed, got.error == ERROR_INVALID_DATA ? 1 : 0, download_failure(got));
        logging::log(Level::info, Channel::runtime, "Animator Studio {} downloaded to {} ({} request{}{})", version, files::path_text(final_path),
                     got.attempts, got.attempts == 1 ? "" : "s", got.resumed ? ", resumed" : "");
    }
    // 2. Verifying: GitHub's SHA-256 (read again from the disk), and that it is a ReSkate runtime at all.
    set([&](State& st) {
        st.step = Step::verifying;
        st.saved_to = final_path;
    });
    if (files::sha256_of(final_path) != digest) {
        std::filesystem::remove(final_path, error);
        return fail(Step::install_failed, 1, "the download did not match its SHA-256, so it was thrown away. TRY AGAIN downloads it again");
    }
    if (std::string why; !reskate_runtime(final_path, why)) {
        std::filesystem::remove(final_path, error);
        return fail(Step::install_failed, 1, "the download is not a ReSkate.dll (" + why + "), so it was thrown away");
    }
    // 3. Installing: into the folder ReSkate runs from, the old one kept as ReSkate.dll.previous.
    set([&](State& st) { st.step = Step::installing; st.installed_to = current; });
    const auto swap = files::install_dll(final_path, current, digest);
    if (!swap.ok) return fail(Step::install_failed, 2, swap.why);
    const bool restartable = !files::launcher_for(current).empty();
    set([&](State& st) {
        st.step = Step::installed;
        st.saved_to = final_path;
        st.installed_to = current;
        st.can_restart = restartable;
        st.checked = true;
    });
    logging::log(Level::info, Channel::runtime, "Animator Studio {} installed in place of {}{} ({})", version, files::path_text(current),
                 swap.earlier_update ? " (over the update installed earlier this session)" : ", the old one kept as ReSkate.dll.previous",
                 "SHA-256 checked");
}

void start_thread(void (*work)()) {
    auto& s = shared();
    if (s.busy.exchange(true)) return;
    try {
        std::thread([work] {
            try { work(); } catch (...) {}
            shared().busy.store(false);
        }).detach();
    } catch (...) {
        s.busy.store(false);
    }
}
}

State state() {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    auto copy = s.state;
    if (copy.step == Step::downloading) {
        copy.received = s.watch.received.load();
        copy.total = s.watch.total.load();
        copy.attempt = s.watch.attempt.load();
        const auto retry = s.watch.retry_at_us.load(), now = now_us();
        copy.retry_in = retry > now ? static_cast<std::uint32_t>((retry - now + 999999) / 1000000) : 0;
    }
    if (copy.step == Step::restarting)
        copy.restarting_for = std::chrono::duration<float>(std::chrono::steady_clock::now() - s.restart_asked).count();
    if (copy.running.empty()) copy.running = running_version_of_build();
    return copy;
}

void tick() noexcept {
    if (!ours()) return;
    auto& s = shared();
    try {
        if (!s.loaded.load() && profile_runtime::local_profile_store_ready()) {
            if (const auto on = profile_runtime::local_preference(saved_checking)) s.checking.store(*on);
            s.loaded.store(true);
        }
        if (s.loaded.load() && s.dirty.exchange(false)) profile_runtime::set_local_preference(saved_checking, s.checking.load());
        if (s.loaded.load() && s.checking.load() && !s.started.exchange(true)) start_thread(&run_check);
        // Again while the game stays open (look_again_every): only from a look that found nothing newer or failed - one that
        // found an update has offered it, and the download and install steps keep their own state.
        const bool open = is_open();
        bool again = false;
        {
            std::lock_guard lock(s.mutex);
            const bool opened = open && !s.open_before;
            s.open_before = open;
            if (s.looked && s.loaded.load() && s.checking.load() && !s.busy.load() &&
                (s.state.step == Step::current || s.state.step == Step::failed)) {
                const auto since = std::chrono::steady_clock::now() - s.looked_at;
                again = since >= (s.state.step == Step::failed ? look_again_after_failing : look_again_every) ||
                        (opened && since >= look_again_on_opening);
            }
        }
        if (again) start_thread(&run_check);
    } catch (...) {}
}
void check_now() noexcept {
    if (!ours()) return;
    set([](State& st) { st.answered = false; });
    shared().started.store(true);
    start_thread(&run_check);
}
void download() noexcept {
    {
        auto& s = shared();
        std::lock_guard lock(s.mutex);
        if (s.dll_url.empty()) return;
        s.state.answered = true;
    }
    start_thread(&run_download);
}
void cancel() noexcept {
    shared().watch.cancel.store(true);
    set([](State& st) { st.answered = true; });
    shared().popup_closed.store(true);
}
void restart() noexcept {
    try {
        const auto st = state();
        if (st.step != Step::installed || !st.can_restart) return;
        const auto launcher = files::launcher_for(st.installed_to);
        if (launcher.empty()) return;
        if (const auto code = files::start_hidden(files::restart_script(GetCurrentProcessId(), launcher), launcher.parent_path())) {
            set([&](State& s) { s.error = std::format("the game could not be restarted (Windows error {}); close it and start it as usual", code); });
            return;
        }
        {
            auto& s = shared();
            std::lock_guard lock(s.mutex);
            s.state.step = Step::restarting;
            s.state.error.clear();
            s.restart_asked = std::chrono::steady_clock::now();
        }
        logging::log(Level::info, Channel::runtime, "Animator Studio update: closing the game to start it again with the new ReSkate.dll ({})",
                     files::path_text(launcher));
        logging::flush();
        // Then the game is asked to close as its own window's close button would: every top-level window of this process
        // gets WM_CLOSE (it saves and closes as it always does; when it asks first, the player answers).
        EnumWindows(
            [](HWND window, LPARAM) -> BOOL {
                DWORD owner = 0;
                GetWindowThreadProcessId(window, &owner);
                if (owner == GetCurrentProcessId() && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) PostMessageW(window, WM_CLOSE, 0, 0);
                return TRUE;
            },
            0);
    } catch (...) {}
}
void open_folder() noexcept {
    const auto st = state();
    if (st.saved_to.empty()) return;
    const std::wstring arguments = L"/select,\"" + st.saved_to.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}
void open_previous() noexcept {
    try {
        const auto dll = own_dll();
        if (dll.empty()) return;
        auto previous = dll;
        previous += L".previous";
        std::error_code error;
        const auto& chosen = std::filesystem::is_regular_file(previous, error) ? previous : dll;
        const std::wstring arguments = L"/select,\"" + chosen.wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
    } catch (...) {}
}
bool popup_wanted() noexcept {
    const auto st = state();
    if (st.step == Step::newer) return !st.answered;
    if (st.step == Step::downloading || st.step == Step::verifying || st.step == Step::installing || st.step == Step::download_failed ||
        st.step == Step::installed || st.step == Step::install_failed || st.step == Step::restarting)
        return !shared().popup_closed.load();
    return false;
}
void close_popup() noexcept {
    set([](State& st) { st.answered = true; });
    shared().popup_closed.store(true);
}
void show_popup() noexcept {
    set([](State& st) { st.answered = false; });
    shared().popup_closed.store(false);
}
int install_step(const State& st, bool& failed) noexcept {
    failed = st.step == Step::download_failed || st.step == Step::install_failed;
    switch (st.step) {
    case Step::downloading: return 0;
    case Step::verifying: return 1;
    case Step::installing: return 2;
    case Step::installed:
    case Step::restarting: return 3;
    case Step::download_failed:
    case Step::install_failed: return std::clamp(st.failed_at, 0, 2);
    default: return -1;
    }
}
std::string running_version() noexcept {
    try {
        return ours() ? running_version_of_build() : std::string{};
    } catch (...) {
        return {};
    }
}
std::string reskate_base() noexcept {
    try {
        // "1.1.3+reskate.2.1.3": the ReSkate it is built on is after "reskate.".
        const std::string full(reskate_mod_version);
        const auto at = full.find("reskate.");
        return at == std::string::npos ? std::string{} : full.substr(at + 8);
    } catch (...) {
        return {};
    }
}
bool available() noexcept { return ours(); }
bool checking() noexcept { return shared().checking.load(); }
void set_checking(bool on) noexcept {
    shared().checking.store(on);
    shared().dirty.store(true);
}

bool newer(std::string_view a, std::string_view b) noexcept {
    try {
        const auto x = numbers(a), y = numbers(b);
        if (x.empty() || y.empty()) return false;
        for (std::size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
            const int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
            if (p != q) return p > q;
        }
        return false;
    } catch (...) {
        return false;
    }
}
}
