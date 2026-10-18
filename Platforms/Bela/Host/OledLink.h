#pragma once

#include <atomic>
#include <thread>

#include "BelaHost.h"
#include "I2cPanel.h"
#include "wf/oled/Display.h"
#include "wf/oled/Screen.h"
#include "wf/oled/TestPattern.h"

namespace wf {
namespace bela {

// The OLED's thread: an ordinary Linux thread, not Xenomai, so its I2C
// system calls never touch render(). Each frame it draws the app's screen
// (or the test pattern) from the values render() published, sends the whole
// buffer, then waits for the next frame time; a frame that runs late moves
// the schedule rather than bursting to catch up. Every few seconds it prints the frame rate and how long drawing and
// sending took, the numbers that set the desktop emulator's frame rate.
//
// A missing or failing panel never stops the audio: the error is printed
// once, and the thread retries once a second until a frame goes through.
class OledLink {
public:
    OledLink();

    OledLink(const OledLink&) = delete;
    OledLink& operator=(const OledLink&) = delete;

    // From setup(), once `published` is set up. `screen` null shows the test
    // pattern. False if the bus cannot be opened; nothing then runs.
    bool Start(const Published& published, oled::Screen* screen, int bus, int address, int framesPerSecond);

    // From cleanup(): blanks the panel and joins the thread.
    void Stop();

private:
    // render()'s published values, found by a search over the parameters:
    // a dozen lookups of a few dozen entries a frame.
    class PublishedReadout final : public oled::RigReadout {
    public:
        explicit PublishedReadout(const Published& published) : published_(published) {}

        float Value(std::size_t slot, ParamId id) const noexcept override;
        float InputLevel() const noexcept override;
        float OutputLevel() const noexcept override;
        Focus ViewFocus() const noexcept override;

    private:
        const Published& published_;
    };

    void Run(const PublishedReadout& readout, oled::Screen& screen, int framesPerSecond);

    oled::TestPatternScreen testPattern_;
    I2cPanel panel_;
    oled::Display display_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace bela
}  // namespace wf
