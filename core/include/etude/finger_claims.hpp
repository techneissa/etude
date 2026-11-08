// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace etude {

// The fingers that landed on something that is not a screen's gesture (a button drawn over it, as
// the Undo chip of a toast over the section editor's roll): their touch belongs to that control
// from the landing to the lift, so a screen that reads raw finger events leaves them out. A few
// ids; a finger past them is not claimed.
class FingerClaims {
  public:
    static constexpr std::size_t kMax = 4;

    void claim(std::int64_t id) {
        if (!claimed(id) && count_ < kMax) {
            ids_[count_++] = id;
        }
    }
    bool claimed(std::int64_t id) const {
        for (std::size_t i = 0; i < count_; ++i) {
            if (ids_[i] == id) {
                return true;
            }
        }
        return false;
    }
    // The finger lifted or was cancelled.
    void release(std::int64_t id) {
        for (std::size_t i = 0; i < count_; ++i) {
            if (ids_[i] == id) {
                ids_[i] = ids_[--count_];
                return;
            }
        }
    }
    void clear() { count_ = 0; }
    std::size_t size() const { return count_; }

  private:
    std::array<std::int64_t, kMax> ids_{};
    std::size_t count_ = 0;
};

} // namespace etude
