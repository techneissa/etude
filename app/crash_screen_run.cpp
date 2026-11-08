// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash_screen_run.hpp"

#include "crash_handler.hpp"
#include "crash_notice.hpp"
#include "crash_screen.hpp"
#include "gfx.hpp"
#include "log_ring.hpp"
#include "ui.hpp"
#include "update_service.hpp"
#include "window_run.hpp"
#include "window_state.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

namespace etude::app {
namespace {

// What the report's sheet reads, from the notice (as the app's own notice fills it).
ui::CrashView view_of(const CrashNotice& notice, bool browser) {
    ui::CrashView view;
    view.report = notice.text;
    view.qr_url = notice.qr_url;
    view.opens_browser = browser;
    return view;
}

// The report Send report shows: a made-up one for the shots, else the newest crash report, else (the
// update's data was not put back and nothing crashed) a short one that says so.
std::optional<CrashNotice> report_for(const WindowOptions& opt, const std::filesystem::path& data_dir,
                                      const CrashScreenFacts& facts, bool& browser) {
    browser = false;
#if defined(__APPLE__)
    browser = true;
#endif
    if (!opt.crash_demo.empty()) {
        browser = opt.crash_demo == "mac";
        return demo_crash_notice(browser);
    }
    std::filesystem::path install;
    if (const char* base = SDL_GetBasePath()) {
        install = install_dir_of(base);
    }
    const etude::ScrubRules rules = scrub_rules_for(data_dir, install);
    if (std::optional<CrashNotice> latest = latest_crash_notice(data_dir, rules)) {
        return latest;
    }
    if (!facts.data_error.empty()) {
        return restore_failure_notice(facts.data_error, rules);
    }
    return std::nullopt;
}

CrashScreenFacts facts_for(const WindowOptions& opt) {
    if (!opt.crash_screen_reason.empty()) {
        // "demo" is the shots' and the audit's own reason (a command line cannot carry a sentence there).
        return {opt.crash_screen_reason == "demo"
                    ? "the backup of 1.1.0 is not complete (history.db: no space left on device)"
                    : opt.crash_screen_reason};
    }
    if (!opt.crash_demo.empty()) {
        return {}; // the shots: a crash, with its made-up report
    }
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? CrashScreenFacts{} : read_crash_screen_facts(update_dir_for(exe));
}

// A developer run (a script, the audit, a headless window, a demo) leaves no file behind.
bool leaves_nothing(const WindowOptions& opt) {
    return opt.headless || opt.script.active() || opt.audit.active() || !opt.crash_demo.empty() ||
           !opt.crash_screen_reason.empty();
}

// The headless target: as big as the window in pixels, drawn into and read back for --shot.
struct HeadlessTarget {
    SDL_Texture* texture = nullptr;
    int w = 0;
    int h = 0;
    void ensure(SDL_Window* window, SDL_Renderer* renderer) {
        int pw = 0;
        int ph = 0;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        if (texture != nullptr && pw == w && ph == h) {
            return;
        }
        gfx::set_target(renderer, nullptr);
        gfx::destroy_texture(texture);
        texture = gfx::create_texture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_TARGET,
                                      std::max(1, pw), std::max(1, ph));
        w = pw;
        h = ph;
        gfx::set_target(renderer, texture);
    }
    void release(SDL_Renderer* renderer) {
        gfx::set_target(renderer, nullptr);
        gfx::destroy_texture(texture);
        texture = nullptr;
    }
};

// The frame at which --crash-screen-press taps its button: the screen has been drawn a while by then.
constexpr long kPressAtFrame = 30;

struct Run {
    WindowOptions& opt;
    WindowHandles handles;
    ui::CrashScreenView view;
    std::optional<CrashNotice> report;
    ui::CrashView report_view;
    CrashLeave leave = CrashLeave::Stay;
    std::filesystem::path data_dir;
    DisplayPanel panel;
    bool running = true;
    long frame = 0;

    explicit Run(WindowOptions& o) : opt(o) {}

    UiFrameInfo frame_info() {
        UiFrameInfo info;
        int out_w = 0;
        int out_h = 0;
        gfx::output_size(handles.renderer, &out_w, &out_h);
        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSize(handles.window, &win_w, &win_h);
        info.width_px = static_cast<float>(out_w);
        info.height_px = static_cast<float>(out_h);
        info.pixel_density = win_w > 0 ? static_cast<float>(out_w) / static_cast<float>(win_w) : 1.0f;
        info.px_per_mm = panel.px_per_mm(info.width_px, info.height_px, info.pixel_density, 0, opt.audit.ui_scale());
        info.real_px_per_mm = panel.px_per_mm(info.width_px, info.height_px, info.pixel_density, 0, 1.0f);
        set_display_px_per_mm(info.px_per_mm);
        info.now_ns = static_cast<std::int64_t>(SDL_GetTicksNS());
        return info;
    }

    // --crash-screen-press: the tap of a button this screen offers, answered as the player's tap is.
    void press_named(const std::string& name) {
        const std::optional<ui::CrashButton> button = crash_button_named(name);
        const bool offered = button && std::find(view.buttons.begin(), view.buttons.end(), *button) != view.buttons.end();
        log_printf("etude: crash screen: %s %s\n", offered ? "pressing" : "no such button to press:", name.c_str());
        if (offered) {
            ui::CrashScreenTap tap;
            tap.button = button;
            answer(tap);
        }
    }

    void answer(const ui::CrashScreenTap& tap) {
        if (tap.report == ui::CrashChoice::OpenUrl && report) {
            SDL_OpenURL(report->url.c_str());
        }
        if (!tap.button) {
            return;
        }
        leave = crash_leave_for(*tap.button);
        if (leaves_screen(leave)) {
            if (!leaves_nothing(opt)) {
                leave_for_next_start(data_dir, leave);
            }
            running = false;
        }
    }
};

} // namespace

int run_crash_screen(WindowOptions& opt) {
    Run run(opt);
    if (!open_window(opt, run.handles)) {
        return 1;
    }
    run.data_dir = settings_directory(opt.settings_dir);
    run.panel.resolve(run.handles.window, opt.panel_mm, opt.headless);
    const CrashScreenFacts facts = facts_for(opt);
    run.view = crash_screen_view(facts);
    bool browser = false;
    run.report = report_for(opt, run.data_dir, facts, browser);
    if (run.report) {
        run.report_view = view_of(*run.report, browser);
    }
    int code = 1;
    {
        Ui ui(run.handles.window, run.handles.renderer);
        HeadlessTarget target;
        while (run.running) {
            opt.script.before_events(run.handles.window, run.frame);
            opt.audit.before_frame(run.handles.window, run.frame);
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ui.process_event(event, static_cast<std::int64_t>(SDL_GetTicksNS()));
                if (event.type == SDL_EVENT_QUIT) {
                    run.running = false; // stopped from outside: nothing restarts the app
                }
            }
            if (!run.running) {
                break;
            }
            if (opt.headless) {
                target.ensure(run.handles.window, run.handles.renderer);
            }
            gfx::clear(run.handles.renderer, 0, 0, 0, 255);
            gfx::flush(run.handles.renderer);
            const ui::CrashScreenTap tap =
                ui.draw_crash_screen(run.view, run.report ? &run.report_view : nullptr, run.frame_info());
            if (opt.script.after_draw(run.handles.renderer, run.frame) ||
                opt.audit.after_draw(run.handles.renderer, run.frame)) {
                run.running = false;
                code = 0;
            }
            if (opt.headless) {
                gfx::flush(run.handles.renderer);
            } else if (!gfx::present(run.handles.renderer)) {
                SDL_Delay(16);
            }
            if (!run.handles.vsync) {
                SDL_DelayPrecise(static_cast<Uint64>(run.handles.frame_ns));
            }
            ++run.frame;
            run.answer(tap);
            if (run.frame == kPressAtFrame && !opt.crash_screen_press.empty()) {
                run.press_named(opt.crash_screen_press);
            }
            if (leaves_screen(run.leave)) {
                code = 0;
            }
        }
        target.release(run.handles.renderer);
    }
    gfx::destroy_renderer(run.handles.renderer);
    SDL_DestroyWindow(run.handles.window);
    SDL_Quit();
    return opt.script.shot_failed() ? 1 : code;
}

} // namespace etude::app
