// SPDX-License-Identifier: GPL-3.0-or-later
// Draws one frame: background, the song's falling notes and the player's rising note blocks,
// keyboard and chord name, each in its own region from etude::compute_layout (the top row is
// the UI's), coloured, styled and named by a LookSettings. Holds no music state; the frame loop
// passes everything in. Each layer is a few SDL_RenderGeometry batches plus cached text textures
// (note names are cached per name and size and capped per frame), so the cost per frame stays
// flat. The effects level (from the core
// EffectsGovernor) decides which effects the frame can afford.
#pragma once

#include "chord_label_view.hpp"
#include "effects.hpp"
#include "gfx.hpp"
#include "glow_shader.hpp"
#include "look.hpp"
#include "mesh.hpp"
#include "text.hpp"
#include "etude/accidental_glyph.hpp"
#include "etude/block_column.hpp"
#include "etude/block_names.hpp"
#include "etude/combo_display.hpp"
#include "etude/effects_governor.hpp"
#include "etude/evaluator.hpp"
#include "etude/grade_words.hpp"
#include "etude/hand.hpp"
#include "etude/key_label.hpp"
#include "etude/key_signature.hpp"
#include "etude/key_state.hpp"
#include "etude/keyboard.hpp"
#include "etude/keyboard_view.hpp"
#include "etude/look.hpp"
#include "etude/note_colours.hpp"
#include "etude/note_style.hpp"
#include "etude/note_trail.hpp"
#include "etude/piece_widths.hpp"
#include "etude/replay_keys.hpp"
#include "etude/replay_marks.hpp"
#include "etude/scale_overlay.hpp"
#include "etude/screen_layout.hpp"
#include "etude/song.hpp"
#include "etude/transpose.hpp"

#include <SDL3/SDL_render.h>

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace etude::app {

// Never pressed, for FrameInput::last_press_ns.
inline constexpr std::int64_t kNeverPressed = INT64_MIN / 2;

// A note of the song on its way down to the hit line: NoteWindow's VisibleNote, placed on a key.
struct SongBar {
    std::uint8_t key = 0; // after transposition
    std::uint8_t velocity = 80;
    Hand hand = Hand::Unknown;
    // Fractions of the note area: 0 is the hit line, 1 the top. A note that has reached the line
    // has bottom <= 0; the bed hides what is below it.
    float bottom = 0.0f;
    float top = 0.0f;
    bool practised = true;  // false: a hand you are not practising, drawn faint
    std::int8_t grade = -1; // its NoteGrade once graded, else -1
    std::int8_t fold = 0;   // moved by this many octaves to fit the keyboard: a marker shows it
    // The player's held key is striking it (note_struck): lit in its hand's colour while the part
    // above the hit line shortens into the key.
    bool struck = false;
    // The finger that plays it (1..5), shown on the block of a drill; 0: none.
    std::int8_t finger = 0;
};

// A grade shown briefly where a note met the hit line (Flow Mode).
struct GradePopup {
    std::uint8_t key = 0;
    NoteGrade grade = NoteGrade::Missed;
    float age = 0.0f; // 0 when it appears, 1 when it is gone
};

// A soft "?" over a key, where the microphone heard something it could not name. It marks a
// register on the hit line and presses nothing.
struct UnsureMarker {
    std::uint8_t key = 0;
    float alpha = 1.0f; // 1 at first, 0 once faded (unsure_marker_alpha)
};

// The key of the song at the playhead, for the key label: null for none (no song, or too few notes
// to tell). `transpose` is the session's shift in semitones; the label names the key played.
struct KeyLabelIn {
    const KeyChange* key = nullptr;
    int transpose = 0;
};

inline constexpr std::uint8_t kPressFine = 0;
inline constexpr std::uint8_t kPressWrong = 1;
inline constexpr std::uint8_t kPressPending = 2;

struct FrameInput {
    // Built from `view.layout_range`: every key at least partly in the window.
    const KeyboardLayout& layout;
    const KeyViewFrame& view; // where the window sits on `layout`, see KeyViewFrame::to_window
    const KeyState& keys;
    std::span<const TrailBar> bars; // from NoteTrail::visible with Scene::trail_span_ns()
    // Latest note-on time per MIDI note, kept after release, so a tap shorter than a frame still
    // flashes its key.
    const std::array<std::int64_t, 128>& last_press_ns;
    // Note-on velocity of that latest press, so a lit key matches its block's colour.
    const std::array<std::uint8_t, 128>& last_velocity;
    std::int64_t now_ns;
    bool no_keyboard; // shows the "connect a keyboard" prompt in the note area
    // The computer-key letter that plays each note right now ("A", ";"), empty for none, drawn
    // as small keycaps on the piano keys; null to draw none (--show-typing-keys).
    const std::array<std::string_view, 128>* typing_labels = nullptr;

    // The song, when one is loaded. Set after construction.
    std::span<const SongBar> song_bars{};
    // The hands' chord labels (#272): their timelines (null: none), a number that changes when the
    // timelines are replaced, and the playhead, song seconds, whose chord each hand shows.
    const HandChords* chords = nullptr;
    std::uint32_t chords_serial = 0;
    double chords_time_s = 0.0;
    Transposition
        chords_transposition{}; // the song's, to put the label's keys where the blocks are
    // Its notes and the song time at the hit line, for the beat lines (null: none drawn).
    const Song* song = nullptr;
    double song_now_s = 0.0;
    std::span<const GradePopup> grades{};
    // A replay's played bars, wrong keys and missed notes over the song's blocks (empty: none), from
    // `replay_bar_marks`.
    std::span<const ReplayBarMark> replay_marks{};
    // The keys a replay lights, as live play lights a held key but in the note's hand (null: none).
    // A wrong key is lit through `wrong_flash`, as in live play.
    const ReplayKeys::Lit* replay_keys = nullptr;
    std::span<const UnsureMarker> unsure{};
    // Wait Mode: per key, 1 + the Hand of the chord note waiting there, 0 for none. Those keys
    // pulse in their hand's colour until pressed.
    const std::array<std::uint8_t, 128>* hint_hands = nullptr;
    // The player's rising blocks are drawn this opaque: 0 hides them (a song open, unless the look
    // asks for them: LookSettings::rising_in_song), fainter when they show beside a song's notes,
    // so the two don't compete.
    float trail_alpha = 1.0f;
    // Per key, how strong the red flash of a wrong press is, 0..1 (wrong_flash_alpha); null for
    // none.
    const std::array<float, 128>* wrong_flash = nullptr;
    // Per key, what the latest press looks like to the grading (kPressFine, kPressWrong or
    // kPressPending): a wrong press starts no hit effect, only the red flash, and one not judged
    // yet waits a frame for it; null for all fine.
    const std::array<std::uint8_t, 128>* press_look = nullptr;
    KeyLabelIn key_label{};
    // The combo on screen (#88): the evaluator's combo, and whether this frame crossed a milestone
    // (SongController::take_combo_milestone). The number and the rail animate on `now_ns`.
    int combo = 0;
    bool combo_milestone = false;
    // Changes when a song is loaded or unloaded (SongController::load_serial): the number starts
    // over instead of fading across songs.
    std::uint32_t combo_epoch = 0;
};

class Scene {
  public:
    Scene(SDL_Renderer* renderer, const LookSettings& settings);

    const LookSettings& settings() const { return settings_; }
    void set_settings(const LookSettings& settings);
    // The renderer is being replaced (gfx::replace_renderer): the effect textures go while the old
    // one is alive, and are made again for the new one. The name and word textures are text slots,
    // which remake themselves.
    void drop_renderer();
    void adopt_renderer(SDL_Renderer* renderer);
    // The free-play scale overlay (core scale_overlay.hpp), or null for none: keys outside the
    // scale are dimmed, a played note outside it is red, the root keys carry the root's name and
    // the fingers show on their keys. The mask is the caller's, worked out once when the choice
    // changes; it must outlive the frame.
    void set_scale_overlay(const ScaleOverlayMask* scale) { scale_ = scale; }
    // The control bar kept shown: the notes are laid out below it (compute_layout). Cheap to call
    // every frame; the geometry is rebuilt only when it changes.
    void set_pinned_bar(const PinnedBar& bar);
    // The count band's height in pixels (0: none) and how far it is shown (0..1): the solid top of
    // the note area a count-in or the resume countdown shows in (count_band.hpp).
    void set_count_band(float height_px, float shown) {
        count_band_h_ = height_px;
        count_band_shown_ = shown;
    }
    // The UI's growth the frame draws with (`UiGrowHold`), so the scene's layout keeps the top row
    // the UI draws while a resize holds the old growth.
    void set_ui_grow(float grow);
    // Where the falling notes go: the note area, or what sheet music leaves of it (empty in the
    // Sheet view, when no note is drawn); `chord_label` false while the score covers the chord
    // name's strip. Cheap every frame.
    void set_falling_area(const RectF& area, bool chord_label);
    // Time a note takes to cross the note area (LookSettings::visible_seconds), for NoteTrail.
    std::int64_t trail_span_ns() const {
        return static_cast<std::int64_t>(static_cast<double>(settings_.visible_seconds) * 1e9);
    }
    // Where the key label was placed in the last frame, in window pixels; empty when it is not
    // shown. The toasts keep clear of it.
    const RectF& key_label_rect() const { return key_rect_; }
    // Where the combo number was placed in the last frame (window pixels, with its air round it),
    // empty when it is not shown. Toasts and the grade words keep clear of it, and so do the note
    // names.
    const RectF& combo_rect() const { return combo_rect_; }
    // Where each hand's chord name and line stand in the last frame (window pixels, empty for a
    // hand with none): the toasts keep clear of them.
    std::span<const RectF> chord_label_rects() const { return chord_view_.rects(); }
    // The UI's unit in pixels (pixels per design point, `ui_scale`), which sizes the combo number
    // and its margins like every other control; 0 (the default) takes the scene's own from the
    // window.
    void set_ui_unit(float px_per_pt) { ui_unit_ = px_per_pt; }
    // The pixels per millimetre the UI draws with; 0 (the start) takes the display's density at 96
    // dpi.
    void set_px_per_mm(float px_per_mm);
    // Where the section band hanging from the revealed bar ends over the top of the notes (window
    // pixels; 0 for none): the combo number stays below it, gliding down as the band comes and
    // back up as it goes.
    void set_combo_clear_y(float y) { combo_clear_y_ = y; }
    // A card in the note area's top that the combo number steps under (the octave-shift offer),
    // window pixels; empty for none.
    void set_combo_keep_out(const RectF& r) { combo_keep_out_ = r; }
    // The sheet-music button at the notes' top right (window pixels; empty for none): the combo
    // number stays under it, and the key label and the chord names keep clear of it.
    void set_side_button(const RectF& r) { side_button_ = r; }

    // Which effects the frame can afford (EffectsGovernor). Notes and keys are drawn at every
    // level.
    void set_effects_level(EffectsLevel level);
    EffectsLevel effects_level() const { return level_; }
    // Whether the frame has a shader effect (the Neon glow, on a renderer that has the shader), so
    // the governor's first step down is real.
    bool has_shader_effect() const {
        return settings_.note_style == NoteStyle::Neon && glow_shader(renderer_) != nullptr;
    }

    // Starts a frame's budget for making text textures (`kRasterBudgetNs`), shared by every Scene
    // that draws in it (the screen's and the settings preview's). The frame loop calls it once
    // before its first draw. `budgeted` false (a still that must show every name) makes every
    // texture when it is wanted; so does a program that never calls it.
    static void begin_frame(bool budgeted = true);

    // Room for a song whose densest view holds `notes` notes (densest_window), so a frame of it
    // grows no buffer on the way. Capped at a bounded size; a denser view grows them once, as does a
    // larger "Notes ahead" than the song was loaded with: they keep the capacity after.
    void reserve_notes(std::size_t notes);

    // Clears the screen and draws the frame, sized from the current render target in pixels.
    void draw(const FrameInput& in);

  private:
    struct KeyPx {
        float x = 0.0f;
        float width = 0.0f;
        bool black = false;
        bool in_range = false;
    };

    // The colours of one note, shared by its block and its key so the two always match. The
    // style (StylePainter) is applied on top of these.
    struct NotePaint {
        SDL_FColor base;   // the note's colour, flash applied: the far end of a block
        SDL_FColor lower;  // key end of a block (lighter while held), the front of a lit key
        float glow = 0.0f; // glow alpha, 0 for none
        float glow_reach = 1.0f; // how much wider than the style's own glow it reaches
        float wash = 0.0f;       // a fresh press's white wash over a lit key, 0..1 (Key flash)
    };
    // The note's colour from the core look, so a block and its key always match.
    SDL_FColor fill_for(int note, int velocity) const;
    NotePaint paint(int note, int velocity, bool held, float flash,
                    Hand hand = kFreePlayHand) const;
    SDL_FColor hand_fill(int note, int velocity, Hand hand) const;
    // True when the scale overlay is on and `note` is not in the scale.
    bool outside_scale(int note) const {
        return scale_ != nullptr && scale_->on && !scale_->in_scale(note);
    }
    // The face of an unlit key outside the scale: toward a cool grey on white keys, toward the
    // background on black ones.
    SDL_FColor dimmed_key(SDL_FColor face, bool black) const;
    // An unlit key's face (and a black key's lip), dimmed while the overlay is on and the key is
    // outside the scale.
    void add_unlit_white(Mesh& out, bool dim, const SDL_FRect& face, float radius);
    void add_unlit_black(Mesh& out, bool dim, const SDL_FRect& face, float radius,
                         const SDL_FRect& lip, float lip_radius);
    // A C key shows its marker (name and octave) whatever the overlay does.
    bool has_c_marker(int note) const {
        return c_marker_px_ > 0.0f && !c_names_[static_cast<std::size_t>(note)].empty();
    }
    // The scale overlay names this key (a root with no C marker): its plain name is not drawn.
    bool names_root_key(int note) const {
        return scale_ != nullptr && scale_->is_root(note) && !has_c_marker(note);
    }
    // The scale overlay's marks over the keys: the root keys' accent bar and name, the fingers.
    void draw_scale_marks(float top, float bottom, float black_length);
    // A drill's finger numbers, on the blocks that carry them (scene_fingers.cpp).
    void draw_finger_marks();
    void prepare_beat_lines(const FrameInput& in);
    void draw_song_bars(const FrameInput& in, float base_y, float min_height, float gap);
    // A replay's marks over the song's blocks (scene_replay.cpp).
    void draw_replay_marks(const FrameInput& in, float base_y, float min_height);
    void draw_grades();
    void draw_unsure(const FrameInput& in);
    // Queues the name of `note` for `block` in `column`, `colour` (it contrasts with the block)
    // times `alpha`. Names are placed once the frame's blocks are known (draw_note_text).
    void add_block_name(int note, const BlockColumn& column, const RectF& block, SDL_FColor colour,
                        float alpha);
    void update_recipe();
    // Starts a hit effect for each fresh press, then draws the running ones in the note area,
    // beneath the blocks (kSceneLayers).
    void draw_hit_effects(const FrameInput& in);
    float flash_at(std::int64_t now_ns, std::int64_t pressed_ns) const;
    // The press effects' switches as the look has them.
    PressState press_state() const {
        return {settings_.key_flash, settings_.held_glow, settings_.reduced_effects};
    }

    // The grade words, placed by core's place_grade_words so none overlaps another, a note name
    // inside a block or the chord label. Its text slots are measured at the current size.
    float grade_font_px() const;
    // Measures and places this frame's words (into `grades_`), then draws them from it.
    void layout_grades(const FrameInput& in);

    // A note name of `size` px centred on `centre_x` with its lower edge at `bottom`. The glyphs
    // are cached per (text, size), so a name is only rasterised the first time it is drawn.
    // Returns the width and height of the drawn text.
    SDL_FPoint draw_name(const std::string& text, float size, float centre_x, float bottom,
                         SDL_FColor colour, bool bold = false);
    // A cached slot for `text` at exactly `px` whole pixels with an outline of `outline_px`, for
    // the pieces of a planned block name; made once, then reused every frame. Once the frame has
    // spent its budget, a slot not made yet is replaced by one of the same text at another size,
    // stretched to the planned box (`width` by the size's own line height), when that fits inside
    // the planned box (`max_height` is its room for the text) and does not distort the letters
    // much; else there is no slot (`slot` null) and the name is left out of this frame. The right
    // slot is made in a later frame.
    struct PieceSlot {
        TextSlot* slot = nullptr;
        bool stand_in = false; // `box` is where to stretch it to
        SDL_FRect box{};
    };
    PieceSlot piece_slot(const std::string& text, float px, float outline_px, float width,
                         float max_height, float x, float y);
    // The slot of `text` with a texture whose size is nearest `px`, or null.
    TextSlot* nearest_slot(const std::string& text, int px);
    // The cached slot for `text` at `size` rounded down to an even pixel count; null when the
    // frame's budget is spent and it is not made yet (the caller draws nothing this frame).
    TextSlot* name_slot(const std::string& text, float size, bool bold);
    // Drops the slots drawn longest ago while there are more than kMaxCachedNames, a few a frame.
    void trim_name_cache();
    std::uint64_t frame_ = 0; // draws so far, for the slots' `used`
    // A key's name drawn like a block's: its accidental as the same drawn glyph (into `mesh_`).
    void draw_key_name(int note, float size, float centre_x, float bottom, SDL_FColor colour);
    // Adds `glyph` with its top-left at (x, y), each corner pushed `grow` px away from its centre.
    static void add_glyph(Mesh& mesh, const AccidentalShape& glyph, float x, float y, float grow,
                          SDL_FColor colour);
    BlockNameParams name_params() const;
    void draw_block_names();
    // How much of a name at `rect` shows through the fog at the top of the note area, 0..1.
    float fog_visibility(const RectF& rect) const;
    // A block's corner radius at width `w`: the look's, scaled by the style's corner share.
    float block_radius(float w) const;
    // The beat lines across a song block (`block`, window pixels), clear of the name at its foot;
    // `black` draws them with the black keys' blocks.
    void add_beat_lines(const SDL_FRect& block, bool black, SDL_FColor colour, float alpha);
    // Adds one falling block: its glow, its body (black keys' blocks in their fuller style, with
    // the outline where they lie over white ones) and its queued name.
    void add_block(int note, const BlockColumn& c, float top, float bottom, float radius,
                   const NotePaint& p, float alpha, bool glow, SDL_FColor name_colour);
    SDL_FColor label_colour_for(int note, int velocity) const;
    SDL_FColor key_label_colour_for(int note, int velocity) const; // on a lit key
    void update_geometry(const KeyboardLayout& layout, const KeyViewFrame& view, float width,
                         float height);
    void draw_background(std::int64_t now_ns);
    SDL_Rect effects_clip() const; // the note area and the key bed
    void draw_notes(const FrameInput& in);
    void draw_note_text(const FrameInput& in);
    void draw_edge_indicators();
    void draw_keyboard(const FrameInput& in);
    void add_key_tip(float x, float width, float top, float bottom, float tip_h, const NotePaint& p);
    void add_key_glow(int note, const NotePaint& p, float glow_size);
    // The chord label (#272) in the strip above the note area.
    void draw_chord_labels(const FrameInput& in);
    // The key label: prepare_key_label() fades and measures it and reserves its place before the
    // blocks are built (they note whether one passes behind it); draw_key_label() draws it.
    void prepare_key_label(const FrameInput& in);
    void draw_key_label();
    // The combo (#88): prepare_combo() steps its animation and places the number (before the
    // blocks, so their names keep clear of it); draw_combo() draws the number and the fire icon
    // in the Combo layer.
    void prepare_combo(const FrameInput& in);
    void prepare_combo_text(float px);
    void draw_combo();
    float ui_unit_px() const;
    void draw_prompt(const FrameInput& in);

    SDL_Renderer* renderer_;
    LookSettings settings_;
    NoteColourTable colours_; // made with the look (rebuild_names), looked up per frame
    PinnedBar pinned_bar_;
    float count_band_h_ = 0.0f;
    float count_band_shown_ = 0.0f;
    float ui_grow_ = 0.0f; // 0: the window's own
    Look look_; // render-only constants
    Text text_;
    Mesh mesh_;
    Mesh overlay_;    // pressed-key bands, lips and the bed's shadow over the styled keys
    Mesh name_halo_;  // the outline of the accidentals in block names, under the text
    Mesh name_glyph_; // their fill, over it
    GlowLayer glow_;  // block and key glow sprites, drawn clipped to the notes and again to the bed
    Mesh fx_;         // hit effects and the motion background, drawn with the glow sprite
    Mesh hit_lines_;  // the hit effects' rings, untextured
    EffectTextures textures_;
    StylePainter painter_; // white keys' blocks and keys, in the look's style
    StylePainter
        black_painter_; // black keys' blocks and keys, the style's fuller version, over them
    Mesh outline_;      // the dark outline of black keys' blocks over white ones (KeyWidth)
    HitEffects hits_;
    std::vector<HitShape> hit_shapes_; // this frame's, HitEffects::kMaxShapes long
    EffectsLevel level_ = EffectsLevel::Full;
    EffectsBudget budget_;
    StyleRecipe recipe_;                         // the look's style at the current effects level
    StyleRecipe black_recipe_;                   // its fuller version, for black keys
    // This frame's beat lines: heights above the hit line as a share of the note area, lowest
    // first (core beat_line_heights), and how much of a block's foot they keep clear for its name.
    std::array<float, 256> beat_heights_{};
    std::size_t beat_count_ = 0;
    float beat_name_clear_px_ = 0.0f;
    std::array<std::int64_t, 128> seen_press_{}; // last note-on each note started an effect for
    std::int64_t epoch_ns_ = 0;                  // first frame, for the motion background clock
    bool has_epoch_ = false;

    // Pixel geometry, rebuilt when the output size, the keyboard range or the look changes.
    bool geometry_valid_ = false;
    RectF falling_area_{};
    bool falling_set_ = false;
    bool chord_label_shown_ = true;
    float width_ = 0.0f;
    float height_ = 0.0f;
    KeyRange range_{0, 0};
    BlockLayout blocks_ = BlockLayout::KeyWidth;
    float pan_ = -1.0f; // the view the keys_ were placed for
    float span_ = -1.0f;
    ScreenLayout layout_;
    float px_per_mm_ = kDefaultPxPerMm;
    float px_per_mm_set_ = 0.0f; // set_px_per_mm
    float unit_ = 1.0f; // pixels per 1080p pixel
    float white_width_ = 0.0f;
    std::array<KeyPx, 128> keys_{};       // the keys, for drawing the keyboard
    // The unlit key faces, built again only when a key lights or goes dark, the scale overlay
    // changes or the keys move (`keys_epoch`), so an idle keyboard builds nothing.
    struct UnlitKeys {
        Mesh white;
        Mesh black;
        std::array<std::uint8_t, 128> state{}; // 0 lit or off, 1 unlit, 2 unlit and dimmed
        std::array<float, 4> params{};         // top, key area height, unit, white key width
        std::uint64_t epoch = 0;
        bool valid = false;
    } unlit_keys_;
    std::uint64_t keys_epoch_ = 1; // counts every placement of `keys_`
    std::array<BlockColumn, 128> cols_{}; // the lanes and blocks above it

    // Per-note data from the settings: colours (all but ByVelocity, which needs the velocity),
    // names, and the key-name font fitted to the white and black keys.
    std::array<SDL_FColor, 128> fill_{};
    const ScaleOverlayMask* scale_ = nullptr;
    std::array<SDL_FColor, 128> label_colour_{};
    std::array<SDL_FColor, 128> key_label_colour_{};
    std::array<std::string, 128> names_;
    // The computer-keyboard letters drawn this frame (draw_keyboard's keycaps), kept so a frame with
    // them on allocates nothing.
    struct TypingCap {
        int note;
        float cx;
        float cy;
        bool black;
    };
    std::vector<TypingCap> typing_caps_;
    // The pieces of each block name and how wide each is at every whole size the planner may
    // choose, measured with the real font at that size (core's planner has no font). The widths
    // live in one table shared by every Scene (`shared_piece_widths`: one font, so one answer).
    // They are measured ahead of the settings that need them (`pump_names`, once the scene has
    // drawn a few frames); when a setting still brings a piece not measured yet, a slice per frame
    // does it while the names drawn so far stay. They are never measured while a frame is laid
    // out. The spans point into the table.
    std::array<NameParts, 128> name_parts_;
    std::array<AccidentalKind, 128> name_accidental_{};
    std::array<NameWidths, 128> name_widths_{};
    Text::VerticalMetrics name_metrics_;
    bool has_metrics_ = false;
    // The names the settings ask for, waiting for the widths of their pieces. They replace the
    // ones above in one step when every piece is measured.
    struct NameSet {
        std::array<std::string, 128> names;
        std::array<NameParts, 128> parts;
        std::array<AccidentalKind, 128> accidental{};
        std::array<NameWidths, 128> widths{};
        std::array<std::string, 128> c_names;
        std::vector<PieceWidths::Index> wanted; // the pieces of `parts`
    };
    NameSet next_names_;
    bool names_pending_ = false;
    // While the names are pending: notes whose drawn name (or C marker) differs from the one the
    // settings now ask for. Those are not drawn until the new names are in, never in the old form.
    std::array<bool, 128> name_stale_{};
    std::array<bool, 128> c_stale_{};
    // What the names above (or the pending ones) were made from: the settings that decide them.
    struct NameKey {
        NameMode mode = NameMode::Off;
        bool flats = false;
        bool octave = false;
        bool c_markers = false;
        OctaveNumbering numbering = OctaveNumbering::Scientific;
        bool operator==(const NameKey&) const = default;
    };
    NameKey names_key_;
    bool has_names_key_ = false;
    PieceWidths& widths_;
    std::vector<PieceWidths::Index> warm_pieces_; // every piece any naming can need
    int warm_frames_ = 0;
    bool warmed_ = false;
    // Measures `pieces` for at most `budget_ns` (all of it when negative). True when all are done.
    bool measure_pieces(std::span<const PieceWidths::Index> pieces, std::int64_t budget_ns);
    // Queues the names for the settings, applying them at once when their pieces are measured.
    void rebuild_names();
    void apply_names();
    // One slice of measuring for the pending names, or, with none, for the pieces any naming may
    // need (so a later change finds them done).
    void pump_names();
    float min_name_px_ = kMinNameFontPx;            // the smallest legible name, for this display
    float min_small_name_px_ = kMinSmallNameFontPx; // the smallest a black key's name may shrink to
    std::array<std::string, 128> c_names_;          // C markers, empty for notes that are not a C
    float c_marker_px_ = 0.0f;
    float white_name_px_ = 0.0f;
    float black_name_px_ = 0.0f;

    // The blocks that want a name this frame, collected while the blocks are built, their text
    // colours, and what core's plan_block_names made of them (same order). Reserved once, so the
    // frame allocates nothing.
    std::vector<BlockNameIn> name_in_;
    std::vector<SDL_FColor> name_colour_;
    std::vector<BlockNamePlacement> name_out_;
    std::vector<std::array<PieceSlot, 2>> name_slots_; // the textures of each name's pieces
    std::vector<char> name_ready_;                     // every piece has one, so the name draws
    // Flow Mode's grade words of this frame, measured and placed by layout_grades().
    struct GradeFrame {
        std::array<GradeWordIn, kMaxGradeWords> words{};
        std::array<GradeWordOut, kMaxGradeWords> placed{};
        std::array<std::uint8_t, kMaxGradeWords> kind{};
        std::size_t count = 0;
        float pad_y = 0.0f;
    };
    GradeFrame grades_;
    // Played notes whose key is off the left or right edge of the window, for the edge arrows.
    struct EdgeMark {
        int side;     // -1 left, 1 right
        float centre; // y of the block's middle
        float half_h; // half the block's height
        SDL_FColor colour;
    };
    std::vector<EdgeMark> edge_marks_;
    // The finger numbers of this frame's blocks (a drill's), collected while they are built.
    struct FingerMark {
        float cx;   // across the block's middle
        float top;  // the block's top, kept inside the note area
        float width;
        std::uint8_t digit;
    };
    std::vector<FingerMark> fingers_;
    std::map<std::tuple<std::string, int, bool>, TextSlot> name_cache_;

    std::array<TextSlot, 6> grade_slot_; // one per NoteGrade, bold, at grade_font_px()
    // One label of the key: its words, the three pieces of text and where each goes, relative to
    // the label's left edge. Two of them, so a change of key can cross-fade.
    struct KeyLayer {
        KeyLabelParts parts;
        TextSlot tonic;
        TextSlot mode;
        TextSlot count;
        float tonic_x = 0.0f;
        float glyph_x = 0.0f;
        float mode_x = 0.0f;
        float count_x = 0.0f;
        float sig_x = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float glyph_h = 0.0f;
        float sig_h = 0.0f;
        float count_y = 0.0f;
        float hint_cap_top = 0.0f;
        float font_px = 0.0f;
    };
    void measure_key_layer(KeyLayer& layer, float font_px);
    std::array<KeyLayer, 2> key_layers_;
    KeyLabelFade key_fade_;
    Mesh key_glyphs_;
    RectF key_rect_{};      // the placed label, empty when not shown
    bool key_behind_ = false; // a block passes behind it this frame
    TextSlot unsure_slot_;
    ComboDisplay combo_;
    std::int64_t combo_ns_ = 0;
    bool combo_has_clock_ = false;
    float ui_unit_ = 0.0f;          // set_ui_unit
    float combo_clear_y_ = 0.0f;    // set_combo_clear_y
    float combo_clear_shown_ = 0.0f; // what the number is under now: eases to the above
    float pixel_density_ = 1.0f;    // of the window, for the scene's own unit
    RectF combo_rect_{};            // the number's box with its air; empty when not shown
    RectF combo_keep_out_{};
    RectF side_button_{};
    // The digits 0 to 9 at `combo_px_`, bold with an outline, and the widest of them; the number
    // is drawn from these, so nothing is rasterised while it animates.
    std::array<TextSlot, 10> combo_slot_;
    float combo_px_ = 0.0f;
    float combo_digit_w_ = 0.0f;
    TextSlot combo_times_slot_;     // the × before the digits
    float combo_times_w_ = 0.0f;
    Text::VerticalMetrics combo_metrics_;
    float combo_cap_h_ = 0.0f;      // the digits' height, the unit of their slide
    SDL_FRect combo_inner_{};       // the number and icon without the air, centred in combo_rect_
    float combo_icon_w_ = 0.0f;
    Mesh combo_mesh_;               // the fire icon
    ChordLabelView chord_view_;
    std::array<SDL_FColor, 2> chord_colours_{}; // the left and the right hand, for the chord label
    TextSlot prompt_slot_;
};

} // namespace etude::app
