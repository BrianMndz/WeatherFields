#include "wf/oled/Display.h"

namespace wf {
namespace oled {

namespace {

// u8x8_dummy_cb fails every message; a display without a panel succeeds.
std::uint8_t NoGpioAndDelay(u8x8_t*, std::uint8_t, std::uint8_t, void*) {
    return 1;
}

}  // namespace

Display::Display() noexcept : Display(u8x8_byte_empty, NoGpioAndDelay) {}

Display::Display(u8x8_msg_cb byteCallback, u8x8_msg_cb gpioAndDelayCallback,
                 const u8g2_cb_t* rotation) noexcept
    : u8g2_(), buffer_() {
    // u8g2_Setup_sh1106_i2c_128x64_noname_f() (u8g2_d_setup.c, not imported)
    // with this object's buffer instead of u8g2_d_memory.c's static one.
    u8g2_SetupDisplay(&u8g2_, u8x8_d_sh1106_128x64_noname, u8x8_cad_ssd13xx_fast_i2c, byteCallback,
                      gpioAndDelayCallback);
    u8g2_SetupBuffer(&u8g2_, buffer_, kHeight / 8, u8g2_ll_hvline_vertical_top_lsb, rotation);
}

bool Display::Pixel(int x, int y) const noexcept {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return false;
    return ((buffer_[(y / 8) * kWidth + x] >> (y % 8)) & 1) != 0;
}

}  // namespace oled
}  // namespace wf
