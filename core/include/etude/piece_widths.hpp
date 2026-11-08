// SPDX-License-Identifier: GPL-3.0-or-later
// How wide pieces of text are at every whole pixel size, filled in slices. The block-name planner
// (`block_names.hpp`) needs the width of each letter, syllable and octave number at every size it
// may plan, measured with the real font (core has no font, so the caller passes the measuring
// function). A piece takes about a hundred measurements, so a caller that meets many new pieces at
// once measures them over several frames and keeps what it drew before until they are all there.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace etude {

class PieceWidths {
  public:
    // Sizes 1 to kSizes - 1 px are measured; entry 0 is never used (0 px is not a size).
    static constexpr std::size_t kSizes = 128;
    using Index = std::size_t;

    // The index of `piece`, added with no size measured if it is new. An empty piece is complete
    // and all zeros.
    Index add(std::string_view piece) {
        if (const auto it = index_.find(piece); it != index_.end()) {
            return it->second;
        }
        const Index i = entries_.size();
        entries_.emplace_back();
        Entry& e = entries_.back();
        e.piece = std::string(piece);
        e.next = piece.empty() ? kSizes : 1;
        index_.emplace(e.piece, i);
        return i;
    }

    // The widths of piece `i` by size (kSizes entries, 0 where not measured yet). The tables never
    // move, so a span stays valid as pieces are added.
    std::span<const float> table(Index i) const { return entries_[i].px; }
    const std::string& piece(Index i) const { return entries_[i].piece; }
    bool complete(Index i) const { return entries_[i].next >= kSizes; }
    std::size_t size() const { return entries_.size(); }

    bool all_complete(std::span<const Index> pieces) const {
        for (const Index i : pieces) {
            if (!complete(i)) {
                return false;
            }
        }
        return true;
    }

    // Sizes still to measure for `pieces`, in total (for progress and tests).
    std::size_t sizes_missing(std::span<const Index> pieces) const {
        std::size_t n = 0;
        for (const Index i : pieces) {
            n += entries_[i].next < kSizes ? kSizes - entries_[i].next : 0;
        }
        return n;
    }

    // Measures the sizes `pieces` still lack, one size for all of them at a time, the smallest
    // missing size first (so the font changes size once per size). `width(piece, px)` measures;
    // `out_of_time()` is asked after each size and ends the call when it says true, so a caller
    // with a time budget stops between sizes. At least one size is always measured, so repeated
    // calls finish. Returns whether every piece in `pieces` is complete.
    template <class Width, class OutOfTime>
    bool measure(std::span<const Index> pieces, Width&& width, OutOfTime&& out_of_time) {
        while (true) {
            std::size_t px = kSizes;
            for (const Index i : pieces) {
                px = std::min(px, entries_[i].next);
            }
            if (px >= kSizes) {
                return true;
            }
            for (const Index i : pieces) {
                Entry& e = entries_[i];
                if (e.next == px) {
                    e.px[px] = width(static_cast<const std::string&>(e.piece), px);
                    ++e.next;
                }
            }
            if (out_of_time()) {
                return all_complete(pieces);
            }
        }
    }

  private:
    struct Entry {
        std::string piece;
        std::array<float, kSizes> px{};
        std::size_t next = 1; // the smallest size not measured yet; kSizes when complete
    };
    std::deque<Entry> entries_;
    std::map<std::string, Index, std::less<>> index_;
};

} // namespace etude
