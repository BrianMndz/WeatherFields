#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioHost.h"
#include "wf/oled/Display.h"
#include "wf/oled/Screen.h"
#include "wf/oled/TestPattern.h"

namespace wf {
namespace desktop {

// The emulated OLED: a wf::oled::Display with no panel, drawn at the board's
// frame rate, its buffer shown at the largest integer scale that fits, one
// square per pixel, so it shows exactly what the board would send. It draws
// the current rig's screen from the host's published values and levels, or
// the test pattern for a rig without one. A click changes the lit colour
// (monochrome panels come in white, blue or yellow).
class OledView final : public juce::Component, private juce::Timer {
public:
    explicit OledView(const AudioHost& host);

    // The rig's screen; null for the test pattern.
    void SetScreen(std::unique_ptr<oled::Screen> screen);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    // The host's published values, looked up on the message thread each
    // frame, so a rig swap never leaves it pointing at old tables.
    class HostReadout final : public oled::RigReadout {
    public:
        explicit HostReadout(const AudioHost& host) : host_(host) {}

        float Value(std::size_t slot, ParamId id) const noexcept override;
        float InputLevel() const noexcept override { return host_.InputLevel(); }
        float OutputLevel() const noexcept override { return host_.OutputLevel(); }
        Focus ViewFocus() const noexcept override;

    private:
        const AudioHost& host_;
    };

    void timerCallback() override;
    void Render();

    HostReadout readout_;
    std::unique_ptr<oled::Screen> screen_;
    oled::TestPatternScreen testPattern_;
    oled::Display display_;
    juce::Image image_;
    std::uint32_t startMs_;
    int colour_ = 0;
};

// A separate window holding an OledView; closing it only hides it.
class OledWindow final : public juce::DocumentWindow {
public:
    explicit OledWindow(const AudioHost& host);

    OledView& View() noexcept { return *view_; }

    void closeButtonPressed() override;

private:
    OledView* view_;  // owned by the window as its content
};

}  // namespace desktop
}  // namespace wf
