#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "wf/oled/Display.h"

namespace wf {
namespace desktop {

// The emulated OLED: a wf::oled::Display with no panel, drawn at a fixed
// frame rate, its buffer shown at the largest integer scale that fits, one
// square per pixel, so it shows exactly what the board would send. A click
// changes the lit colour (monochrome panels come in white, blue or yellow).
// For now it draws the test pattern; rig views come later.
class OledView final : public juce::Component, private juce::Timer {
public:
    OledView();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    void Render();

    oled::Display display_;
    juce::Image image_;
    std::uint32_t startMs_;
    int colour_ = 0;
};

// A separate window holding an OledView; closing it only hides it.
class OledWindow final : public juce::DocumentWindow {
public:
    OledWindow();

    void closeButtonPressed() override;
};

}  // namespace desktop
}  // namespace wf
