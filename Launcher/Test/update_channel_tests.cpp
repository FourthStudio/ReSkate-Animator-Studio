#include "Launcher/updater.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace dingosdk;

namespace {

void check(bool condition, const char* what) {
    if (condition) return;
    std::cerr << "FAILED: " << what << '\n';
    std::exit(1);
}

} // namespace

void choice_regressions() {
    using launcher_update::UpdateChoices;
    const UpdateChoices own{true, false, false}, official{true, false, true}, preview{true, true, false}, off{false, false, false};
    check(launcher_update::can_apply_update(own, own, true, true), "unchanged choices can install their own result");
    check(!launcher_update::can_apply_update(own, official, true, true), "choosing official cannot install a stale Studio result");
    check(!launcher_update::can_apply_update(official, own, true, true), "staying on Studio cannot install a stale official result");
    check(!launcher_update::can_apply_update(preview, own, true, true), "turning pre-releases off invalidates the previous result");
    check(!launcher_update::can_apply_update(own, off, true, true) && !launcher_update::can_apply_update(off, off, true, true),
          "updates disabled never install binaries");
    check(!launcher_update::can_apply_update(own, own, false, true) && !launcher_update::can_apply_update(own, own, true, false),
          "the build's update protection and --no-update cannot be bypassed by a stale button");
}

// --live: what each channel's releases on GitHub offer now (needs the network, so not in CTest).
int live() {
    using launcher_update::Channel;
    for (const auto channel : {Channel::own, Channel::official}) {
        const auto repo = launcher_update::release_repo(channel);
        std::wcout << (channel == Channel::own ? L"own      " : L"official ") << repo << L": ";
        const auto config = launcher_update::fetch_config(false, channel);
        if (!config) {
            std::wcout << L"no usable launcher.json (see the log)\n";
            continue;
        }
        std::cout << config->tag << ", runtime " << config->runtime.version << ", game build " << config->game.build_id
                  << "\n";
    }
    return 0;
}

int main(int argc, char** argv) {
    choice_regressions();
    if (argc > 1 && std::string(argv[1]) == "--live") return live();
    // There is a choice only when this build's releases are not official ReSkate's.
    using launcher_update::Channel;
    check(launcher_update::has_official_channel() ==
              (launcher_update::release_repo(Channel::own) != launcher_update::release_repo(Channel::official)),
          "the official channel is offered only for releases of another repo");
    check(launcher_update::releases_page(Channel::official).ends_with("/releases") &&
              launcher_update::releases_page(Channel::official).starts_with("https://github.com/"),
          "the official releases page is a GitHub releases page");
    std::cout << "update channel tests passed\n";
    return 0;
}
