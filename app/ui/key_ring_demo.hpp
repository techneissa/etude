// SPDX-License-Identifier: GPL-3.0-or-later
// The key ring on a screen of its own (`--panel key-ring`, a developer's view): the ring in the
// middle and a row of scale types under it, so the ring can be shot and audited alone.
#pragma once

#include "bar.hpp"
#include "key_ring.hpp"

namespace etude::app::ui {

class KeyRingDemo {
  public:
    void open() { open_ = true; }
    void close() { open_ = false; }
    bool is_open() const { return open_; }
    // Draws it under the top row, over everything under it.
    void draw(Kit& k, const Frame& f);

  private:
    bool open_ = false;
    int root_ = 7; // G
    ScaleType type_ = ScaleType::Major;
};

} // namespace etude::app::ui
