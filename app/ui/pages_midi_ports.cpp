// SPDX-License-Identifier: GPL-3.0-or-later
// Settings > Input: the MIDI input and output rows. A row shows the current choice; a tap opens the
// list under it, Auto first and then the ports the watcher thread listed, with a check on the
// chosen one. A saved port that is not listed shows as "Not found: NAME" and, dimmed, in the list.
#include "pages.hpp"

#include "icons.hpp"
#include "item_probe.hpp"
#include "port_lists.hpp"

#include <string>

namespace etude::app::ui {

namespace {

void port_row(Kit& k, const char* icon, const char* label, const char* probe_id,
              const char* choice_probe_id, std::string& saved, bool input,
              const char* info = nullptr) {
    static const PortLists kNone;
    const PortLists& lists = k.midi_ports != nullptr ? *k.midi_ports : kNone;
    const PortChoices choices = port_choices(saved, input ? lists.inputs : lists.outputs);
    const bool missing = port_missing(saved, lists, input);
    std::string shown = "Auto";
    if (!saved.empty()) {
        shown = missing ? "Not found: " + saved : saved;
    }
    // Open or shut, kept per row by ImGui.
    ImGui::PushID(label);
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID open_id = ImGui::GetID("##open");
    bool open = store->GetBool(open_id, false);
    if (nav_row(k, icon, label, shown, info)) {
        open = !open;
    }
    probe::add_rect(probe_id, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (open) {
        for (std::size_t i = 0; i < choices.names.size(); ++i) {
            const std::string& name = choices.names[i];
            const bool gone = i > 0 && missing && name == saved;
            const bool on = static_cast<int>(i) == choices.current;
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const bool tapped = radio_row(k, i == 0 ? "Auto" : name.c_str(), nullptr, on, gone);
            probe::add_rect(choice_probe_id, at, {at.x + ImGui::GetItemRectSize().x, at.y + ImGui::GetItemRectSize().y});
            if (tapped) {
                saved = name.substr(0, kMaxNameBytes);
                k.say(std::string(label) + ": " + (saved.empty() ? "Auto" : saved));
                open = false;
            }
            ImGui::PopID();
        }
    }
    store->SetBool(open_id, open);
    ImGui::PopID();
}

} // namespace

void midi_port_rows(Kit& k, AppSettings& app) {
    catalogue_sweep::drawn(Setting::MidiInputPort);
    port_row(k, icon::kKeyboard, "MIDI input", "##sheet-midi-in", "##sheet-midi-in-choice", app.midi_in_port, true);
    catalogue_sweep::drawn(Setting::MidiOutputPort);
    port_row(k, icon::kPlug, "MIDI output", "##sheet-midi-out", "##sheet-midi-out-choice", app.midi_out_port, false,
             "Where the app sends the song's notes, so a keyboard or synth can play its sound. "
             "Auto chooses for you.");
}

} // namespace etude::app::ui
