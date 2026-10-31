#include "wf/oled/Widgets.h"

#include <algorithm>
#include <cmath>

namespace wf {
namespace oled {

namespace {

constexpr float kMeterFloorDb = -48.0f;
constexpr int kMeterWidth = 3;

u8g2_uint_t Px(int coordinate) {
    return static_cast<u8g2_uint_t>(coordinate);
}

}  // namespace

void DrawReversed(u8g2_t* u8g2, int x, int y, int width, int height, const char* text) noexcept {
    u8g2_DrawBox(u8g2, Px(x), Px(y), Px(width), Px(height));
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawStr(u8g2, Px(x + 2), Px(y + height - 2), text);
    u8g2_SetDrawColor(u8g2, 1);
}

void DrawMeter(u8g2_t* u8g2, int x, int top, int height, float level) noexcept {
    const float db = level > 0.0f ? 20.0f * std::log10(level) : kMeterFloorDb;
    const float share = (db - kMeterFloorDb) / -kMeterFloorDb;
    const float lit = (share >= 0.0f ? std::min(share, 1.0f) : 0.0f) * static_cast<float>(height);  // NaN gives 0
    for (int offset = 0; offset + 2 <= height; offset += 3) {
        const int bottom = top + height - 1 - offset;
        if (static_cast<float>(offset) < lit) {
            u8g2_DrawBox(u8g2, Px(x), Px(bottom - 1), Px(kMeterWidth), 2);
        } else {
            u8g2_DrawPixel(u8g2, Px(x + kMeterWidth / 2), Px(bottom));
        }
    }
}

}  // namespace oled
}  // namespace wf
