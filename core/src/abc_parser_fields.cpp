// SPDX-License-Identifier: GPL-3.0-or-later
#include "abc_parser.hpp"

#include <algorithm>
#include <cstring>

namespace etude::abc_detail {

score_detail::RawMeasure& TuneParser::measure(int bar) {
    if (raw_.measures.size() <= static_cast<std::size_t>(bar)) {
        raw_.measures.resize(static_cast<std::size_t>(bar) + 1);
    }
    return raw_.measures[static_cast<std::size_t>(bar)];
}

std::int64_t TuneParser::bar_ticks() const {
    return kWholeTicks * tune_.meter.numerator / tune_.meter.denominator;
}

Voice* TuneParser::ensure_voice() {
    if (voice_ >= 0) {
        return &voices_[static_cast<std::size_t>(voice_)];
    }
    if (voice_ == kNoVoice) {
        return nullptr;
    }
    if (voices_.empty()) {
        Voice v;
        v.id = "1";
        v.name = "Melody";
        v.key = tune_.key;
        v.part = 0;
        raw_.parts.push_back(RawPart{v.name, {}, {}, -1});
        voices_.push_back(std::move(v));
    }
    voice_ = 0;
    return &voices_[0];
}

void TuneParser::select_voice(std::string_view value) {
    const std::string_view v = trim(value);
    std::size_t end = 0;
    while (end < v.size() && !is_space(v[end])) {
        ++end;
    }
    const std::string id(v.substr(0, end));
    if (id.empty()) {
        return;
    }
    int found = -1;
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (voices_[i].id == id) {
            found = static_cast<int>(i);
            break;
        }
    }
    const std::vector<Word> words = split_words(v.substr(end));
    if (found < 0) {
        if (voices_.size() >= static_cast<std::size_t>(kMaxVoices)) {
            if (!voices_capped_) {
                warn("more than 64 voices: the notes of the later ones were left out");
                voices_capped_ = true;
            }
            voice_ = kNoVoice;
            return;
        }
        Voice nv;
        nv.id = id;
        nv.key = tune_.key;
        nv.part = static_cast<int>(voices_.size());
        found = static_cast<int>(voices_.size());
        // A voice that was used before any V: line is the first voice; it keeps its place.
        voices_.push_back(std::move(nv));
        raw_.parts.push_back(RawPart{});
    }
    Voice& voice = voices_[static_cast<std::size_t>(found)];
    for (const Word& w : words) {
        if ((equals_nocase(w.key, "name") || equals_nocase(w.key, "nm")) && !w.value.empty()) {
            voice.name = clean_text(w.value);
        }
    }
    KeyInfo info;
    read_voice_words(words, info);
    if (info.has_clef) {
        voice.key.has_clef = true;
        voice.key.clef_bass = info.clef_bass;
        voice.key.clef_treble = info.clef_treble;
    }
    if (info.has_shift) {
        voice.key.shift = info.shift;
        voice.key.has_shift = true;
    }
    if (voice.name.empty()) {
        voice.name = "Voice " + voice.id;
    }
    raw_.parts[static_cast<std::size_t>(voice.part)].name = voice.name;
    voice_ = found;
}

void TuneParser::end_header() {
    if (tune_.header_done) {
        return;
    }
    tune_.header_done = true;
    if (!tune_.unit_given) {
        // Under three quarters the default note is a sixteenth, else an eighth.
        const bool short_meter =
            !tune_.meter.free && tune_.meter.numerator * 4 < tune_.meter.denominator * 3;
        tune_.unit_ticks = kWholeTicks / (short_meter ? 16 : 8);
    }
    for (Voice& v : voices_) {
        const KeyInfo own = v.key;
        v.key = tune_.key;
        v.key.has_clef = own.has_clef;
        v.key.clef_bass = own.clef_bass;
        v.key.clef_treble = own.clef_treble;
        v.key.shift = own.shift;
        v.key.has_shift = own.has_shift;
    }
    voice_ = voices_.empty() ? -1 : 0;
    measure(0).has_signature = true;
    measure(0).numerator = tune_.meter.numerator;
    measure(0).denominator = tune_.meter.denominator;
}

void TuneParser::set_meter_here(Voice& v) {
    const int bar = v.content ? v.bar + 1 : v.bar;
    if (bar >= static_cast<int>(kMaxSongMeasures)) {
        return;
    }
    RawMeasure& m = measure(bar);
    m.has_signature = true;
    m.numerator = tune_.meter.numerator;
    m.denominator = tune_.meter.denominator;
}

void TuneParser::field(char name, std::string_view value, std::size_t line) {
    switch (name) {
    case 'T':
        if (tune_.title.empty()) {
            tune_.title = clean_text(value);
        }
        break;
    case 'C':
        if (tune_.composer.empty()) {
            tune_.composer = clean_text(value);
        }
        break;
    case 'M': {
        Meter m;
        if (!parse_meter(value, m)) {
            warn("line " + std::to_string(line) + ": meter \"" + std::string(value) +
                 "\" not understood; 4/4 used");
            m = Meter{};
        }
        tune_.meter = m;
        if (tune_.header_done) {
            if (Voice* v = ensure_voice()) {
                set_meter_here(*v);
            }
        }
        break;
    }
    case 'L': {
        long long n = 0;
        long long d = 0;
        if (parse_fraction(value, n, d)) {
            tune_.unit_ticks = std::clamp<std::int64_t>(kWholeTicks * n / d, 1, 4 * kWholeTicks);
            tune_.unit_given = true;
        } else {
            warn("line " + std::to_string(line) + ": note length \"" + std::string(value) +
                 "\" not understood; the default is kept");
        }
        break;
    }
    case 'Q': {
        double qpm = 0.0;
        if (!parse_tempo(value, qpm)) {
            warn("line " + std::to_string(line) + ": tempo \"" + std::string(value) +
                 "\" not understood; ignored");
            break;
        }
        RawTempo t;
        t.part = 0;
        t.bpm = qpm;
        if (tune_.header_done) {
            if (Voice* v = ensure_voice()) {
                t.measure = v->bar;
                t.offset = v->pos;
            }
        } else {
            // A tempo in the tune header replaces the file header's.
            std::erase_if(raw_.tempos,
                          [](const RawTempo& o) { return o.measure == 0 && o.offset == 0; });
        }
        raw_.tempos.push_back(t);
        break;
    }
    case 'K': {
        KeyInfo k;
        if (!parse_key(value, k)) {
            warn("line " + std::to_string(line) + ": key \"" + std::string(value) +
                 "\" not understood; C major used");
        } else if (k.lone_capital_m) {
            warn("line " + std::to_string(line) + ": key \"" + std::string(value) +
                 "\": M read as major");
        }
        if (!tune_.header_done) {
            tune_.key = k;
            measure(0).has_key = true;
            measure(0).key_fifths = fifths_of(k.sig);
            note_song_key(raw_, 0, 0, k);
            for (Voice& v : voices_) {
                v.key.sig = k.sig;
            }
            // Parameters on the key line belong to the first voice, or to every voice.
            end_header();
            for (Voice& v : voices_) {
                if (k.has_clef && !v.key.has_clef) {
                    v.key.has_clef = true;
                    v.key.clef_bass = k.clef_bass;
                    v.key.clef_treble = k.clef_treble;
                }
                if (k.has_shift && !v.key.has_shift) {
                    v.key.shift = k.shift;
                    v.key.has_shift = true;
                }
            }
        } else if (Voice* v = ensure_voice()) {
            const bool clef = v->key.has_clef;
            const bool bass = v->key.clef_bass;
            const bool treble = v->key.clef_treble;
            const int shift = v->key.shift;
            const bool has_shift = v->key.has_shift;
            if (k.sig != v->key.sig) {
                measure(v->bar).has_key = true;
                measure(v->bar).key_fifths = fifths_of(k.sig);
            }
            if (!k.same_song_key(v->key)) {
                note_song_key(raw_, v->bar, v->pos, k);
            }
            v->key = k;
            if (!k.has_clef) {
                v->key.has_clef = clef;
                v->key.clef_bass = bass;
                v->key.clef_treble = treble;
            }
            if (!k.has_shift) {
                v->key.shift = shift;
                v->key.has_shift = has_shift;
            }
        }
        break;
    }
    case 'V':
        select_voice(value);
        break;
    default:
        break;
    }
}


} // namespace etude::abc_detail
