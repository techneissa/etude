// SPDX-License-Identifier: GPL-3.0-or-later
// The audit's settings fit check (icon_audit.hpp, settings_fit.hpp): what the sheet cut to its room
// and which pages scroll, and the report lines for them.
#include "icon_audit.hpp"

#include "settings_fit.hpp"

#include <cstdio>

namespace etude::app::ui::audit {

namespace {

int g_settings = 0; // inside a SettingsScope
std::vector<TextCut> g_cuts;
std::vector<PageScroll> g_page_scrolls;

} // namespace

SettingsScope::SettingsScope() { ++g_settings; }
SettingsScope::~SettingsScope() { --g_settings; }

void cut(std::string_view text, std::string_view shown) {
    if (!on() || g_settings == 0 || !text_was_cut(text, shown)) {
        return;
    }
    for (const TextCut& c : g_cuts) {
        if (c.text == text && c.shown == shown) {
            return;
        }
    }
    g_cuts.push_back({std::string(text), std::string(shown)});
}

void page_scroll(const char* page, float max_scroll) {
    if (on()) {
        g_page_scrolls.push_back({page != nullptr ? page : "", max_scroll});
    }
}

const std::vector<TextCut>& cuts() { return g_cuts; }
const std::vector<PageScroll>& page_scrolls() { return g_page_scrolls; }
void clear_settings_fit() {
    g_cuts.clear();
    g_page_scrolls.clear();
}

} // namespace etude::app::ui::audit

namespace etude::app {

// One `cut-text` line per text the sheet cut (the full text, then what is shown) and one
// `page-scroll` line per page drawn open (how many points it scrolls: 0 for a page that fits, then
// the issue of a known scrolling page in the last column). A cut or a scroll at 1366x768 and up is a
// FAIL, unless the page is a known one (`known`: reported); a known page that fits is `stale`.
// Below that size the line says `listed`.
void IconAudit::write_settings_fit(std::FILE* out) const {
    const ImVec2 window = ImGui::GetIO().DisplaySize;
    for (const ui::audit::TextCut& c : settled_cuts_) {
        const ui::FitVerdict v = ui::cut_verdict(window.x, window.y);
        std::fprintf(out, "%s\t%s\tcut-text\t0\t0\t0\t0\t%.0f\t%.0f\t0\t0\t%s\t%s\n", label_.c_str(),
                     c.text.c_str(), static_cast<double>(window.x), static_cast<double>(window.y),
                     ui::verdict_word(v), c.shown.c_str());
    }
    // The scenario's name is the label's first word (the rest is the window size and scale).
    const std::string_view scenario = std::string_view(label_).substr(0, label_.find(' '));
    const std::string_view issue = ui::known_scroll_issue(scenario);
    for (const ui::audit::PageScroll& s : settled_scrolls_) {
        const ui::FitVerdict v = ui::scroll_verdict(s.max_scroll, window.x, window.y, issue);
        std::fprintf(out, "%s\t%s\tpage-scroll\t0\t0\t0\t0\t%.1f\t%.0f\t0\t0\t%s\t%.*s\n",
                     label_.c_str(), s.page.c_str(), static_cast<double>(s.max_scroll),
                     static_cast<double>(window.y), ui::verdict_word(v),
                     static_cast<int>(issue.size()), issue.data());
    }
}

} // namespace etude::app
