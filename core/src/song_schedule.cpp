// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_schedule.hpp"

#include "etude/midi_event.hpp"

namespace etude {

std::array<PlaybackMessage, kPortResetMessages> port_reset_messages() {
    std::array<PlaybackMessage, kPortResetMessages> out{};
    for (std::size_t ch = 0; ch < 16; ++ch) {
        PlaybackMessage off;
        off.kind = PlaybackMessage::Kind::ControlChange;
        off.channel = static_cast<std::uint8_t>(ch);
        off.data = kCcAllNotesOff;
        off.value = 0;
        PlaybackMessage pedal = off;
        pedal.data = kCcSustain;
        out[2 * ch] = off;
        out[2 * ch + 1] = pedal;
    }
    return out;
}

} // namespace etude
