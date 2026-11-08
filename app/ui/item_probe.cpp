// SPDX-License-Identifier: GPL-3.0-or-later
#include "item_probe.hpp"

#include "item_hook.hpp"

#include <imgui_internal.h>

#include <utility>

namespace {

std::vector<etude::app::ui::probe::Item> g_collecting;
std::vector<etude::app::ui::probe::Item> g_last;
bool g_probe_on = false;                          // the settings check's list
etude::app::ui::ItemHook g_hook = nullptr;      // the audit's hit areas

// The hooks run while either wants them.
void update_hooks() {
    if (ImGuiContext* g = ImGui::GetCurrentContext()) {
        g->TestEngineHookItems = g_probe_on || g_hook != nullptr;
    }
}

} // namespace

// ImGui calls these for every item while ImGuiContext::TestEngineHookItems is set. ItemAdd feeds
// the settings check's list; ItemInfo, which every widget that takes input calls after it was added and
// found visible (so the last item is the widget itself), feeds the audit's hook.
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb,
                                 const ImGuiLastItemData* item_data) {
    if (!g_probe_on || item_data == nullptr || ctx->CurrentWindow == nullptr) { // a window's box
        return;
    }
    const ImGuiWindow* w = ctx->CurrentWindow;
    etude::app::ui::probe::Item item;
    item.id = id;
    item.min = bb.Min;
    item.max = bb.Max;
    item.clip_min = w->ClipRect.Min;
    item.clip_max = w->ClipRect.Max;
    item.window = w->Name;
    item.scroll_y = w->Scroll.y;
    item.scroll_max_y = w->ScrollMax.y;
    g_collecting.push_back(std::move(item));
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label,
                                  ImGuiItemStatusFlags) {
    // A window reports itself the same way; it is not an item to press.
    if (g_hook == nullptr || ctx->LastItemData.ID != id || ImGui::FindWindowByID(id) != nullptr) {
        return;
    }
    g_hook(id, label, ctx->LastItemData.Rect.Min, ctx->LastItemData.Rect.Max);
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

namespace etude::app::ui::probe {

void enable(bool on) {
    g_probe_on = on;
    update_hooks();
    g_collecting.clear();
    g_last.clear();
}

void next_frame() {
    g_last.swap(g_collecting);
    g_collecting.clear();
}

const std::vector<Item>& items() { return g_last; }

ImGuiID row_item_id(const std::string& window, const char* label, const char* leaf,
                    bool in_table) {
    ImGuiID seed = ImHashStr(window.c_str()); // a window's id is the hash of its name
    if (in_table) {
        seed = ImHashStr("##cols", 0, seed);
    }
    return ImHashStr(leaf, 0, ImHashStr(label, 0, seed));
}

void add_rect(const char* window, ImVec2 min, ImVec2 max) {
    if (!g_probe_on) {
        return;
    }
    Item item;
    item.min = min;
    item.max = max;
    item.clip_min = min;
    item.clip_max = max;
    item.window = window;
    g_collecting.push_back(std::move(item));
}

} // namespace etude::app::ui::probe

namespace etude::app::ui {

void set_item_hook(ItemHook fn) {
    g_hook = fn;
    update_hooks();
}

} // namespace etude::app::ui
