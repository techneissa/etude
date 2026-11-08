// SPDX-License-Identifier: GPL-3.0-or-later
#include "window_options.hpp"

#include "cli_values.hpp"
#include "gfx.hpp"
#include "persistence.hpp"

#include "etude/data_folder.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>

namespace etude::app {

// Seconds east of UTC here, now (for the day a run belongs to).
std::int32_t local_utc_offset_s(std::time_t t) {
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
    long bias = 0;
    _get_timezone(&bias);
    return static_cast<std::int32_t>(-bias + (local.tm_isdst > 0 ? 3600 : 0));
#else
    localtime_r(&t, &local);
    return static_cast<std::int32_t>(local.tm_gmtoff);
#endif
}

void apply_look_options(const WindowOptions& o, etude::LookSettings& look) {
    if (o.key_size) {
        look.key_size = *o.key_size;
    }
    if (o.show_typing_keys) {
        look.show_typing_keys = true;
    }
    if (o.octaves != nullptr) {
        look.octave_numbering = std::strcmp(o.octaves, "yamaha") == 0
                                    ? etude::OctaveNumbering::Yamaha
                                    : etude::OctaveNumbering::Scientific;
    }
    if (o.names != nullptr) {
        // One switch for both places: names on the blocks and on the keys.
        look.name_mode = std::strcmp(o.names, "off") == 0       ? etude::NameMode::Off
                         : std::strcmp(o.names, "solfege") == 0 ? etude::NameMode::Solfege
                                                                : etude::NameMode::Letters;
        look.names_on_blocks = true;
        look.names_on_keys = true;
    }
    if (o.style != nullptr) {
        etude::parse_note_style(o.style, &look.note_style);
    }
    if (o.glow) {
        look.block_glow = *o.glow;
    }
    if (o.beat_lines) {
        look.beat_lines = *o.beat_lines;
    }
    if (o.blocks) {
        look.block_layout = *o.blocks;
    }
    if (o.colors != nullptr) {
        parse_colors(o.colors, &look.color_scheme);
    }
    if (o.high_contrast) {
        look.high_contrast = true;
    }
    if (o.hit_effect) {
        look.hit_effect = *o.hit_effect;
    }
}

// Where the settings file lives: the --settings-dir option, else SDL's per-user folder.
std::filesystem::path settings_directory(const std::string& option) {
    if (!option.empty()) {
        return option;
    }
    char* pref = SDL_GetPrefPath("", etude::kAppDataFolderName);
    std::filesystem::path dir = pref != nullptr ? pref : ".";
    SDL_free(pref);
    return dir;
}

// The renderer for this run: --renderer, else the one the settings file asks for.
etude::RendererPath renderer_for(const WindowOptions& opt) {
    return opt.renderer ? *opt.renderer
                        : etude::app::saved_renderer(settings_directory(opt.settings_dir));
}

} // namespace etude::app
