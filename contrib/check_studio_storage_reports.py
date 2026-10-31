"""Run production playlist/report functions against injected storage failures, without the game."""
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[1]
source = (repo / 'Extension/AnimationStudio/anim_studio.cpp').read_text(encoding='utf-8-sig')

def function(text, signature):
    start = text.index(signature)
    at = text.index('(', start) + 1
    depth = 1
    while depth:
        depth += (text[at] == '(') - (text[at] == ')')
        at += 1
    opening = text.index('{', at)
    at, depth = opening + 1, 1
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    return text[start:at] + '\n'

types = source[source.index('struct NamedPlaylist {'):source.index('struct State {')]
fixture = r'''
#include <windows.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include "Extension/AnimationStudio/playlist_management.h"
using namespace dingosdk::anim_studio;
namespace logging { enum class Level {info, warning}; enum class Channel {skater}; template<class... T> void log(T&&...) {} }
using logging::Level; using logging::Channel;
'''
fixture += types + r'''
struct State {
    std::mutex mutex;
    Playlists playlists;
    PlaylistEditHistory playlist_history;
    std::string playlist_open, status;
    std::vector<PresetPack> packs;
    bool tricks_read{true};
};
State shared_state;
State& state() { return shared_state; }
Playlists disk;
bool fail_write{}, fail_publish{};
void write_playlists(const Playlists& lists) { if (fail_write) throw std::runtime_error("read-only playlist"); disk = lists; }
void read_library_locked(State& s) {
    s.playlists = disk;
    if (!s.playlists.named(s.playlist_open)) s.playlist_open = s.playlists.lists.empty() ? "" : s.playlists.lists.front().name;
}
void publish_locked(State&, bool) { if (fail_publish) throw std::runtime_error("notification failure"); }
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
'''
production = ''
for signature in ['bool playlists_changed_locked(State& s, const Playlists& playlists, std::string& message)',
                  'void say_locked(', 'std::string put_in_playing_list(', 'bool edit_playlist_locked(',
                  'bool undo_playlist_entries(', 'bool duplicate_playlist(', 'bool set_pack_in_list_locked(',
                  'bool set_pack_in_playlist_on(']:
    production += function(source, signature)
production += '\nstd::atomic<bool> report_being_written{false};\n' + function(source, 'bool write_off_the_game_threads(')
tests = r'''
std::atomic<bool> callback_started{}, release_callback{};
bool blocking_report(std::string& message) {
    callback_started = true;
    while (!release_callback.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    message = "report complete"; return true;
}
bool throwing_report(std::string&) { throw std::runtime_error("report disk failure"); }
bool completed_report(std::string& message) { message = "another report complete"; return true; }
void wait_for(const std::function<bool()>& ready) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!ready()) {
        if (std::chrono::steady_clock::now() > until) throw std::runtime_error("report did not finish");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
int main() { try {
    auto& s = state();
    disk.on = "Everyday";
    disk.lists = {{"Everyday", PlaylistMode::shuffle, {}}, {"Editing", PlaylistMode::shuffle, {{"", "Kickflip"}}}};
    s.playlist_open = "Editing"; read_library_locked(s);
    PresetPack pack; pack.name = "Downloaded"; s.packs.push_back(pack);
    std::string message;
    const auto add = [](auto& entries) { entries.push_back({"", "Dolphin"}); return true; };
    fail_write = true;
    check(!edit_playlist_locked(s, "Editing", message, add), "A failed Library edit must report failure");
    check(s.playlist_history.name.empty() && s.playlists.named("Editing")->entries.size() == 1 && disk.on == "Everyday",
          "A failed edit must not create Undo history or change playback/content");
    fail_write = false;
    check(edit_playlist_locked(s, "Editing", message, add), "Retrying the edit succeeds");
    fail_write = true;
    check(!undo_playlist_entries("Editing", message) && !s.playlist_history.undone && s.playlists.named("Editing")->entries.size() == 2,
          "A failed Undo must leave its history ready to retry");
    fail_write = false;
    check(undo_playlist_entries("Editing", message) && s.playlist_history.undone, "Undo retries the same change once");
    fail_write = true;
    check(!undo_playlist_entries("Editing", message) && s.playlist_history.undone, "A failed Redo must not advance history");
    check(!duplicate_playlist("Editing", message) && s.playlist_open == "Editing" && disk.lists.size() == 2,
          "A failed duplicate must not open a nonexistent playlist");
    fail_write = false;
    check(undo_playlist_entries("Editing", message) && !s.playlist_history.undone, "Redo is still available after its failed write");
    check(set_pack_in_playlist_on("Downloaded", true, message), "The trainer can add a pack to the playing playlist");
    check(playlist_has_pack(disk.named("Everyday")->entries, "Downloaded") && !playlist_has_pack(disk.named("Editing")->entries, "Downloaded") && s.playlist_open == "Editing",
          "Trainer pack switches must change the playing playlist independently of the edited playlist");
    fail_write = true;
    check(!set_pack_in_playlist_on("Downloaded", false, message) && playlist_has_pack(disk.named("Everyday")->entries, "Downloaded"),
          "A failed trainer toggle must not change the stored pack assignment");
    fail_write = false;
    check(write_off_the_game_threads(message, "saving", blocking_report), "A report starts asynchronously");
    wait_for([] { return callback_started.load(); });
    check(!write_off_the_game_threads(message, "another", completed_report), "A second report cannot overlap the first");
    release_callback = true;
    wait_for([] { return !report_being_written.load(std::memory_order_acquire); });
    check(s.status == "report complete", "Completion publishes the final report status");
    check(write_off_the_game_threads(message, "saving", throwing_report), "A failing report is accepted by the worker");
    wait_for([] { return !report_being_written.load(std::memory_order_acquire); });
    fail_publish = true;
    check(write_off_the_game_threads(message, "saving", completed_report), "A new report starts after a callback exception");
    wait_for([] { return !report_being_written.load(std::memory_order_acquire); });
    fail_publish = false;
    check(write_off_the_game_threads(message, "saving", completed_report), "A notification exception must also release the report gate");
    wait_for([] { return !report_being_written.load(std::memory_order_acquire); });
    check(s.status == "another report complete", "Later reports remain functional");
    std::cout << "Production storage/report checks: failed writes, Undo/Redo, destination isolation and asynchronous completion passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
'''
out = repo / 'build/storage-report-check'
out.mkdir(parents=True, exist_ok=True)
cpp = out / 'check.cpp'
cpp.write_text(fixture + production + tests, encoding='utf-8')
result = subprocess.run(['cl', '/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/DNOMINMAX', f'/I{repo}',
                         str(cpp), f'/Fe:{out / "check.exe"}', f'/Fo:{out / "check.obj"}'], cwd=out, capture_output=True, text=True)
print(result.stdout, result.stderr)
if result.returncode:
    sys.exit(result.returncode)
sys.exit(subprocess.run([str(out / 'check.exe')], cwd=out).returncode)
