#pragma once

#include <cstdint>

#include "u8g2.h"

namespace wf {
namespace oled {

// The fonts WeatherFields compiles (src/Fonts.c). u8g2.h declares every
// upstream font, but only these have data here: any other fails to link.
// Drawing code uses these names, so changing a font is one line.
constexpr const std::uint8_t* kFontSmall = u8g2_font_5x7_tf;  // X11 misc-fixed 5x7
constexpr const std::uint8_t* kFontText = u8g2_font_6x10_tf;  // X11 misc-fixed 6x10

}  // namespace oled
}  // namespace wf
