#pragma once

#include "u8g2.h"

namespace wf {
namespace oled {

// Pieces the rigs' screens share, so Clouds and Stratus look alike.

// A box with `text` cut out of it, as a tag for an active state (Clouds'
// FRZ, Stratus' RECORDING). Uses the current font; leaves draw colour 1.
void DrawReversed(u8g2_t* u8g2, int x, int y, int width, int height, const char* text) noexcept;

// A level meter, 3 pixels wide, from `top` down `height` rows: segments of
// two rows every three, lit from the bottom for -48 to 0 dB of `level` (a
// linear peak, 1 at full scale); an unlit segment keeps a dot.
void DrawMeter(u8g2_t* u8g2, int x, int top, int height, float level) noexcept;

}  // namespace oled
}  // namespace wf
