// SPDX-License-Identifier: GPL-3.0-or-later
// Lucide icon codepoints (lucide-font 1.55.0, codepoints.json) as UTF-8, for text drawn with a
// font the icon font is merged into.
#pragma once

namespace etude::app::ui::icon {

// Not a font icon: glyph() draws a C marker (a bold C over a short line) for this pointer.
inline constexpr const char* kCMarker = "\x01";
// Not font icons either: glyph() draws an open hand seen from its back, as a player sees their own
// hands on the keys: the thumb points inward, on the right of the left hand and on the left of the
// right hand, in the stroke style of the other icons.
inline constexpr const char* kLeftHand = "\x02";
inline constexpr const char* kRightHand = "\x03";
// Drawn too (the icon font has neither): a quarter note, the tempo chip's mark, a dotted quarter
// (its mark in 6/8, 9/8 and 12/8) and a metronome.
inline constexpr const char* kNote = "\x04";
inline constexpr const char* kMetronome = "\x05";
inline constexpr const char* kNoteDotted = "\x06";
// Drawn too: "play it again", a circular arrow with a play head (MorphGlyph::Replay). The one icon
// for that meaning: the bar's replay button and the results card's Replay.
inline constexpr const char* kReplay = "\x07";
// Drawn too: the Ear card's modes, one or two small blocks: a note, an interval up (the second block
// higher), down (lower) and together (side by side). Also the mark in the interval's pill.
inline constexpr const char* kEarNote = "\x08";
inline constexpr const char* kEarUp = "\x09";
inline constexpr const char* kEarDown = "\x0A";
inline constexpr const char* kEarTogether = "\x0B";

inline constexpr const char* kQuill = "\xEE\x82\xBE";      // feather

inline constexpr const char* kSettings = "\xEE\x85\x94";    // settings
inline constexpr const char* kNames = "\xEE\x8F\x99";       // case-sensitive
inline constexpr const char* kStyle = "\xEE\x90\x92";       // sparkles
inline constexpr const char* kPalette = "\xEE\x87\x9D";     // palette
inline constexpr const char* kConfigs = "\xEE\x81\xA0";     // bookmark
inline constexpr const char* kEffects = "\xEE\x8D\x97";     // wand-sparkles
inline constexpr const char* kView = "\xEE\x84\x9D";        // monitor
inline constexpr const char* kPractice = "\xEE\x86\x80";    // target
inline constexpr const char* kBack = "\xEE\x81\xAE";        // chevron-left
inline constexpr const char* kChevron = "\xEE\x81\xAF";     // chevron-right
inline constexpr const char* kClose = "\xEE\x86\xB2";       // x
inline constexpr const char* kCheck = "\xEE\x81\xAC";       // check
inline constexpr const char* kWarning = "\xEE\x81\xB7";     // circle-alert
inline constexpr const char* kInfo = "\xEE\x83\xB9";        // info
inline constexpr const char* kReset = "\xEE\x85\x88";       // rotate-ccw
inline constexpr const char* kBackspace = "\xEE\x82\xAE";   // delete
inline constexpr const char* kEnter = "\xEE\x82\xA1";       // corner-down-left
inline constexpr const char* kSave = "\xEE\x85\x8D";        // save
inline constexpr const char* kRename = "\xEE\x87\xB9";      // pencil
inline constexpr const char* kClear = "\xEE\x86\x8E";       // trash-2
inline constexpr const char* kLoad = "\xEE\x82\xB2";        // download
inline constexpr const char* kContrast = "\xEE\x82\x9D";    // contrast
inline constexpr const char* kFitKeys = "\xEE\x87\x86";     // move-horizontal
inline constexpr const char* kFitSong = "\xEE\x84\xA2";     // music
inline constexpr const char* kPin = "\xEE\x89\x99";         // pin
inline constexpr const char* kPlug = "\xEE\x8D\xBF";        // plug
inline constexpr const char* kCircleCheck = "\xEE\x88\xA6"; // circle-check
inline constexpr const char* kKeyboard = "\xEE\x95\xA0";    // keyboard-music
inline constexpr const char* kPiano = "\xEE\x95\xA1";       // piano
inline constexpr const char* kTimer = "\xEE\x87\xA0";       // timer
inline constexpr const char* kGauge = "\xEE\x86\xBF";       // gauge
inline constexpr const char* kHome = "\xEE\x83\xB5";        // house
inline constexpr const char* kGrid = "\xEE\x83\xBF";        // layout-grid
inline constexpr const char* kFlash = "\xEE\x86\xB4";       // zap
inline constexpr const char* kGlow = "\xEE\x85\xB8";        // sun
inline constexpr const char* kSparks = "\xEE\x91\xBE";      // sparkle
inline constexpr const char* kTrail = "\xEE\x8A\x83";       // waves
inline constexpr const char* kPhone = "\xEE\x85\xA3";       // smartphone
inline constexpr const char* kWifi = "\xEE\x86\xAE";        // wifi
inline constexpr const char* kClock = "\xEE\x82\x87";       // clock
inline constexpr const char* kDrive = "\xEE\x83\xAD";       // hard-drive
inline constexpr const char* kUsb = "\xEE\x8D\x96";         // usb
inline constexpr const char* kFlame = "\xEE\x83\x92";       // flame
inline constexpr const char* kBackground = "\xEE\x83\xB6";  // image
inline constexpr const char* kChord = "\xEE\x8E\xA4";       // captions
inline constexpr const char* kHeight = "\xEE\x8D\xBD";      // arrow-up-down
inline constexpr const char* kSeconds = "\xEE\x8A\x96";     // hourglass
inline constexpr const char* kOctave = "\xEE\x83\xAF";      // hash
inline constexpr const char* kLanguages = "\xEE\x98\x85";   // letter-text
inline constexpr const char* kMarker = "\xEE\x84\x91";      // map-pin
inline constexpr const char* kReduced = "\xEE\x8A\x99";     // sun-dim
inline constexpr const char* kDrop = "\xEE\x82\xB4";        // droplet
inline constexpr const char* kLayers = "\xEE\x94\xA9";      // layers
inline constexpr const char* kPlus = "\xEE\x84\xBD";        // plus
inline constexpr const char* kMinus = "\xEE\x84\x9C";       // minus
inline constexpr const char* kSize = "\xEE\x85\x8B";        // ruler
inline constexpr const char* kTextSize = "\xEE\x96\x87";   // a-large-small: a text's size
inline constexpr const char* kBlend = "\xEE\x96\x9C";       // blend
inline constexpr const char* kPrevious = "\xEE\x85\x9F";    // skip-back
inline constexpr const char* kRewind = "\xEE\x85\x87";      // rewind
inline constexpr const char* kForward = "\xEE\x82\xBD";     // fast-forward
inline constexpr const char* kPlay = "\xEE\x84\xBC";        // play
inline constexpr const char* kPause = "\xEE\x84\xAE";       // pause
inline constexpr const char* kMute = "\xEE\x86\xAC";        // volume-x
inline constexpr const char* kSoundOther = "\xEE\x86\xAA";  // volume-1
inline constexpr const char* kSoundBoth = "\xEE\x86\xAB";   // volume-2
inline constexpr const char* kCountdown = "\xEE\x88\xB6";   // timer-reset
inline constexpr const char* kSkipBack = "\xEE\x85\x88";    // rotate-ccw
inline constexpr const char* kSkipForward = "\xEE\x85\x89"; // rotate-cw
inline constexpr const char* kBar = "\xEE\x90\xB5";         // panel-top
inline constexpr const char* kTyping = "\xEE\x8A\x84";      // keyboard
inline constexpr const char* kMic = "\xEE\x84\x98";         // mic
inline constexpr const char* kWave = "\xEE\x95\x9B";        // audio-waveform

inline constexpr const char* kScissors = "\xEE\x85\x8E"; // scissors
inline constexpr const char* kUndo = "\xEE\x8A\xA1"; // undo-2
inline constexpr const char* kZoomIn = "\xEE\x86\xB6"; // zoom-in
inline constexpr const char* kZoomOut = "\xEE\x86\xB7"; // zoom-out
inline constexpr const char* kFitAll = "\xEE\x84\x92"; // maximize
inline constexpr const char* kMerge = "\xEE\x90\xBF"; // merge
inline constexpr const char* kListMusic = "\xEE\x8B\xA0"; // list-music
inline constexpr const char* kArrowUp = "\xEE\x81\x8A"; // arrow-up
inline constexpr const char* kArrowDown = "\xEE\x81\x82"; // arrow-down
inline constexpr const char* kEar = "\xEE\x8E\x82"; // ear
inline constexpr const char* kAcross = "\xEE\x89\x8A"; // arrow-left-right
inline constexpr const char* kStop = "\xEE\x85\xA7"; // square
inline constexpr const char* kAddMarker = "\xEE\x98\x93"; // map-pin-plus
inline constexpr const char* kListPlus = "\xEE\x88\xBF"; // list-plus
inline constexpr const char* kRepeat = "\xEE\x85\x86"; // repeat
inline constexpr const char* kSkipNext = "\xEE\x85\xA0"; // skip-forward
inline constexpr const char* kTrash = "\xEE\x86\x8E"; // trash-2
inline constexpr const char* kUnfold = "\xEE\x90\xBE"; // unfold-vertical: open the lane
inline constexpr const char* kFold = "\xEE\x90\xBC";   // fold-vertical: close it
inline constexpr const char* kChecked = "\xEE\x88\xA6";   // circle-check
inline constexpr const char* kUnchecked = "\xEE\x81\xB6"; // circle
inline constexpr const char* kOneByOne = "\xEE\x87\x91";  // list-ordered
inline constexpr const char* kChartBars = "\xEE\x8A\xA2";  // chart-bar: horizontal bars
inline constexpr const char* kChartLine = "\xEE\x8A\xA5";  // chart-line
inline constexpr const char* kLearn = "\xEE\x88\xB4";    // graduation-cap: the Learn screen
inline constexpr const char* kScales = "\xEE\x81\xA8";   // chart-no-axes-column: the scale card
inline constexpr const char* kHistory = "\xEE\x87\xB5";  // history: Continue
inline constexpr const char* kOrbit = "\xEE\x8F\xA7";    // orbit: the key ring
inline constexpr const char* kLibrary = "\xEE\x84\x80";   // library
inline constexpr const char* kHint = "\xEE\x87\x82";      // lightbulb: the Ear card's Hint
inline constexpr const char* kNext = "\xEE\x81\x89";      // arrow-right: the next question
inline constexpr const char* kAnyOctave = "\xEE\x88\x91"; // chevrons-up-down: any octave is right
inline constexpr const char* kOptions = "\xEE\x8A\x9A";   // sliders-horizontal: the Learn card's Options
inline constexpr const char* kMelodies = "\xEE\x84\xA2";  // music: the Melodies tab and tile
inline constexpr const char* kSprout = "\xEE\x87\xAB";    // sprout: the first tunes' path
inline constexpr const char* kHandshake = "\xEE\x97\x80"; // handshake: the both hands path
inline constexpr const char* kAward = "\xEE\x81\x8F";     // award: the first repertoire path

} // namespace etude::app::ui::icon
