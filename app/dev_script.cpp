// SPDX-License-Identifier: GPL-3.0-or-later
#include "dev_script.hpp"

#include "frame_log.hpp"
#include "gfx.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace etude::app {

namespace {

// --info: where the window is and how big.
void print_window_info(SDL_Window* window) {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int pw = 0;
    int ph = 0;
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    std::printf("window at %d,%d size %dx%d points, %dx%d pixels\n", x, y, w, h, pw, ph);
    std::fflush(stdout);
}

} // namespace

// One or two fingers go down at `from`, move in `moves` equal steps to `to`, and lift `hold`
// frames after the last move.
void DevScript::finger_path(long frame, std::initializer_list<std::pair<float, float>> from,
                            std::initializer_list<std::pair<float, float>> to, int moves,
                            long hold) {
    using Kind = Step::Kind;
    const auto a = std::vector<std::pair<float, float>>(from);
    const auto b = std::vector<std::pair<float, float>>(to);
    for (std::size_t f = 0; f < a.size(); ++f) {
        steps_.push_back({Kind::FingerDown, frame, a[f].first, a[f].second, {}, static_cast<int>(f) + 1});
    }
    for (int i = 1; i <= moves; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(moves);
        for (std::size_t f = 0; f < a.size(); ++f) {
            steps_.push_back({Kind::FingerMove, frame + i, a[f].first + (b[f].first - a[f].first) * t,
                              a[f].second + (b[f].second - a[f].second) * t, {},
                              static_cast<int>(f) + 1});
        }
    }
    for (std::size_t f = 0; f < a.size(); ++f) {
        steps_.push_back({Kind::FingerUp, frame + moves + hold, b[f].first, b[f].second, {},
                          static_cast<int>(f) + 1});
    }
}

// --shots FROM:TO:DIR and --shot: PNGs of the frame, or of every frame from FROM to TO.
bool DevScript::parse_shots(const char* option, long frame, const char* rest) {
    using Kind = Step::Kind;
    if (std::strcmp(option, "--shots") == 0) {
        // FROM:TO:DIR: a PNG of every frame from FROM to TO, DIR/f0000.png, f0001.png ...
        long to = 0;
        int n = 0;
        if (std::sscanf(rest, "%ld:%n", &to, &n) != 1 || to < frame || rest[n] == '\0') {
            return false;
        }
        for (long f = frame; f <= to; ++f) {
            char name[32];
            std::snprintf(name, sizeof name, "/f%04ld.png", f - frame);
            steps_.push_back({Kind::Shot, f, 0, 0, std::string(rest + n) + name});
        }
        return true;
    }
    if (std::strcmp(option, "--shot") == 0) {
        if (rest[0] == '\0') {
            return false;
        }
        steps_.push_back({Kind::Shot, frame, 0, 0, rest});
        return true;
    }
    return false;
}

// The pretend keyboards' options; `rest` is what follows the frame.
bool DevScript::parse_keyboard(const char* option, long frame, const char* rest) {
    using Kind = DevKeyboardEvent::Kind;
    const auto add = [&](long at, Kind kind, std::string name, int pitch) {
        steps_.push_back({Step::Kind::Keyboard, at, static_cast<float>(pitch), 0.0f, std::move(name),
                          static_cast<int>(kind)});
    };
    if (std::strcmp(option, "--device-check") == 0) {
        add(frame, Kind::OpenCheck, {}, 0);
        return true;
    }
    if (std::strcmp(option, "--fake-keyboard") == 0 || std::strcmp(option, "--fake-unplug") == 0) {
        if (rest[0] == '\0') {
            return false;
        }
        add(frame, std::strcmp(option, "--fake-keyboard") == 0 ? Kind::Plug : Kind::Unplug, rest, 0);
        return true;
    }
    // --fake-note: NAME:PITCH[:N] (the name holds no colon).
    long pitch = 0;
    long hold = 10;
    char name[128];
    int n = 0;
    int got = std::sscanf(rest, "%127[^:]:%ld:%ld%n", name, &pitch, &hold, &n);
    if (got == 2) {
        got = std::sscanf(rest, "%127[^:]:%ld%n", name, &pitch, &n);
    }
    if ((got != 2 && got != 3) || rest[n] != '\0' || pitch < 0 || pitch > 127 || hold < 1) {
        return false;
    }
    add(frame, Kind::NoteOn, name, static_cast<int>(pitch));
    add(frame + hold, Kind::NoteOff, name, static_cast<int>(pitch));
    return true;
}

// --resize: max, full and restore, or WxH; `rest` follows the frame.
bool DevScript::parse_resize(long frame, const char* rest) {
    using Kind = Step::Kind;
    // max, full and restore: the window maximised, made full screen, or put back, as the OS's
    // own buttons do (with their animation where the OS has one).
    for (const auto& [word, code] : {std::pair{"max", kResizeMaximise},
                                     std::pair{"full", kResizeFullscreen},
                                     std::pair{"restore", kResizeRestore}}) {
        if (std::strcmp(rest, word) == 0) {
            steps_.push_back({Kind::Resize, frame, code, 0, {}});
            return true;
        }
    }
    float w = 0;
    float h = 0;
    int n = 0;
    if (std::sscanf(rest, "%fx%f%n", &w, &h, &n) != 2 || rest[n] != '\0' || w < 1 || h < 1) {
        return false;
    }
    steps_.push_back({Kind::Resize, frame, w, h, {}});
    return true;
}

// --simulate-device-lost: FRAME[:to=PATH][:refuse=PATH[,PATH]][:present]; `rest` follows the frame.
bool DevScript::parse_device_lost(long frame, const char* rest) {
    DeviceLossSim loss;
    std::string_view options(rest);
    while (!options.empty()) {
        if (options.front() != ':') {
            return false;
        }
        options.remove_prefix(1);
        const std::string_view option = options.substr(0, options.find(':'));
        options.remove_prefix(option.size());
        etude::RendererPath path = etude::RendererPath::Basic;
        if (option == "present") {
            loss.by_present = true;
        } else if (option.rfind("to=", 0) == 0 &&
                   etude::parse_renderer_path(option.substr(3), &path)) {
            loss.to = path;
        } else if (option.rfind("refuse=", 0) == 0) {
            std::string_view list = option.substr(7);
            while (!list.empty()) {
                const std::string_view word = list.substr(0, list.find(','));
                if (!etude::parse_renderer_path(word, &path)) {
                    return false;
                }
                loss.refuse |= etude::renderer_path_bit(path);
                list.remove_prefix(std::min(list.size(), word.size() + 1));
            }
        } else {
            return false;
        }
    }
    Step step;
    step.kind = Step::Kind::DeviceLost;
    step.frame = frame;
    step.finger = static_cast<int>(losses_.size());
    losses_.push_back(loss);
    steps_.push_back(std::move(step));
    return true;
}

bool DevScript::parse(const char* option, const char* value) {
    using Kind = Step::Kind;
    long frame = 0;
    int consumed = 0;
    if (std::strcmp(option, "--info") == 0) {
        if (std::sscanf(value, "%ld%n", &frame, &consumed) != 1 || value[consumed] != '\0' ||
            frame < 0) {
            return false;
        }
        steps_.push_back({Kind::Info, frame, 0, 0, {}});
        return true;
    }
    if (std::strcmp(option, "--quit-frame") == 0) {
        return std::sscanf(value, "%ld%n", &quit_frame_, &consumed) == 1 && value[consumed] == '\0';
    }
    if (std::strcmp(option, "--simulate-device-lost") == 0) {
        return std::sscanf(value, "%ld%n", &frame, &consumed) == 1 && frame >= 0 &&
               parse_device_lost(frame, value + consumed);
    }
    if (std::strcmp(option, "--device-check") == 0) {
        return std::sscanf(value, "%ld%n", &frame, &consumed) == 1 && value[consumed] == '\0' &&
               frame >= 0 && parse_keyboard(option, frame, "");
    }
    if (std::sscanf(value, "%ld:%n", &frame, &consumed) != 1 || consumed == 0 || frame < 0) {
        return false;
    }
    const char* rest = value + consumed;
    if (std::strcmp(option, "--tap") == 0) {
        float x = 0;
        float y = 0;
        int n = 0;
        if (std::sscanf(rest, "%f,%f%n", &x, &y, &n) != 2 || rest[n] != '\0') {
            return false;
        }
        steps_.push_back({Kind::Move, frame, x, y, {}});
        steps_.push_back({Kind::Down, frame + 1, x, y, {}});
        steps_.push_back({Kind::Up, frame + 2, x, y, {}});
        return true;
    }
    if (std::strcmp(option, "--drag") == 0) {
        float x = 0;
        float y = 0;
        float x2 = 0;
        float y2 = 0;
        int n = 0;
        if (std::sscanf(rest, "%f,%f:%f,%f%n", &x, &y, &x2, &y2, &n) != 4 || rest[n] != '\0') {
            return false;
        }
        constexpr int kSteps = 8;
        steps_.push_back({Kind::Move, frame, x, y, {}});
        steps_.push_back({Kind::Down, frame + 1, x, y, {}});
        for (int i = 1; i <= kSteps; ++i) {
            const float t = static_cast<float>(i) / kSteps;
            steps_.push_back({Kind::Move, frame + 1 + i, x + (x2 - x) * t, y + (y2 - y) * t, {}});
        }
        steps_.push_back({Kind::Up, frame + 2 + kSteps, x2, y2, {}});
        return true;
    }
    if (std::strcmp(option, "--touch") == 0) {
        float x = 0;
        float y = 0;
        int n = 0;
        if (std::sscanf(rest, "%f,%f%n", &x, &y, &n) != 2 || rest[n] != '\0') {
            return false;
        }
        finger_path(frame, {{x, y}}, {{x, y}}, 0, 1);
        return true;
    }
    if (std::strcmp(option, "--fdrag") == 0) {
        float x = 0;
        float y = 0;
        float x2 = 0;
        float y2 = 0;
        long hold = 1;
        int n = 0;
        if (std::sscanf(rest, "%f,%f:%f,%f%n", &x, &y, &x2, &y2, &n) != 4) {
            return false;
        }
        if (rest[n] == ':' && (std::sscanf(rest + n + 1, "%ld%n", &hold, &consumed) != 1 ||
                               rest[n + 1 + consumed] != '\0' || hold < 1)) {
            return false;
        }
        if (rest[n] != '\0' && rest[n] != ':') {
            return false;
        }
        finger_path(frame, {{x, y}}, {{x2, y2}}, 8, hold);
        return true;
    }
    if (std::strcmp(option, "--fholddrag") == 0 || std::strcmp(option, "--rdrag") == 0) {
        // A finger held still for HOLD frames, then dragged to X2,Y2 in 8 steps and lifted (a long
        // press that drags); --rdrag: the mouse's right button pressed, dragged and released.
        float x = 0;
        float y = 0;
        float x2 = 0;
        float y2 = 0;
        long hold = 1;
        int n = 0;
        if (std::sscanf(rest, "%f,%f:%f,%f%n", &x, &y, &x2, &y2, &n) != 4) {
            return false;
        }
        if (rest[n] == ':' && (std::sscanf(rest + n + 1, "%ld%n", &hold, &consumed) != 1 ||
                               rest[n + 1 + consumed] != '\0' || hold < 1)) {
            return false;
        }
        if (rest[n] != '\0' && rest[n] != ':') {
            return false;
        }
        constexpr int kSteps = 8;
        const bool finger = option[2] == 'f';
        const Kind down = finger ? Kind::FingerDown : Kind::Down;
        const Kind move = finger ? Kind::FingerMove : Kind::Move;
        const Kind up = finger ? Kind::FingerUp : Kind::Up;
        const int which = finger ? 1 : kRightButton;
        if (!finger) {
            steps_.push_back({Kind::Move, frame, x, y, {}});
        }
        steps_.push_back({down, frame + 1, x, y, {}, which});
        for (int i = 1; i <= kSteps; ++i) {
            const float t = static_cast<float>(i) / kSteps;
            steps_.push_back({move, frame + 1 + hold + i, x + (x2 - x) * t, y + (y2 - y) * t, {}, which});
        }
        steps_.push_back({up, frame + 3 + hold + kSteps, x2, y2, {}, which});
        return true;
    }
    if (std::strcmp(option, "--fhold") == 0) {
        float x = 0;
        float y = 0;
        long hold = 0;
        int n = 0;
        if (std::sscanf(rest, "%f,%f:%ld%n", &x, &y, &hold, &n) != 3 || rest[n] != '\0' ||
            hold < 1) {
            return false;
        }
        finger_path(frame, {{x, y}}, {{x, y}}, 0, hold);
        return true;
    }
    if (std::strcmp(option, "--pinch") == 0) {
        float p[8] = {};
        long hold = 1;
        int n = 0;
        if (std::sscanf(rest, "%f,%f:%f,%f:%f,%f:%f,%f%n", &p[0], &p[1], &p[2], &p[3], &p[4], &p[5],
                        &p[6], &p[7], &n) != 8) {
            return false;
        }
        if (rest[n] == ':' && (std::sscanf(rest + n + 1, "%ld%n", &hold, &consumed) != 1 ||
                               rest[n + 1 + consumed] != '\0' || hold < 1)) {
            return false;
        }
        if (rest[n] != '\0' && rest[n] != ':') {
            return false;
        }
        finger_path(frame, {{p[0], p[1]}, {p[2], p[3]}}, {{p[4], p[5]}, {p[6], p[7]}}, 8, hold);
        return true;
    }
    if (std::strcmp(option, "--move") == 0) {
        float x = 0;
        float y = 0;
        int n = 0;
        if (std::sscanf(rest, "%f,%f%n", &x, &y, &n) != 2 || rest[n] != '\0') {
            return false;
        }
        steps_.push_back({Kind::Move, frame, x, y, {}});
        return true;
    }
    if (std::strcmp(option, "--key") == 0) {
        const char* colon = std::strchr(rest, ':');
        const std::string name = colon != nullptr ? std::string(rest, colon) : std::string(rest);
        long hold = 3;
        if (colon != nullptr && (std::sscanf(colon + 1, "%ld%n", &hold, &consumed) != 1 ||
                                 colon[1 + consumed] != '\0' || hold < 1)) {
            return false;
        }
        if (SDL_GetScancodeFromName(name.c_str()) == SDL_SCANCODE_UNKNOWN) {
            return false;
        }
        steps_.push_back({Kind::KeyDown, frame, 0, 0, name});
        steps_.push_back({Kind::KeyUp, frame + hold, 0, 0, name});
        return true;
    }
    if (std::strcmp(option, "--press") == 0) {
        float x = 0;
        float y = 0;
        long hold = 3;
        int n = 0;
        const int got = std::sscanf(rest, "%f,%f%n", &x, &y, &n);
        if (got != 2) {
            return false;
        }
        if (rest[n] == ':' && (std::sscanf(rest + n + 1, "%ld%n", &hold, &consumed) != 1 ||
                               rest[n + 1 + consumed] != '\0' || hold < 1)) {
            return false;
        }
        if (rest[n] != '\0' && rest[n] != ':') {
            return false;
        }
        steps_.push_back({Kind::Move, frame, x, y, {}});
        steps_.push_back({Kind::Down, frame + 1, x, y, {}});
        steps_.push_back({Kind::Up, frame + 1 + hold, x, y, {}});
        return true;
    }
    if (std::strcmp(option, "--resize") == 0) {
        return parse_resize(frame, rest);
    }
    if (std::strcmp(option, "--type") == 0) {
        steps_.push_back({Kind::Text, frame, 0, 0, rest});
        return true;
    }
    if (std::strcmp(option, "--fake-keyboard") == 0 || std::strcmp(option, "--fake-unplug") == 0 ||
        std::strcmp(option, "--fake-note") == 0) {
        return parse_keyboard(option, frame, rest);
    }
    return parse_shots(option, frame, rest);
}

// A scripted finger's event, and for the first finger the mouse events SDL makes from a touch.
void DevScript::push_finger(const Step& s, SDL_Window* window) {
    SDL_Event e;
    SDL_zero(e);
    int w = 1;
    int h = 1;
    SDL_GetWindowSize(window, &w, &h);
    e.type = s.kind == Step::Kind::FingerDown   ? SDL_EVENT_FINGER_DOWN
             : s.kind == Step::Kind::FingerMove ? SDL_EVENT_FINGER_MOTION
                                                : SDL_EVENT_FINGER_UP;
    e.tfinger.touchID = kScriptedTouchId;
    e.tfinger.fingerID = static_cast<SDL_FingerID>(s.finger);
    e.tfinger.x = s.x / static_cast<float>(std::max(1, w));
    e.tfinger.y = s.y / static_cast<float>(std::max(1, h));
    e.tfinger.pressure = s.kind == Step::Kind::FingerUp ? 0.0f : 1.0f;
    e.tfinger.windowID = SDL_GetWindowID(window);
    e.common.timestamp = SDL_GetTicksNS();
    SDL_PushEvent(&e);
    // A real touch screen also makes SDL send the first finger as a mouse with the touch
    // mouse id, which is how the settings sheet and the bar (Dear ImGui) see it.
    if (s.finger == 1) {
        const auto mouse = [&](SDL_EventType type) {
            SDL_Event m;
            SDL_zero(m);
            m.type = type;
            m.common.timestamp = SDL_GetTicksNS();
            if (type == SDL_EVENT_MOUSE_MOTION) {
                m.motion.windowID = SDL_GetWindowID(window);
                m.motion.which = SDL_TOUCH_MOUSEID;
                m.motion.x = s.x;
                m.motion.y = s.y;
            } else {
                m.button.windowID = SDL_GetWindowID(window);
                m.button.which = SDL_TOUCH_MOUSEID;
                m.button.button = SDL_BUTTON_LEFT;
                m.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                m.button.clicks = 1;
                m.button.x = s.x;
                m.button.y = s.y;
            }
            SDL_PushEvent(&m);
        };
        mouse(SDL_EVENT_MOUSE_MOTION);
        if (s.kind == Step::Kind::FingerDown) {
            mouse(SDL_EVENT_MOUSE_BUTTON_DOWN);
        } else if (s.kind == Step::Kind::FingerUp) {
            mouse(SDL_EVENT_MOUSE_BUTTON_UP);
        }
    }
}

void DevScript::before_events(SDL_Window* window, long frame) {
    for (const Step& s : steps_) {
        if (s.frame != frame) {
            continue;
        }
        SDL_Event e;
        SDL_zero(e);
        switch (s.kind) {
        case Step::Kind::Move:
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.windowID = SDL_GetWindowID(window);
            e.motion.x = s.x;
            e.motion.y = s.y;
            break;
        case Step::Kind::Down:
        case Step::Kind::Up: {
            // Move there first in the same frame: a real pointer motion that arrived since the
            // scripted move would otherwise put the press somewhere else.
            SDL_Event move;
            SDL_zero(move);
            move.type = SDL_EVENT_MOUSE_MOTION;
            move.motion.windowID = SDL_GetWindowID(window);
            move.motion.x = s.x;
            move.motion.y = s.y;
            SDL_PushEvent(&move);
        }
            e.type = s.kind == Step::Kind::Down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                                : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.windowID = SDL_GetWindowID(window);
            e.button.button = s.finger == kRightButton ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            e.button.down = s.kind == Step::Kind::Down;
            e.button.clicks = 1;
            e.button.x = s.x;
            e.button.y = s.y;
            break;
        case Step::Kind::Text:
            e.type = SDL_EVENT_TEXT_INPUT;
            e.text.windowID = SDL_GetWindowID(window);
            e.text.text = s.text.c_str(); // lives as long as this script
            break;
        case Step::Kind::KeyDown:
        case Step::Kind::KeyUp: {
            const SDL_Scancode sc = SDL_GetScancodeFromName(s.text.c_str());
            e.type = s.kind == Step::Kind::KeyDown ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.windowID = SDL_GetWindowID(window);
            e.key.scancode = sc;
            e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
            e.key.down = s.kind == Step::Kind::KeyDown;
            e.key.repeat = false;
            break;
        }
        case Step::Kind::Resize: {
            // The window system answers these calls (the app is not asked to do them): a
            // person's resize is the OS's work, so the frame log leaves it out of the frame's work.
            const FrameDetailScope window_call(FrameDetail::Window);
            if (s.x == kResizeMaximise) {
                SDL_MaximizeWindow(window);
            } else if (s.x == kResizeFullscreen) {
                SDL_SetWindowFullscreen(window, true);
            } else if (s.x == kResizeRestore) {
                SDL_SetWindowFullscreen(window, false);
                SDL_RestoreWindow(window);
            } else {
                SDL_SetWindowSize(window, static_cast<int>(s.x), static_cast<int>(s.y));
            }
            continue;
        }
        case Step::Kind::Info:
            print_window_info(window);
            continue;
        case Step::Kind::FingerDown:
        case Step::Kind::FingerMove:
        case Step::Kind::FingerUp:
            push_finger(s, window);
            continue;
        case Step::Kind::Keyboard:
            due_keyboard_.push_back({static_cast<DevKeyboardEvent::Kind>(s.finger), s.text,
                                     static_cast<int>(s.x)});
            continue;
        case Step::Kind::DeviceLost:
            due_loss_ = losses_[static_cast<std::size_t>(s.finger)];
            if (due_loss_->by_present) {
                continue; // the presents fail; there is no event
            }
            e.type = SDL_EVENT_RENDER_DEVICE_LOST;
            e.render.windowID = SDL_GetWindowID(window);
            break;
        case Step::Kind::Shot:
            continue;
        }
        e.common.timestamp = SDL_GetTicksNS();
        SDL_PushEvent(&e);
    }
}

bool DevScript::after_draw(SDL_Renderer* renderer, long frame) {
    long last_shot = -1;
    for (const Step& s : steps_) {
        if (s.kind != Step::Kind::Shot) {
            continue;
        }
        last_shot = std::max(last_shot, s.frame);
        if (s.frame != frame) {
            continue;
        }
        SDL_Surface* surface = gfx::read_pixels(renderer, nullptr);
        if (surface == nullptr || !SDL_SavePNG(surface, s.text.c_str())) {
            std::fprintf(stderr, "screenshot %s failed: %s\n", s.text.c_str(), SDL_GetError());
            shot_failed_ = true;
        } else {
            std::printf("screenshot %s (%dx%d)\n", s.text.c_str(), surface->w, surface->h);
        }
        SDL_DestroySurface(surface);
    }
    if (quit_frame_ < 0 && quit_after_shots_ && last_shot >= 0) {
        return frame >= last_shot;
    }
    return quit_frame_ >= 0 && frame >= quit_frame_;
}

} // namespace etude::app
