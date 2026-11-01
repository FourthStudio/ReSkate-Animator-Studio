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

std::string running_version() {
    const std::string full(reskate_mod_version);
    return full.substr(0, full.find('+'));
}
// Only the Animator Studio's own builds ask its releases (a ReSkate release or another mod has its own).
bool ours() noexcept { return reskate_modified_build && reskate_mod_id == "AnimatorStudio" && !running_version().empty(); }

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
        st.running = running_version();
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
        const bool is_newer = !tag.empty() && newer(tag, running_version()) && !dll_url.empty();
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
        logging::log(Level::info, Channel::runtime, "Animator Studio update check: running {}, newest release {}{}", running_version(), tag,
                     is_newer ? " (newer)" : "");
        if (is_newer)
            overlay::notify(overlay::NoticeLevel::info, "Animator Studio " + tag + " is out",
                            "You have " + running_version() + ". Open the studio (F8) to see what is new and download it.");
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
    // Downloaded before and whole: nothing to fetch.
    if (!digest.empty() && std::filesystem::exists(final_path, error) && sha256_of(final_path) == digest) {
        set([&](State& st) { st.step = Step::downloaded; st.saved_to = final_path; });
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
    if (copy.running.empty()) copy.running = running_version();
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
void open_folder() noexcept {
    const auto st = state();
    if (st.saved_to.empty()) return;
    const std::wstring arguments = L"/select,\"" + st.saved_to.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}
bool popup_wanted() noexcept {
    const auto st = state();
    if (st.step == Step::newer) return !st.answered;
    if (st.step == Step::downloading || st.step == Step::downloaded || st.step == Step::download_failed) return !shared().popup_closed.load();
    return false;
}
void close_popup() noexcept {
    set([](State& st) { st.answered = true; });
    shared().popup_closed.store(true);
}
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
