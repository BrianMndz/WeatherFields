#pragma once

#include <cstddef>
#include <cstdint>

#include "u8g2.h"

namespace wf {
namespace oled {

constexpr int kWidth = 128;
constexpr int kHeight = 64;
constexpr std::size_t kBufferBytes = kWidth * kHeight / 8;

// A 128x64 SH1106, the controller of the 1.3" I2C modules, driven by u8g2 in
// full-buffer mode with its buffer here. Drawing through U8g2() only writes
// that buffer; u8g2_SendBuffer() sends it through the byte and GPIO callbacks,
// the display's only access to hardware. The default constructor gives a
// display with no panel: hosts without one draw and read Buffer(), so the
// same drawing code shows the same pixels everywhere. u8g2's SSD13xx I2C
// layer keeps its transfer state in a static, so only one display in a
// process may send at a time.
class Display {
public:
    Display() noexcept;
    Display(u8x8_msg_cb byteCallback, u8x8_msg_cb gpioAndDelayCallback,
            const u8g2_cb_t* rotation = U8G2_R0) noexcept;

    // u8g2 keeps a pointer to the buffer.
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    u8g2_t* U8g2() noexcept { return &u8g2_; }

    // The panel's own layout, as sent: 8 pages of 128 bytes, each byte a
    // column of 8 pixels with the least significant bit at the top. With a
    // rotation other than U8G2_R0 this differs from drawing coordinates.
    const std::uint8_t* Buffer() const noexcept { return buffer_; }

    // Whether the pixel at panel coordinates (x, y) is lit; false outside.
    bool Pixel(int x, int y) const noexcept;

private:
    u8g2_t u8g2_;
    std::uint8_t buffer_[kBufferBytes];
};

}  // namespace oled
}  // namespace wf
