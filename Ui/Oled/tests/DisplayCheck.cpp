#include <cstdint>
#include <cstring>
#include <iostream>

#include "wf/oled/Display.h"
#include "wf/oled/Fonts.h"
#include "wf/oled/TestPattern.h"

namespace {

int failures = 0;

void Expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// The buffer has u8g2's page layout: byte (y / 8) * 128 + x, bit y % 8.
void CheckLayout() {
    wf::oled::Display display;
    u8g2_ClearBuffer(display.U8g2());
    u8g2_DrawPixel(display.U8g2(), 5, 9);

    const std::uint8_t* buffer = display.Buffer();
    Expect(buffer[1 * wf::oled::kWidth + 5] == 0x02, "pixel (5, 9) is bit 1 of byte 133");
    int lit = 0;
    for (std::size_t i = 0; i < wf::oled::kBufferBytes; ++i) lit += buffer[i] != 0;
    Expect(lit == 1, "one pixel lights one byte");
    Expect(display.Pixel(5, 9), "Pixel(5, 9) reads it back");
    Expect(!display.Pixel(5, 8) && !display.Pixel(6, 9), "its neighbours stay dark");
    Expect(!display.Pixel(-1, 0) && !display.Pixel(0, -1) && !display.Pixel(128, 0) && !display.Pixel(0, 64),
           "outside the panel is dark");
}

// Both fonts link and have the cell sizes Fonts.h names.
void CheckFonts() {
    wf::oled::Display display;
    u8g2_t* u8g2 = display.U8g2();
    u8g2_SetFont(u8g2, wf::oled::kFontText);
    Expect(u8g2_GetMaxCharWidth(u8g2) == 6 && u8g2_GetMaxCharHeight(u8g2) == 10, "kFontText is 6x10");
    u8g2_SetFont(u8g2, wf::oled::kFontSmall);
    Expect(u8g2_GetMaxCharWidth(u8g2) == 5 && u8g2_GetMaxCharHeight(u8g2) == 7, "kFontSmall is 5x7");
}

void CheckTestPattern() {
    wf::oled::Display first;
    wf::oled::Display second;
    wf::oled::DrawTestPattern(first.U8g2(), 1234);
    wf::oled::DrawTestPattern(second.U8g2(), 1234);
    Expect(std::memcmp(first.Buffer(), second.Buffer(), wf::oled::kBufferBytes) == 0,
           "the pattern depends only on the time");

    Expect(first.Pixel(0, 0) && first.Pixel(127, 0) && first.Pixel(0, 63) && first.Pixel(127, 63),
           "the outline reaches every corner");
    Expect(first.Pixel(117, 3) && !first.Pixel(118, 3) && first.Pixel(118, 4) && first.Pixel(124, 10) &&
               !first.Pixel(125, 10),
           "the checkerboard alternates");

    wf::oled::DrawTestPattern(second.U8g2(), 1734);
    Expect(std::memcmp(first.Buffer(), second.Buffer(), wf::oled::kBufferBytes) != 0, "the pattern moves");
}

std::size_t sentBytes = 0;

std::uint8_t CountingByteCallback(u8x8_t*, std::uint8_t message, std::uint8_t count, void*) {
    if (message == U8X8_MSG_BYTE_SEND) sentBytes += count;
    return 1;
}

std::uint8_t NoGpioAndDelay(u8x8_t*, std::uint8_t, std::uint8_t, void*) {
    return 1;
}

// A panel's callbacks receive the whole buffer, plus the controller's commands.
void CheckSend() {
    wf::oled::Display display(CountingByteCallback, NoGpioAndDelay);
    wf::oled::DrawTestPattern(display.U8g2(), 0);
    sentBytes = 0;
    u8g2_SendBuffer(display.U8g2());
    Expect(sentBytes >= wf::oled::kBufferBytes, "SendBuffer sends at least the 1024 buffer bytes");
}

}  // namespace

int main() {
    CheckLayout();
    CheckFonts();
    CheckTestPattern();
    CheckSend();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "wf_oled_display_check passed\n";
    return 0;
}
