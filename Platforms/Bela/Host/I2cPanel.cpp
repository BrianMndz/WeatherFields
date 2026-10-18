#include "I2cPanel.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

// u8x8.h defines it on Linux; without it there is nowhere to keep the state.
#ifndef U8X8_WITH_USER_PTR
#error "I2cPanel needs u8x8's user pointer (U8X8_WITH_USER_PTR)"
#endif

namespace wf {
namespace bela {

I2cPanel::~I2cPanel() {
    if (file_ >= 0) close(file_);
}

bool I2cPanel::Open(int bus, int address) {
    char path[32];
    std::snprintf(path, sizeof path, "/dev/i2c-%d", bus);
    file_ = open(path, O_RDWR);
    if (file_ < 0) {
        Fail(errno);
        return false;
    }
    if (ioctl(file_, I2C_SLAVE, address) < 0) {
        Fail(errno);
        close(file_);
        file_ = -1;
        return false;
    }
    return true;
}

void I2cPanel::Attach(u8x8_t* u8x8) {
    u8x8_SetUserPtr(u8x8, this);
}

int I2cPanel::TakeError() {
    const int error = error_;
    error_ = 0;
    return error;
}

void I2cPanel::Fail(int error) {
    if (error_ == 0) error_ = error;
}

std::uint8_t I2cPanel::Byte(u8x8_t* u8x8, std::uint8_t message, std::uint8_t count, void* data) {
    I2cPanel& panel = *static_cast<I2cPanel*>(u8x8_GetUserPtr(u8x8));
    switch (message) {
        case U8X8_MSG_BYTE_INIT:
        case U8X8_MSG_BYTE_SET_DC:  // no D/C line on I2C: a control byte says it
            return 1;
        case U8X8_MSG_BYTE_START_TRANSFER:
            panel.length_ = 0;
            panel.overflow_ = false;
            return 1;
        case U8X8_MSG_BYTE_SEND:
            if (panel.length_ + count > kMaxTransfer) {
                panel.overflow_ = true;
                return 0;
            }
            std::memcpy(panel.transfer_ + panel.length_, data, count);
            panel.length_ += count;
            return 1;
        case U8X8_MSG_BYTE_END_TRANSFER: {
            if (panel.overflow_) {
                panel.Fail(EMSGSIZE);
                return 0;
            }
            if (panel.file_ < 0) {
                panel.Fail(EBADF);
                return 0;
            }
            const ssize_t written = write(panel.file_, panel.transfer_, panel.length_);
            if (written != static_cast<ssize_t>(panel.length_)) {
                panel.Fail(written < 0 ? errno : EIO);
                return 0;
            }
            return 1;
        }
        default:
            return 0;
    }
}

// No reset or other GPIO lines on these modules; only the init sequence's
// delays matter.
std::uint8_t I2cPanel::GpioAndDelay(u8x8_t*, std::uint8_t message, std::uint8_t count, void*) {
    switch (message) {
        case U8X8_MSG_DELAY_MILLI:
            std::this_thread::sleep_for(std::chrono::milliseconds(count));
            return 1;
        case U8X8_MSG_DELAY_10MICRO:
            std::this_thread::sleep_for(std::chrono::microseconds(10 * count));
            return 1;
        case U8X8_MSG_DELAY_100NANO:
            std::this_thread::sleep_for(std::chrono::nanoseconds(100 * count));
            return 1;
        case U8X8_MSG_DELAY_NANO:
            std::this_thread::sleep_for(std::chrono::nanoseconds(count));
            return 1;
        default:
            return 1;
    }
}

}  // namespace bela
}  // namespace wf
