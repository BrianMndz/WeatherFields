#pragma once

#include <atomic>
#include <thread>

#include "I2cPanel.h"
#include "wf/oled/Display.h"

namespace wf {
namespace bela {

// The OLED's thread: an ordinary Linux thread, not Xenomai, so its I2C
// system calls never touch render(). Each frame it draws (for now the test
// pattern) and sends the whole buffer, then waits for the next frame time;
// a frame that runs late moves the schedule rather than bursting to catch
// up. Every few seconds it prints the frame rate and how long drawing and
// sending took, the numbers that set the desktop emulator's frame rate.
//
// A missing or failing panel never stops the audio: the error is printed
// once, and the thread retries once a second until a frame goes through.
class OledLink {
public:
    OledLink();

    OledLink(const OledLink&) = delete;
    OledLink& operator=(const OledLink&) = delete;

    // From setup(). False if the bus cannot be opened; nothing then runs.
    bool Start(int bus, int address, int framesPerSecond);

    // From cleanup(): blanks the panel and joins the thread.
    void Stop();

private:
    void Run(int framesPerSecond);

    I2cPanel panel_;
    oled::Display display_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace bela
}  // namespace wf
