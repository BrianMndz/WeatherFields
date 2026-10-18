#pragma once

#include <cstddef>
#include <cstdint>

#include "u8g2.h"

namespace wf {
namespace bela {

// A wf::oled::Display's panel on a Linux I2C bus (/dev/i2c-<bus>, i2c-dev),
// as the display's byte and GPIO callbacks. u8x8 frames each I2C transfer
// with start and end messages; the byte callback collects one and writes it
// with a single write(), which the kernel sends as one bus transaction.
// Attach() points the display's u8x8 user pointer at this object, so the
// state is per display (O2O's glue kept it in globals). One thread uses it:
// the one that sends.
class I2cPanel {
public:
    I2cPanel() = default;
    ~I2cPanel();

    I2cPanel(const I2cPanel&) = delete;
    I2cPanel& operator=(const I2cPanel&) = delete;

    // Opens the bus and selects the panel's 7-bit address (0x3C for the
    // usual SH1106 modules). False if either fails, with errno in TakeError().
    // Nothing is sent: a missing panel shows as failed writes.
    bool Open(int bus, int address);

    // Before the display's first message.
    void Attach(u8x8_t* u8x8);

    // The display's callbacks (wf::oled::Display's constructor).
    static std::uint8_t Byte(u8x8_t* u8x8, std::uint8_t message, std::uint8_t count, void* data);
    static std::uint8_t GpioAndDelay(u8x8_t* u8x8, std::uint8_t message, std::uint8_t count, void* data);

    // errno of the first failure since the last call, or 0. u8g2's calls
    // return nothing, so a frame checks this after u8g2_SendBuffer().
    int TakeError();

private:
    // The longest transfer the SSD13xx I2C layer makes is a command with its
    // arguments, 2 + at most 30 bytes; data goes 24 bytes at a time plus one.
    static constexpr std::size_t kMaxTransfer = 64;

    void Fail(int error);

    int file_ = -1;
    std::uint8_t transfer_[kMaxTransfer];
    std::size_t length_ = 0;
    bool overflow_ = false;
    int error_ = 0;
};

}  // namespace bela
}  // namespace wf
