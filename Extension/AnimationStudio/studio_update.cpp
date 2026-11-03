#include "studio_update.h"
#include "reskate_version.h"
#include "Engine/Core/Json/json.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Vfs/https_download.h"
#include "Extension/Profile/local_profile_runtime.h"
#include "Extension/UI/Overlay/overlay.h"
#include <Windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <format>
#include <functional>
#include <fstream>
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

struct Shared {
    std::mutex mutex;
    State state;
    std::string dll_url, dll_digest, sum_url; // the release's ReSkate.dll, its SHA-256 as GitHub gives it, its .sha256 file
    std::atomic<bool> loaded{}, checking{true}, dirty{}, started{}, busy{}, popup_closed{};
    https::Watch watch;
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

std::filesystem::path game_folder() {
    std::array<wchar_t, 32768> module{};
    if (!GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()))) return {};
    return std::filesystem::path(module.data()).parent_path();
}
std::filesystem::path updates_folder(const std::string& version) {
    return game_folder() / L"Animator Studio updates" / std::filesystem::path(version);
}

// The file's SHA-256 as lower-case hex, empty when it cannot be read.
std::string sha256_of(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    BCRYPT_HASH_HANDLE hash{};
    if (!BCRYPT_SUCCESS(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0))) return {};
    std::vector<char> chunk(1 << 20);
    bool ok = true;
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto got = static_cast<ULONG>(in.gcount());
        if (got && !BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(chunk.data()), got, 0))) { ok = false; break; }
    }
    std::array<unsigned char, 32> digest{};
    ok = ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
    BCryptDestroyHash(hash);
    if (!ok) return {};
    std::string hex;
    for (const auto byte : digest) hex += std::format("{:02x}", byte);
    return hex;
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

// The ReSkate.dll this code runs from (the one the launcher put in the game: its own path, whatever the folder).
std::filesystem::path own_dll() {
    static const int marker = 0;
    HMODULE self{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&marker), &self))
        return {};
    std::array<wchar_t, 32768> path{};
    const DWORD n = GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) return {};
    return std::filesystem::path(path.data());
}
std::filesystem::path launcher_beside(const std::filesystem::path& dll) {
    std::error_code error;
    const auto launcher = dll.parent_path() / L"ReSkateLauncher.exe";
    return !dll.empty() && std::filesystem::is_regular_file(launcher, error) ? launcher : std::filesystem::path{};
}

// Puts the downloaded ReSkate.dll in place of the one the game loaded. Windows lets a loaded DLL be renamed but never
// overwritten or deleted, so: the new one is copied in beside it first (same folder, so the swap is two renames) and
// read back against its SHA-256, the running one is renamed to ReSkate.dll.previous (the one before is replaced: kept
// to go back to), and the new one renamed into its place. Anything failing puts the old one back.
bool install(const std::filesystem::path& downloaded, const std::string& digest, std::filesystem::path& current, std::string& why) {
    current = own_dll();
    if (current.empty()) {
        why = "the running ReSkate.dll could not be found";
        return false;
    }
    const auto folder = current.parent_path();
    const auto previous = folder / (current.filename().wstring() + L".previous");
    const auto incoming = folder / (current.filename().wstring() + L".incoming");
    std::error_code error;
    std::filesystem::remove(incoming, error);
    if (!CopyFileW(downloaded.c_str(), incoming.c_str(), FALSE)) {
        why = std::format("it could not be copied into {} (Windows error {}). Is the folder read-only?", folder.string(), GetLastError());
        return false;
    }
    if (!digest.empty() && sha256_of(incoming) != digest) {
        std::filesystem::remove(incoming, error);
        why = "the copy did not match its SHA-256, so nothing was changed";
        return false;
    }
    if (!MoveFileExW(current.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const auto code = GetLastError();
        std::filesystem::remove(incoming, error);
        why = std::format("the running ReSkate.dll could not be moved aside (Windows error {})", code);
        return false;
    }
    if (!MoveFileExW(incoming.c_str(), current.c_str(), 0)) {
        const auto code = GetLastError();
        MoveFileExW(previous.c_str(), current.c_str(), 0); // the old one back where it was
        std::filesystem::remove(incoming, error);
        why = std::format("the new ReSkate.dll could not be put in place (Windows error {}); the old one is back", code);
        return false;
    }
    return true;
}

void set(const std::function<void(State&)>& change) {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    change(s.state);
}

void run_check() {
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
                }
                if (name == "ReSkate.dll.sha256") sum_url = url;
            }
        }
        const bool is_newer = !tag.empty() && newer(tag, running_version_of_build()) && !dll_url.empty();
        {
            std::lock_guard lock(s.mutex);
            s.dll_url = dll_url;
            s.dll_digest = digest;
            s.sum_url = sum_url;
            auto& st = s.state;
            st.latest = tag;
            st.title = field("name");
            st.notes = field("body");
            st.page = field("html_url");
            st.step = is_newer ? Step::newer : Step::current;
        }
        logging::log(Level::info, Channel::runtime, "Animator Studio update check: running {}, newest release {}{}", running_version_of_build(), tag,
                     is_newer ? " (newer)" : "");
        if (is_newer)
            overlay::notify(overlay::NoticeLevel::info, "Animator Studio " + tag + " is out",
                            "You have " + running_version_of_build() + ". Open the studio (F8) to see what is new and update in one click.");
    } catch (...) {
        set([](State& st) { st.step = Step::failed; st.error = "GitHub's answer could not be read"; });
    }
}

void run_download() {
    auto& s = shared();
    std::string url, digest, sum_url, version;
    {
        std::lock_guard lock(s.mutex);
        url = s.dll_url;
        digest = s.dll_digest;
        sum_url = s.sum_url;
        version = s.state.latest;
        s.state.step = Step::downloading;
        s.state.error.clear();
        s.state.received = s.state.total = 0;
    }
    s.popup_closed.store(false);
    const auto fail = [&](const std::string& why) {
        set([&](State& st) { st.step = Step::download_failed; st.error = why; });
        logging::log(Level::warning, Channel::runtime, "Animator Studio update download: {}", why);
    };
    // The SHA-256 to check it against: GitHub's own, else the release's .sha256 file.
    if (digest.empty() && !sum_url.empty())
        if (const auto text = https::get_text(wide(sum_url), 4096, 20, agent)) digest = lower_hex(*text);
    std::error_code error;
    const auto folder = updates_folder(version);
    std::filesystem::create_directories(folder, error);
    const auto final_path = folder / L"ReSkate.dll", part = folder / L"ReSkate.dll.part";
    // Installed once downloaded and checked: the game runs it from its next start.
    const auto put_in_place = [&] {
        std::filesystem::path current;
        std::string why;
        if (!install(final_path, digest, current, why)) {
            set([&](State& st) { st.step = Step::install_failed; st.error = why; st.saved_to = final_path; });
            logging::log(Level::warning, Channel::runtime, "Animator Studio {} not installed: {}", version, why);
            return;
        }
        const bool restartable = !launcher_beside(current).empty();
        set([&](State& st) {
            st.step = Step::installed;
            st.saved_to = final_path;
            st.installed_to = current;
            st.can_restart = restartable;
        });
        logging::log(Level::info, Channel::runtime, "Animator Studio {} installed in place of {} (the old one is ReSkate.dll.previous)", version,
                     current.string());
    };
    // Downloaded before and whole: nothing to fetch.
    if (!digest.empty() && std::filesystem::exists(final_path, error) && sha256_of(final_path) == digest) {
        put_in_place();
        return;
    }
    std::filesystem::remove(part, error);
    s.watch.cancel.store(false);
    s.watch.received.store(0);
    s.watch.total.store(0);
    s.watch.deadline_us.store(0);
    const auto got = https::get(wide(url), part, most_dll_bytes, 900, agent, &s.watch);
    if (!got.ok) {
        std::filesystem::remove(part, error);
        return fail(got.http_status ? std::format("GitHub answered {}", got.http_status) : std::format("the download stopped ({})", got.error));
    }
    if (!digest.empty() && sha256_of(part) != digest) {
        std::filesystem::remove(part, error);
        return fail("the download did not match its SHA-256, so it was thrown away. Try again.");
    }
    std::filesystem::remove(final_path, error);
    std::filesystem::rename(part, final_path, error);
    if (error) return fail("it could not be saved in " + folder.string());
    set([&](State& st) { st.step = Step::downloaded; st.saved_to = final_path; });
    logging::log(Level::info, Channel::runtime, "Animator Studio {} downloaded to {}{}", version, final_path.string(),
                 digest.empty() ? " (no SHA-256 to check it against)" : ", SHA-256 checked");
    put_in_place();
}

void start_thread(void (*work)()) {
    auto& s = shared();
    if (s.busy.exchange(true)) return;
    std::thread([work] {
        try { work(); } catch (...) {}
        shared().busy.store(false);
    }).detach();
}
}

State state() {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    auto copy = s.state;
    if (copy.step == Step::downloading) {
        copy.received = s.watch.received.load();
        copy.total = s.watch.total.load();
    }
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
        const auto launcher = launcher_beside(st.installed_to);
        if (launcher.empty()) return;
        const auto folder = launcher.parent_path();
        // A hidden PowerShell (no window: CREATE_NO_WINDOW) waits for this game to close, then starts ReSkateLauncher.exe
        // as its Play does (--no-gui, the saved launch options) without its own update check (--no-update: that would put
        // base ReSkate's DLL back over this one). The launcher's folder has plain letters only (it will not run anywhere
        // else), and a quote in it is doubled for PowerShell.
        std::array<wchar_t, MAX_PATH> system{};
        const UINT n = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
        if (!n || n >= system.size()) return;
        const std::wstring shell = std::wstring(system.data()) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        const auto quoted = [](std::wstring text) {
            std::wstring out = L"'";
            for (const wchar_t c : text) out += c == L'\'' ? std::wstring(L"''") : std::wstring(1, c);
            return out + L"'";
        };
        const std::wstring script = L"Wait-Process -Id " + std::to_wstring(GetCurrentProcessId()) +
                                    L" -ErrorAction SilentlyContinue; Start-Sleep -Seconds 2; Start-Process -FilePath " + quoted(launcher.wstring()) +
                                    L" -ArgumentList '--no-gui','--no-update' -WorkingDirectory " + quoted(folder.wstring());
        std::wstring command = L"\"" + shell + L"\" -NoProfile -NonInteractive -WindowStyle Hidden -Command \"" + script + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        // Outside any job the game is in when it may be (so closing the game does not end it), else as it is.
        BOOL started = CreateProcessW(shell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB,
                                      nullptr, folder.c_str(), &startup, &process);
        if (!started)
            started = CreateProcessW(shell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, folder.c_str(), &startup, &process);
        if (!started) {
            const auto code = GetLastError();
            set([&](State& s) { s.error = std::format("the game could not be restarted (Windows error {}); close it and start it as usual", code); });
            return;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        set([](State& s) { s.step = Step::restarting; });
        logging::log(Level::info, Channel::runtime, "Animator Studio update: closing the game to start it again with the new ReSkate.dll");
        logging::flush();
        // Then the game is asked to close as its own window's close button would: every top-level window of this process
        // gets WM_CLOSE (it saves and closes as it always does).
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
bool popup_wanted() noexcept {
    const auto st = state();
    if (st.step == Step::newer) return !st.answered;
    if (st.step == Step::downloading || st.step == Step::downloaded || st.step == Step::download_failed || st.step == Step::installed ||
        st.step == Step::install_failed || st.step == Step::restarting)
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
