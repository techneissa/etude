// SPDX-License-Identifier: GPL-3.0-or-later
// What the Configs page does to the settings, apart from drawing it (no ImGui here, so
// tests/configs_logic_test.cpp holds the rules). A config is what a saved slot holds: the look
// (Notes, Effects and View settings, with the player's own preferences that Reset all keeps).
// Every change that overwrites something returns what it replaced, and the page's Undo toast puts
// that back; another profile's settings are only ever read.
#pragma once

#include "../ui_model.hpp"
#include "profile_port.hpp"

#include "etude/look.hpp"
#include "etude/profile.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace etude::app::ui {

// What a card of an empty slot says, and the row's line when the household is one person.
inline constexpr const char* kConfigEmptyWords = "Save current";
inline constexpr const char* kConfigNoOtherProfile = "There is no other profile to copy from";

// The name a slot is saved under when it had none.
inline std::string config_default_name(int slot) { return "Config " + std::to_string(slot + 1); }

// The look a card previews: the slot's own, never the look in use; none for an empty slot.
inline std::optional<LookSettings> config_card_look(const UserPresets& presets, int slot) {
    LookSettings look;
    if (!presets.load_slot(slot, &look)) {
        return std::nullopt;
    }
    return look;
}

// The slot holds exactly the look in use.
inline bool config_is_current(const UiModel& model, int slot) {
    const std::optional<LookSettings> held = config_card_look(model.user_presets, slot);
    return held && *held == model.look;
}

// Tap on a filled card: the slot's look becomes the look in use. False for an empty slot.
inline bool config_apply(UiModel& model, int slot) {
    return model.user_presets.load_slot(slot, &model.look);
}

// Saves the look in use into `slot`, keeping a filled slot's name. Returns the slots as they were
// and the name the slot now has.
struct ConfigSaved {
    UserPresets before;
    std::string name;
};
inline ConfigSaved config_save_here(UiModel& model, int slot) {
    ConfigSaved saved{model.user_presets, {}};
    const std::string keep = model.user_presets.is_used(slot)
                                 ? std::string(model.user_presets.slot_name(slot))
                                 : config_default_name(slot);
    model.user_presets.save_slot(slot, keep, model.look);
    saved.name = std::string(model.user_presets.slot_name(slot));
    return saved;
}

// Empties `slot`. Returns the slots as they were.
inline UserPresets config_clear(UiModel& model, int slot) {
    UserPresets before = model.user_presets;
    model.user_presets.clear_slot(slot);
    return before;
}

// The dialog's name for a filled slot, its look kept. False when the slot is empty.
inline bool config_rename(UiModel& model, int slot, std::string_view name) {
    LookSettings look;
    if (!model.user_presets.load_slot(slot, &look)) {
        return false;
    }
    return model.user_presets.save_slot(slot, name, look);
}

// The Undo of a save or a clear: the slots go back, the look in use stays as it is.
inline void config_undo_slots(UiModel& model, const UserPresets& before) {
    model.user_presets = before;
}

// The other profiles, in the order of the household: how many there are to copy from.
inline int config_other_count(const ProfileInfo* info) {
    if (info == nullptr || info->set == nullptr) {
        return 0;
    }
    int count = 0;
    for (const Profile& p : info->set->all()) {
        count += p.id != info->active ? 1 : 0;
    }
    return count;
}

// The look the household holds for profile `id` (read only); none when it was not worked out.
inline const LookSettings* config_of_profile(const ProfileInfo& info, std::int64_t id) {
    for (const ProfileLook& held : info.looks) {
        if (held.id == id) {
            return &held.look;
        }
    }
    return nullptr;
}

// Copies another profile's config into the look in use. Returns the look it replaced; the profile's
// own settings are not touched (`theirs` is read only).
inline LookSettings config_copy(UiModel& model, const LookSettings& theirs) {
    LookSettings before = model.look;
    model.look = theirs.clamped();
    return before;
}

// The Undo of a copy: the look goes back, the saved configs stay as they are.
inline void config_undo_copy(UiModel& model, const LookSettings& before) { model.look = before; }

// What the copy asks before it copies: a tap on a profile in the copy row only opens the question
// (nothing is copied); Copy answers it with the profile to copy from; Cancel, a tap outside the
// card and anything that takes the page away close it and copy nothing.
class CopyAsk {
  public:
    bool open() const { return open_; }
    std::int64_t profile() const { return id_; }
    // A tap on profile `id`'s pill.
    void ask(std::int64_t id) {
        open_ = true;
        id_ = id;
    }
    // Copy: the profile to copy from, and the question closes.
    std::optional<std::int64_t> copy() {
        if (!open_) {
            return std::nullopt;
        }
        open_ = false;
        return id_;
    }
    // Cancel, a tap outside the card, the sheet closing, the page changing.
    void cancel() { open_ = false; }
    // A tap outside the card: `away` is the overlay's outside-tap test. It closes the question and
    // copies nothing; a tap that is not outside leaves it as it is.
    void outside_tap(bool away) {
        if (away) {
            cancel();
        }
    }
    // The page draws the question every frame it shows. A frame the page was not drawn on (the
    // sheet closed, another page opened) closes it: `frame` is the number of the frame being drawn.
    void page_frame(int frame) {
        if (open_ && last_frame_ >= 0 && last_frame_ < frame - 1) {
            cancel();
        }
        last_frame_ = frame;
    }

  private:
    bool open_ = false;
    std::int64_t id_ = 0;
    int last_frame_ = -10;
};

// The card's words.
inline std::string config_copy_question(std::string_view name) {
    return "Copy " + std::string(name) + "'s config?";
}
inline constexpr const char* kConfigCopyDetail =
    "It replaces your current config. You can undo it.";

// The toast after a copy: "Copied Dani's config".
inline std::string config_copied_text(std::string_view name) {
    return "Copied " + std::string(name) + "'s config";
}

// --- The cards' taps and buttons, and the toasts ----------------------------------------------

// What the page says: a plain toast, or one with an Undo that runs `undo` when its chip is tapped.
// The page gives the kit's; the tests give one that records them.
class ConfigToaster {
  public:
    virtual ~ConfigToaster() = default;
    virtual void say(const std::string& text) = 0;
    virtual void say_undoable(const std::string& text, std::function<void()> undo) = 0;
};

// The three icon buttons under a card, left to right.
enum class ConfigAction : std::uint8_t { Rename, SaveHere, Clear };
inline constexpr std::array<ConfigAction, 3> kConfigActions{
    ConfigAction::Rename, ConfigAction::SaveHere, ConfigAction::Clear};

// Whether a button does anything: Save current here always (on an empty card it is the same as a
// tap on it); Rename and Clear only on a filled one, where an empty card's are greyed.
inline bool config_action_enabled(ConfigAction action, bool used) {
    return action == ConfigAction::SaveHere || used;
}

// A tap on card `slot`: a filled card's config becomes the look in use; an empty card saves the
// look in use there, and its Undo empties it again.
inline void config_tap(UiModel& model, int slot, ConfigToaster& toast) {
    if (!model.user_presets.is_used(slot)) {
        UiModel* live = &model;
        ConfigSaved saved = config_save_here(model, slot);
        toast.say_undoable("Saved as " + saved.name, [live, before = std::move(saved.before)] {
            config_undo_slots(*live, before);
        });
        return;
    }
    config_apply(model, slot);
    toast.say("Config: " + std::string(model.user_presets.slot_name(slot)));
}

// A button under card `slot`: Save current here and Clear change the slot at once and their toast's
// Undo puts the slots back. True for Rename, which the page answers with its dialog. A greyed
// button (Rename or Clear on an empty card) does nothing.
inline bool config_button_act(UiModel& model, ConfigAction action, int slot, ConfigToaster& toast) {
    const bool used = model.user_presets.is_used(slot);
    if (!config_action_enabled(action, used)) {
        return false;
    }
    UiModel* live = &model;
    switch (action) {
    case ConfigAction::Rename:
        return true;
    case ConfigAction::SaveHere: {
        ConfigSaved saved = config_save_here(model, slot);
        toast.say_undoable(
            (used ? "Saved over " : "Saved as ") + saved.name,
            [live, before = std::move(saved.before)] { config_undo_slots(*live, before); });
        break;
    }
    case ConfigAction::Clear: {
        const std::string name(model.user_presets.slot_name(slot));
        UserPresets before = config_clear(model, slot);
        toast.say_undoable("Cleared " + name, [live, before = std::move(before)] {
            config_undo_slots(*live, before);
        });
        break;
    }
    }
    return false;
}

// Copy was tapped on the question for profile `id`: their config becomes the look in use and the
// toast's Undo puts yours back. False (nothing changes) for a profile whose look is not known.
inline bool config_copy_confirmed(UiModel& model, const ProfileInfo& info, std::int64_t id,
                                  ConfigToaster& toast) {
    const Profile* who = info.set != nullptr ? info.set->find(id) : nullptr;
    if (who == nullptr) {
        return false;
    }
    const LookSettings* theirs = config_of_profile(info, id);
    if (theirs == nullptr) {
        toast.say(who->name + "'s config is not ready yet");
        return false;
    }
    UiModel* live = &model;
    LookSettings before = config_copy(model, *theirs);
    toast.say_undoable(config_copied_text(who->name),
                       [live, before = std::move(before)] { config_undo_copy(*live, before); });
    return true;
}

} // namespace etude::app::ui
