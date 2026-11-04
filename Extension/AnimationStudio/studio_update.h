#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// The Animator Studio looks for a newer release of itself on GitHub (FourthStudio/ReSkate-Animator-Studio, its latest
// release), says so in a corner notice in game, and in the studio offers to update, with the release's notes. UPDATE
// NOW is an installer in steps (the studio's card and the ReSkate menu's UPDATES card show them):
// - downloading: the release's ReSkate.dll, resumed after a dropped or stalled connection (https::fetch), into "Animator
//   Studio updates\<version>\" beside ReSkate.dll (in %LOCALAPPDATA%\ReSkate when that folder takes no files);
// - verifying: its SHA-256 against GitHub's, and that it is an x64 ReSkate runtime;
// - installing: into the folder ReSkate runs from - the folder of the ReSkate.dll the game loaded, whatever it is called
//   and wherever it is (studio_update_files.h: the running one renamed to ReSkate.dll.previous, the new one put in its
//   place, all of it put back when a step fails);
// - installed: KEEP PLAYING (it runs from the next start) or RESTART NOW (the game closes and starts again through the
//   launcher beside it, as its Play does, with no window shown).
namespace dingosdk::anim_studio::update {
enum class Step { idle, checking, current, newer, failed, downloading, verifying, installing, download_failed, installed, install_failed, restarting };
struct State {
    Step step{Step::idle};
    std::string running;     // the version running, as players read it ("1.1.1")
    std::string latest;      // the newest release's version
    std::string title;       // its name ("Animator Studio 1.1.2 / ReSkate 2.1.3")
    std::string notes;       // what it says about itself (the release notes, Markdown)
    std::string page;        // its page on GitHub
    std::string error;       // why a check, a download or an install failed
    std::filesystem::path saved_to; // the downloaded ReSkate.dll
    std::filesystem::path installed_to; // the ReSkate.dll it goes (went) in place of: the one the game loaded
    std::uint64_t received{}, total{};
    std::uint32_t attempt{};     // downloading: the request it is on (1 the first)
    std::uint32_t retry_in{};    // downloading: seconds until it tries again after a dropped connection, 0 not waiting
    bool checked{};              // installed: its SHA-256 was GitHub's (else it had none to check against)
    bool folder_writable{true};  // the folder ReSkate runs from takes files (checked when the update is offered)
    float restarting_for{};      // restarting: seconds since RESTART NOW
    int failed_at{-1};           // download_failed, install_failed: the step that did not go (install_step's numbers)
    bool answered{};         // the question was answered (or put away) this session
    bool can_restart{};      // a launcher is beside ReSkate.dll, so RESTART NOW can start the game again
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
void close_popup() noexcept; // KEEP PLAYING / LATER / CLOSE
// The steps an update goes through, as the installer card shows them: 0 downloading, 1 verifying, 2 installing, 3 done;
// -1 before it starts. `failed` says the step it is on did not go.
int install_step(const State& st, bool& failed) noexcept;
void show_popup() noexcept;  // SETTINGS > UPDATES > What's new: the card again (when there is a newer one or one installed)
// The version as players read it ("1.1.3") and the ReSkate it is built on ("2.1.3"); empty in a build that is not the
// studio's own (no updates are looked for then).
std::string running_version() noexcept;
std::string reskate_base() noexcept;
bool available() noexcept;   // this build looks for its own updates (it is the Animator Studio's own)
// SETTINGS > STUDIO > Check for updates, saved with the profile (on unless switched off).
bool checking() noexcept;
void set_checking(bool on) noexcept;

// Whether version `a` is newer than `b` ("1.1.2" > "1.1.1"; a leading v and anything after a '+' or '-' left out).
bool newer(std::string_view a, std::string_view b) noexcept;
}
