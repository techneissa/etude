// SPDX-License-Identifier: GPL-3.0-or-later
// Sends the notes the player plays on the screen or the computer keyboard to the piano over MIDI.
#pragma once

#include "midi_ports.hpp"

#include "etude/playback.hpp"
#include "etude/song_schedule.hpp"
#include "etude/sound_sink.hpp"

#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace etude::app {

// A SoundSink that writes MIDI to the output port of the piano. Which port: the one whose name is
// a known keyboard's or the name of an open MIDI input (is_piano_output). With no such port every
// call does nothing, so it can sit in front of a silent sink. The frame thread uses it for the
// player's notes and for update(); `play` may run on another thread (the song's MIDI sender), so
// every call that writes to the port or the note memory takes one lock.
//
// Finding and opening the port is the MIDI watcher thread's work (MidiInput::add_port_task with
// port_task()): it lists the output ports after each scan of the inputs, opens the piano's, and
// publishes it; update() on the frame thread takes it without waiting. The libremidi port stays
// the watcher thread's too: this object holds a share of it only while it is in use, so closing
// it (which can block in CoreMIDI) never happens on the frame thread.
//
// Note-offs are never lost: it remembers every note it sent a note-on for. `note_off` ends one;
// `all_off` (lost focus, quit) sends a note-off for each still sounding, then All Notes Off (CC123)
// on every channel it used and the pedal up. A port that disappears or is replaced forgets them,
// since there is nobody to tell. When the same port comes back, the first thing it is sent is All
// Notes Off and sustain off on all 16 channels, so a note held across the drop-out cannot stay on.
class MidiOutput final : public SoundSink {
  public:
    MidiOutput();
    ~MidiOutput() override;
    MidiOutput(const MidiOutput&) = delete;
    MidiOutput& operator=(const MidiOutput&) = delete;

    // Takes the output port the watcher thread opened (or let go of) since the last call: forgets
    // the notes of a port that went and, when the same port comes back, first sends it All Notes
    // Off and sustain off on all 16 channels. Never waits; lists and opens nothing itself.
    void update();
    // For MidiInput::add_port_task: what the watcher thread runs to find and open the piano's
    // port. Call once, and register it on the one MidiInput whose inputs name the piano. With
    // `ports` the player's choice of output applies: a named port that is listed is the one opened,
    // whatever it is called; otherwise (Auto, or the name gone) the piano's, and the lists of
    // outputs are published for the settings page.
    std::function<void(std::span<const std::string>)>
    port_task(std::shared_ptr<MidiPorts> ports = nullptr);

    bool connected() const { return out_ != nullptr; }
    const std::string& port_name() const { return name_; }
    // Notes it has sent a note-on for and not yet a note-off.
    int sounding() const;

    void note_on(int channel, int note, int velocity) override;
    void note_off(int channel, int note) override;
    void sustain(int channel, bool down) override;
    void all_off() override;

    // One message of the song (SongController). Its notes and pedal are remembered like the
    // player's, so all_off() stops them too; All Notes Off and All Sound Off forget a channel's.
    void play(const PlaybackMessage& message);

  private:
    // The *_locked members expect mutex_ to be held.
    void send_locked(std::uint8_t status, std::uint8_t data1, std::uint8_t data2);
    void note_on_locked(int channel, int note, int velocity);
    void note_off_locked(int channel, int note);
    void sustain_locked(int channel, bool down);
    void play_locked(const PlaybackMessage& message);
    void reset_port_locked();

    struct Port;    // an open libremidi output port (midi_output.cpp)
    struct Link;    // what the watcher thread and this object share
    class Scanner;  // the watcher thread's: its observer and the port it opened

    mutable std::mutex mutex_; // out_ (when it is reset or sent to), sounding_, pedal_, used_
    std::shared_ptr<Link> link_;
    std::shared_ptr<Port> out_;
    std::string name_;
    std::array<std::bitset<128>, 16> sounding_;
    std::bitset<16> pedal_;
    std::bitset<16> used_; // channels written to since the last all_off
    etude::PortReconnect reconnect_;
};

} // namespace etude::app
