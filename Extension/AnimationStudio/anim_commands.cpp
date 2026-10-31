#include "anim_studio.h"
#include "Extension/Console/commands.h"
#include <string>

namespace dingosdk::console {
void register_animation_studio_commands(Commands& registry) {
    // Opening it from a line.
    auto open_verb = argument("action", Type::text, false);
    open_verb.choices = {"open", "close"};
    auto open = action("animstudio", "Open or close the Animator Studio (F8)", Group::gameplay, {open_verb});
    open.run = [](const Model&, const Values& values, const Output& out) {
        const auto verb = values.empty() ? std::string("open") : std::get<std::string>(values[0]);
        const bool wanted = verb != "close";
        anim_studio::request_open(wanted);
        out(wanted ? "Opening the Animator Studio." : "Closing the Animator Studio.");
    };
    registry.add(std::move(open));
    auto dump = action("animstudio dump", "Export the last 25 gameplay seconds as local diagnostic text files", Group::gameplay, {});
    dump.run = [](const Model&, const Values&, const Output& out) {
        std::string message; anim_studio::export_diagnostics(message); out(message);
    };
    registry.add(std::move(dump));
}
} // namespace dingosdk::console
