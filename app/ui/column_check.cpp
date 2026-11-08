// SPDX-License-Identifier: GPL-3.0-or-later
#include "column_check.hpp"

#include <cmath>
#include <map>

namespace etude::app::ui::column_probe {

const char* kind_name(RowKind kind) {
    switch (kind) {
    case RowKind::Wide:
        return "wide";
    case RowKind::Compact:
        return "compact";
    case RowKind::Block:
        return "block";
    case RowKind::Inline:
        return "inline";
    }
    return "";
}

std::vector<std::string> check(const std::vector<Row>& rows, float tolerance, Stats* stats) {
    Stats local;
    Stats& st = stats != nullptr ? *stats : local;
    const auto where = [](const Row& r) {
        return "page " + std::to_string(r.scope) + " column " + std::to_string(r.column);
    };
    const auto describe = [](const Row& r) {
        return std::string(kind_name(r.kind)) + (r.stacked ? " stacked" : "") +
               (r.radio ? " radio" : "");
    };
    const auto span_text = [](const Row& r) {
        return std::to_string(r.span.x0) + ".." + std::to_string(r.span.x1);
    };
    // The first row of each (page, column, kind, stacked, radio) group is the reference; the first
    // row of each column is the reference for its right edge, and the first full-width row of it
    // for the x full-width rows start at.
    std::map<std::string, Row> first;
    std::map<std::string, Row> edge;
    std::map<std::string, Row> full;
    std::map<int, int> radio_column; // page -> the column its radio rows are in
    std::vector<std::string> problems;
    for (const Row& r : rows) {
        if (!r.visible) {
            continue;
        }
        // One list of choices lives in one column, never split over two.
        if (r.radio) {
            const auto [seen, fresh] = radio_column.emplace(r.scope, r.column);
            if (fresh) {
                ++st.radio_lists;
            } else {
                ++st.comparisons;
                if (seen->second != r.column) {
                    problems.push_back("page " + std::to_string(r.scope) +
                                       ": a list of choices continues in a second column");
                }
            }
        }
        const std::string column = std::to_string(r.scope) + "/" + std::to_string(r.column);
        const std::string key = column + "/" + std::to_string(static_cast<int>(r.kind)) +
                                (r.stacked ? "s" : "") + (r.radio ? "r" : "");
        const auto [it, inserted] = first.emplace(key, r);
        if (!inserted) {
            const Row& ref = it->second;
            ++st.comparisons;
            // A compact control is placed by its right edge; the others by both.
            const bool start_ok =
                r.kind == RowKind::Compact || std::fabs(r.span.x0 - ref.span.x0) <= tolerance;
            const bool end_ok = std::fabs(r.span.x1 - ref.span.x1) <= tolerance;
            if (!start_ok || !end_ok) {
                problems.push_back(where(r) + ": a " + describe(r) + " control spans " +
                                   span_text(r) + ", another " + span_text(ref));
            }
        }
        // Every control of a column ends at one right edge, whatever its kind.
        const auto [e, e_fresh] = edge.emplace(column, r);
        if (!e_fresh) {
            ++st.comparisons;
            if (std::fabs(r.span.x1 - e->second.span.x1) > tolerance) {
                problems.push_back(where(r) + ": a " + describe(r) + " control ends at " +
                                   std::to_string(r.span.x1) + ", a " + describe(e->second) +
                                   " one at " + std::to_string(e->second.span.x1));
            }
        }
        // Full-width rows (under their label, or with none) start where the rows' icons do.
        if (r.kind == RowKind::Block || (r.kind == RowKind::Wide && r.stacked)) {
            const auto [f, f_fresh] = full.emplace(column, r);
            if (!f_fresh) {
                ++st.comparisons;
                if (std::fabs(r.span.x0 - f->second.span.x0) > tolerance) {
                    problems.push_back(where(r) + ": a " + describe(r) + " control starts at " +
                                       std::to_string(r.span.x0) + ", a " + describe(f->second) +
                                       " one at " + std::to_string(f->second.span.x0));
                }
            }
        }
    }
    return problems;
}

} // namespace etude::app::ui::column_probe
