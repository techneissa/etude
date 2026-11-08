// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_check.hpp"

#include "gfx.hpp"
#include "library_bench.hpp"
#include "sections_controller.hpp"
#include "settings_check_rig.hpp"
#include "song_controller.hpp"
#include "song_library.hpp"
#include "song_transport.hpp"
#include "etude/palette.hpp"
#include "etude/screen_layout.hpp"
#include "ui.hpp"
#include "ui/icon_audit.hpp"
#include "ui/input_port.hpp"
#include "ui/preview.hpp"
#include "ui/row_layout.hpp"
#include "ui/settings_catalogue.hpp"
#include "window_icon.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace etude::app {

namespace {

constexpr PanelTab kPages[] = {PanelTab::Notes,    PanelTab::Colours, PanelTab::View,
                               PanelTab::Practice, PanelTab::Input,   PanelTab::Configs,
                               PanelTab::AddSongs, PanelTab::About};
constexpr int kFramesPerPage = 12;
constexpr std::size_t kLibrarySongs = 40; // songs in the library the sweep draws
constexpr Uint32 kFrameMs = 16;

// The pixels of the preview's frame, or empty when they cannot be read.
std::vector<std::uint32_t> preview_pixels(SDL_Renderer* renderer, ui::LivePreview& preview,
                                          const LookSettings& look, double now_s) {
    constexpr int kW = 320;
    constexpr int kH = 120;
    const ui::LivePreview::Rendered out =
        preview.render(look, kW, kH, ui::PreviewLoop::Song, now_s);
    if (out.texture == nullptr) {
        return {};
    }
    SDL_Texture* previous = gfx::target(renderer);
    gfx::set_target(renderer, out.texture);
    SDL_Surface* read = gfx::read_pixels(renderer, nullptr);
    gfx::set_target(renderer, previous);
    if (read == nullptr) {
        return {};
    }
    SDL_Surface* rgba = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(read);
    if (rgba == nullptr) {
        return {};
    }
    std::vector<std::uint32_t> px(static_cast<std::size_t>(rgba->w) *
                                  static_cast<std::size_t>(rgba->h));
    for (int y = 0; y < rgba->h; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(rgba->pixels) +
                          static_cast<std::ptrdiff_t>(y) * rgba->pitch;
        std::memcpy(&px[static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->w)], row,
                    static_cast<std::size_t>(rgba->w) * 4);
    }
    SDL_DestroySurface(rgba);
    return px;
}

std::size_t pixels_apart(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        n += a[i] != b[i] ? 1 : 0;
    }
    return n + (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
}

// The Effects page's preview is drawn by the scene from the look's own colours: the same
// look gives the same pixels, and another palette changes them.
int check_colour_preview(SDL_Renderer* renderer) {
    ui::LivePreview preview(renderer);
    LookSettings classic;
    classic.color_scheme = ColorScheme::ByPitchClass;
    LookSettings vivid = classic;
    apply_palette(vivid, Palette::Vivid);
    constexpr double kStart = 100.0;
    constexpr double kAt = kStart + 2.6; // several notes falling, a few keys down
    preview_pixels(renderer, preview, classic, kStart);
    const auto first = preview_pixels(renderer, preview, classic, kAt);
    const auto again = preview_pixels(renderer, preview, classic, kAt);
    const auto other = preview_pixels(renderer, preview, vivid, kAt);
    if (first.empty() || again.empty() || other.empty()) {
        std::printf("colour preview: could not read the preview's pixels\n");
        return 1;
    }
    const std::size_t same = pixels_apart(first, again);
    const std::size_t changed = pixels_apart(first, other);
    // At least 1% of the strip: the falling notes and the lit keys take their colour from it.
    const std::size_t needed = first.size() / 100;
    if (same != 0 || changed < needed) {
        std::printf("colour preview: the same look changed %zu pixels, the Vivid palette %zu (needs "
                    "%zu)\n",
                    same, changed, needed);
        return 1;
    }
    std::printf("colour preview: ok, the Vivid palette changes %zu of %zu pixels, the same look "
                "none\n",
                changed, first.size());
    return 0;
}

// A mouse event for the window: a move to (x, y), a press or release of the left button there, or
// (for SDL_EVENT_MOUSE_WHEEL) `y` clicks of the wheel where the mouse last was (`x` unused).
SDL_Event mouse_event(SDL_Window* window, SDL_EventType type, float x, float y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        e.motion.windowID = SDL_GetWindowID(window);
        e.motion.x = x;
        e.motion.y = y;
    } else if (type == SDL_EVENT_MOUSE_WHEEL) {
        e.wheel.windowID = SDL_GetWindowID(window);
        e.wheel.y = y;
    } else {
        e.button.windowID = SDL_GetWindowID(window);
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
    }
    return e;
}

// The bar and the song library, drawn so their settings are seen too.
void check_bar_and_library(const CheckRig& rig) {
    Ui& ui = rig.ui;
    UiModel& model = rig.model;
    const auto& draw = rig.draw;
    const auto& mouse = rig.mouse;
    // The bar, pinned so it shows, in each style, with the tempo popover open: where the
    // metronome switch and the count-in live.
    ui.close_panel();
    model.app.bar_pinned = true;
    for (const BarStyle style : {BarStyle::Strip, BarStyle::Groups, BarStyle::Dock}) {
        model.app.bar_style = style;
        for (int i = 0; i < kFramesPerPage; ++i) {
            draw();
        }
        for (const ui::audit::Note& n : ui::audit::notes()) {
            if (n.name == "##tempo") {
                const float x = (n.min.x + n.max.x) * 0.5f;
                const float y = (n.min.y + n.max.y) * 0.5f;
                mouse(SDL_EVENT_MOUSE_MOTION, x, y);
                mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
                draw();
                mouse(SDL_EVENT_MOUSE_BUTTON_UP, x, y);
                mouse(SDL_EVENT_MOUSE_MOTION, 1.0f, 1.0f);
                break;
            }
        }
        for (int i = 0; i < kFramesPerPage; ++i) {
            draw();
        }
    }
    // The song library, over a made-up one: its chips hold the order it shows the songs in.
    model.app.bar_pinned = false;
    BenchLibrary songs_shown(kLibrarySongs);
    ui.set_library(&songs_shown);
    ui.open_library();
    for (int i = 0; i < kFramesPerPage; ++i) {
        draw();
    }
    ui.close_library();
    draw();
    ui.set_library(nullptr);
}

} // namespace

int run_settings_check(int width, int height, const std::string& song_path) {
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1"); // no Dock icon, no focus
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!gfx::create_window_and_renderer("Etude settings check", width, height, SDL_WINDOW_HIDDEN,
                                         RendererPath::Basic, &window, &renderer)) {
        std::fprintf(stderr, "window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    set_window_icon(window);
    SDL_Texture* target = gfx::create_texture(renderer, SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_TARGET, width, height);
    if (target == nullptr) {
        std::fprintf(stderr, "render target failed: %s\n", SDL_GetError());
        gfx::destroy_renderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    int code = 0;
    {
        UiModel model;
        SongController songs;
        SongTransport song(songs);
        ui::NullInputPort input;
        SectionLibrary no_sections;
        SectionsController sections(songs, no_sections);
        if (!song_path.empty()) {
            SongFileLoad loaded = load_song_file(song_path);
            if (!loaded.ok) {
                std::fprintf(stderr, "settings check: %s: %s\n", song_path.c_str(),
                             loaded.error.c_str());
                code = 1;
            } else {
                songs.load(std::move(loaded.song), static_cast<std::int64_t>(SDL_GetTicksNS()));
            }
        }
        Ui ui(window, renderer);
        ui::catalogue_sweep::start();
        ui::audit::start(); // the widgets name their buttons: where the tempo chip is
        std::int64_t frame = 0;
        std::vector<SDL_Event> inbox; // events for the next frame
        const auto draw = [&] {
            gfx::set_target(renderer, target);
            gfx::clear(renderer, 0, 0, 0, 255);
            SDL_Event drain;
            while (SDL_PollEvent(&drain)) {
            }
            const auto now_ns = static_cast<std::int64_t>(SDL_GetTicksNS());
            for (const SDL_Event& e : inbox) {
                ui.process_event(e, now_ns);
            }
            inbox.clear();
            ui::audit::begin_frame(false);
            song.update(now_ns);
            sections.update(now_ns);
            UiFrameInfo info;
            info.width_px = static_cast<float>(width);
            info.height_px = static_cast<float>(height);
            info.px_per_mm = kDefaultPxPerMm;
            info.layout =
                compute_layout(info.width_px, info.height_px, info.px_per_mm, model.look,
                               {model.app.bar_pinned, model.app.bar_style, song.view().has_song});
            info.now_ns = now_ns;
            ui.draw(model, song, sections, input, info);
            SDL_DestroySurface(gfx::read_pixels(renderer, nullptr)); // wait for the frame
            SDL_Delay(kFrameMs);
            ++frame;
        };
        const auto mouse = [&](SDL_EventType type, float x, float y) {
            inbox.push_back(mouse_event(window, type, x, y));
        };
        // The pages, twice: the keyboard on Auto, then on Manual, which shows its key count. The
        // column probe reads three frames of each page: the first, the one after it and the last.
        // A page measures its labels in its first frame, which is drawn hidden, so the first
        // frame anyone sees must already have every control where the settled page has it.
        std::vector<std::string> column_problems;
        ui::column_probe::Stats stats;
        int first_frames_match = 0;
        const bool list_columns = std::getenv("SETTINGS_CHECK_COLUMNS") != nullptr;
        std::size_t rows_probed = 0;
        const auto visible_rows = [](const std::vector<ui::column_probe::Row>& rows) {
            std::vector<ui::column_probe::Row> out;
            for (const ui::column_probe::Row& r : rows) {
                if (r.visible) {
                    out.push_back(r);
                }
            }
            return out;
        };
        const auto has_page_rows = [](const std::vector<ui::column_probe::Row>& rows) {
            for (const ui::column_probe::Row& r : rows) {
                if (r.scope > 0) {
                    return true; // scope 0 is the sheet's header, drawn from the first frame
                }
            }
            return false;
        };
        for (const KeyboardChoice choice : {KeyboardChoice::Auto, KeyboardChoice::Manual}) {
            model.session.keyboard = choice;
            for (const PanelTab tab : kPages) {
                ui.open_panel(tab);
                std::vector<ui::column_probe::Row> first_rows;
                std::vector<ui::column_probe::Row> last_rows;
                for (int i = 0; i < kFramesPerPage; ++i) {
                    const bool probe = i <= 1 || i == kFramesPerPage - 1;
                    if (probe) {
                        ui::column_probe::start();
                    }
                    draw();
                    if (!probe) {
                        continue;
                    }
                    std::vector<ui::column_probe::Row> rows =
                        visible_rows(ui::column_probe::stop());
                    if (i == kFramesPerPage - 1) {
                        last_rows = rows;
                    } else if (!has_page_rows(first_rows)) {
                        first_rows = std::move(rows); // the first frame with a row anyone sees
                    }
                }
                rows_probed += last_rows.size();
                if (list_columns) {
                    for (const ui::column_probe::Row& r : last_rows) {
                        std::printf("columns: page %d column %d %s%s%s x %.1f to %.1f\n", r.scope,
                                    r.column, ui::column_probe::kind_name(r.kind),
                                    r.stacked ? " stacked" : "", r.radio ? " radio" : "", r.span.x0,
                                    r.span.x1);
                    }
                }
                for (const auto* rows : {&first_rows, &last_rows}) {
                    for (std::string& p : ui::column_probe::check(*rows, 0.01f, &stats)) {
                        column_problems.push_back(
                            (rows == &first_rows ? "on the first frame: " : "") + p);
                    }
                }
                // The first visible frame is the settled one: no label side changes width
                // afterwards (the sheet's own opening slide moves the whole column a little, so
                // the spans themselves are not compared).
                bool same = first_rows.size() == last_rows.size();
                for (std::size_t i = 0; same && i < first_rows.size(); ++i) {
                    same = first_rows[i].column == last_rows[i].column &&
                           first_rows[i].kind == last_rows[i].kind &&
                           first_rows[i].zone_measured == last_rows[i].zone_measured &&
                           (!last_rows[i].zone_measured ||
                            std::fabs(first_rows[i].zone - last_rows[i].zone) <= 0.5f);
                }
                if (same) {
                    ++first_frames_match;
                } else {
                    if (list_columns) {
                        for (const auto* rows : {&first_rows, &last_rows}) {
                            std::printf("%s:", rows == &first_rows ? "first" : "last");
                            for (const ui::column_probe::Row& r : *rows) {
                                std::printf(" [p%d c%d %s %.1f..%.1f z%.1f]", r.scope, r.column,
                                            ui::column_probe::kind_name(r.kind), r.span.x0,
                                            r.span.x1, r.zone);
                            }
                            std::printf("\n");
                        }
                    }
                    column_problems.push_back(
                        "page " + std::to_string(last_rows.empty() ? 0 : last_rows.back().scope) +
                        ": its label side changes width after its first visible frame");
                }
            }
        }
        const CheckRig rig{ui, model, draw, mouse};
        check_bar_and_library(rig);
        code |= check_taps(rig) != 0 ? 1 : 0;
        ui::audit::stop();
        const std::vector<ui::Drawn> drawn = ui::catalogue_sweep::stop();
        std::vector<std::string> problems = ui::check_drawn(drawn);
        for (std::string& p : column_problems) {
            problems.push_back("controls out of line: " + p);
        }
        if (rows_probed == 0) {
            problems.push_back("no row reported where its control went");
        }
        if (stats.comparisons == 0) {
            problems.push_back("no control was compared with another in its column");
        }
        if (stats.radio_lists == 0) {
            problems.push_back("no list of choices was drawn, so none was checked");
        }
        for (const std::string& p : problems) {
            std::printf("settings check: %s\n", p.c_str());
        }
        if (check_colour_preview(renderer) != 0) {
            code = 1;
        }
        if (problems.empty() && !drawn.empty()) {
            std::printf("settings check: ok, %zu settings, each on its one page, the bar or the library (%zu "
                        "rows drawn over %lld frames), %zu controls in line in their columns (%d "
                        "comparisons, %d radio lists, %d pages whose first visible frame is "
                        "their settled one)\n",
                        ui::catalogue().size(), drawn.size(), static_cast<long long>(frame),
                        rows_probed, stats.comparisons, stats.radio_lists, first_frames_match);
        } else {
            code = 1;
            if (drawn.empty()) {
                std::printf("settings check: no row was drawn: the sheet did not open\n");
            }
        }
    }
    gfx::destroy_texture(target);
    gfx::destroy_renderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}

} // namespace etude::app
