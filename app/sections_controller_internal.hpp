// SPDX-License-Identifier: GPL-3.0-or-later
// Helpers the sections_controller*.cpp files share. Not for other code.
#pragma once

#include "sections_controller.hpp"

#include <utility>

namespace etude::app {

namespace sections_detail {

constexpr double kSame = 1e-6;

inline const SongSections& empty_sections() {
    static const SongSections kEmpty;
    return kEmpty;
}

inline EditResult failed(EditError error) { return EditResult{false, error, kNoSection}; }

} // namespace sections_detail

template <class F>
EditResult SectionsController::edit(F&& f) {
    if (!bound()) {
        return sections_detail::failed(EditError::NotFound);
    }
    const SongSections* cur = current();
    SongSections before = cur != nullptr ? *cur : SongSections{};
    before.song_hash = hash_;
    SongSections work = before;
    const EditResult r = f(work);
    if (!r.ok) {
        say_error(r.error);
        return r;
    }
    if (!(work == before)) {
        commit(std::move(work), before);
    }
    return r;
}

} // namespace etude::app
