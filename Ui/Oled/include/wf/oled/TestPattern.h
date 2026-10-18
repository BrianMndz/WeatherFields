#pragma once

#include <cstdint>

#include "u8g2.h"

namespace wf {
namespace oled {

// Clears the buffer and draws the bring-up pattern: the panel's outline (a
// column offset or the wrong driver shows at the edges), the title, a
// one-pixel checkerboard (scaling must keep it exact), a scrolling sine and a
// bouncing ball. The animation follows `ms`, not frames, so it moves at the
// same speed whatever frame rate the panel's bus allows.
void DrawTestPattern(u8g2_t* u8g2, std::uint32_t ms) noexcept;

}  // namespace oled
}  // namespace wf
