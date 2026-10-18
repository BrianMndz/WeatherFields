#include "wf/oled/TestPattern.h"

#include <cmath>
#include <cstdio>

#include "wf/oled/Display.h"
#include "wf/oled/Fonts.h"

namespace wf {
namespace oled {

namespace {

constexpr float kTwoPi = 6.28318530718f;

// A position moving between 0 and `range` and back at `pixelsPerSecond`.
int Bounce(std::uint32_t ms, int pixelsPerSecond, int range) {
    const std::uint64_t travelled = static_cast<std::uint64_t>(ms) * pixelsPerSecond / 1000;
    const int position = static_cast<int>(travelled % static_cast<std::uint64_t>(2 * range));
    return position <= range ? position : 2 * range - position;
}

}  // namespace

void DrawTestPattern(u8g2_t* u8g2, std::uint32_t ms) noexcept {
    u8g2_ClearBuffer(u8g2);
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawFrame(u8g2, 0, 0, kWidth, kHeight);

    // Title row: name, running time, checkerboard.
    u8g2_SetFont(u8g2, kFontText);
    u8g2_DrawStr(u8g2, 3, 11, "WeatherFields");

    char seconds[16];
    std::snprintf(seconds, sizeof seconds, "%u.%us", static_cast<unsigned>(ms / 1000 % 1000),
                  static_cast<unsigned>(ms / 100 % 10));
    u8g2_SetFont(u8g2, kFontSmall);
    u8g2_DrawStr(u8g2, static_cast<u8g2_uint_t>(113 - u8g2_GetStrWidth(u8g2, seconds)), 10, seconds);

    for (int y = 3; y < 11; ++y) {
        for (int x = 117; x < 125; ++x) {
            if ((x + y) % 2 == 0) u8g2_DrawPixel(u8g2, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y));
        }
    }
    u8g2_DrawHLine(u8g2, 1, 14, kWidth - 2);

    // Two periods of a sine scrolling left at half a period per second.
    const float phase = static_cast<float>(ms % 2000) / 2000.0f;
    int previous = 0;
    for (int x = 1; x < kWidth - 1; ++x) {
        const float angle = kTwoPi * (2.0f * static_cast<float>(x) / static_cast<float>(kWidth) + phase);
        const int y = 28 - static_cast<int>(std::lround(10.0f * std::sin(angle)));
        if (x > 1) {
            u8g2_DrawLine(u8g2, static_cast<u8g2_uint_t>(x - 1), static_cast<u8g2_uint_t>(previous),
                          static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y));
        }
        previous = y;
    }
    u8g2_DrawHLine(u8g2, 1, 41, kWidth - 2);

    // A ball bouncing in the bottom lane, rows 42 to 62.
    constexpr int kRadius = 3;
    const int x = 1 + kRadius + Bounce(ms, 50, kWidth - 3 - 2 * kRadius);
    const int y = 43 + kRadius + Bounce(ms, 23, 62 - 43 - 2 * kRadius);
    u8g2_DrawDisc(u8g2, static_cast<u8g2_uint_t>(x), static_cast<u8g2_uint_t>(y), kRadius, U8G2_DRAW_ALL);
}

}  // namespace oled
}  // namespace wf
