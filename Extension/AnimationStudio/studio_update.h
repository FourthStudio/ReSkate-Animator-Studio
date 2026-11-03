#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// The Animator Studio looks for a newer release of itself on GitHub (FourthStudio/ReSkate-Animator-Studio, its latest
// release), says so in a corner notice in game, and in the studio offers to update, with the release's notes. UPDATE
// NOW downloads the release's ReSkate.dll (checked against the SHA-256 GitHub gives for it, kept in "<Skate
// folder>\Animator Studio updates\<version>\") and installs it: the running ReSkate.dll is renamed to
// ReSkate.dll.previous (Windows lets a loaded DLL be renamed, not overwritten) and the new one put in its place, so the
// next start runs it. RESTART NOW closes the game and starts it again through ReSkateLauncher.exe, as its Play does.
namespace dingosdk::anim_studio::update {
enum class Step { idle, checking, current, newer, failed, downloading, downloaded, download_failed, installed, install_failed, restarting };
struct State {
    Step step{Step::idle};
    std::string running;     // the version running, as players read it ("1.1.1")
    std::string latest;      // the newest release's version
    std::string title;       // its name ("Animator Studio 1.1.2 / ReSkate 2.1.3")
    std::string notes;       // what it says about itself (the release notes, Markdown)
    std::string page;        // its page on GitHub
    std::string error;       // why a check or a download failed
    std::filesystem::path saved_to; // the downloaded ReSkate.dll
    std::filesystem::path installed_to; // the ReSkate.dll it went in place of (the one the game loaded)
    std::uint64_t received{}, total{};
    bool answered{};         // the question was answered (or put away) this session
    bool can_restart{};      // ReSkateLauncher.exe is beside ReSkate.dll, so RESTART NOW can start the game again
};
State state();

// Client tick: the first call starts the check on its own thread, once the profile is there to say whether checking is
// on; when a newer version turns up a corner notice says so.
void tick() noexcept;
void check_now() noexcept;   // SETTINGS > STUDIO > Check now
void download() noexcept;    // the popup's UPDATE NOW: download, check and install (TRY AGAIN after a failure too)
void restart() noexcept;     // the popup's RESTART NOW: close the game, start it again with the new ReSkate.dll
void cancel() noexcept;      // the popup's CANCEL (asked again at the next start)
void open_folder() noexcept; // where the download went, in Explorer
// The studio's popup is up: a newer version not answered yet, or a download going, done or failed and not put away.
bool popup_wanted() noexcept;
void close_popup() noexcept;
// SETTINGS > STUDIO > Check for updates, saved with the profile (on unless switched off).
bool checking() noexcept;
void set_checking(bool on) noexcept;

// Whether version `a` is newer than `b` ("1.1.2" > "1.1.1"; a leading v and anything after a '+' or '-' left out).
bool newer(std::string_view a, std::string_view b) noexcept;
}
