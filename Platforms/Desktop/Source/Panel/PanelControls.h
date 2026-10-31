#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace wf {
namespace desktop {
namespace panel {

// The desktop panels' look: controls and drawing ported from CloudsEngine's
// JUCE editor (Source/PanelControls, e7738d9) for the Clouds panel, and
// shared by every panel drawn in its style (Stratus). PanelView.h holds the
// plate and its scaling.
namespace colours {
const juce::Colour kPanel{0xffe4e3e4};
const juce::Colour kPanelShade{0xffd2d1d3};
const juce::Colour kOrnament{0xffc9c6ca};
const juce::Colour kInk{0xff1a1a1c};
const juce::Colour kRed{0xffa52f57};
const juce::Colour kRedCap{0xff7d2441};
const juce::Colour kTeal{0xff3a817e};
const juce::Colour kTealCap{0xff2f6e6b};
const juce::Colour kWhiteCap{0xffeeeeee};
const juce::Colour kSkirt{0xff1f2022};
const juce::Colour kLedGreen{0xffb5dc5e};
}  // namespace colours

// Draws rotary sliders as Clouds knobs: a black skirt with a white pointer
// line and a coloured cap.
class PanelLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;
};

// A knob without a text box. Its value appears in a bubble while dragging,
// and a double-click returns it to its default.
class PanelKnob final : public juce::Slider {
public:
    enum class Size { Large, Small };

    PanelKnob(Size size, juce::Colour capColour);

    // Sets the range, default and current value, and the bubble's suffix.
    void Configure(double minimum, double maximum, double defaultValue, double value,
                   const juce::String& suffix, int decimalPlaces);

    float CapRatio() const noexcept;

private:
    Size size_;
};

// An illuminated button like FREEZE: a toggle whose lens lights in `colour`
// when on, glowing from `core` at its centre.
class LitButton final : public juce::Button {
public:
    LitButton(const juce::String& name, juce::Colour colour, juce::Colour core);

private:
    void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override;

    juce::Colour colour_;
    juce::Colour core_;
};

// The small black push buttons: B, C and the trigger. Like the module, a
// button held for kLongPressMilliseconds reports a long press at that
// moment, while it is still held, and, if onVeryLongPress is set, held on to
// kVeryLongPressMilliseconds reports a very long press too; releasing it
// before the first reports a short press. Dragging off the button cancels
// the press. A button with none of these callbacks set can use onClick
// instead.
class PanelPushButton final : public juce::Button, private juce::Timer {
public:
    static constexpr int kLongPressMilliseconds = 1000;
    static constexpr int kVeryLongPressMilliseconds = 4000;

    explicit PanelPushButton(const juce::String& name);

    std::function<void()> onShortPress;
    std::function<void()> onLongPress;
    std::function<void()> onVeryLongPress;

private:
    void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void clicked() override;
    void timerCallback() override;

    bool longPressReported_ = false;
};

// One of the four bicolour LEDs. Like the module's, each has a red and a
// green element, from 0 (off) to 1; both together show yellow.
class PanelLed final : public juce::Component, public juce::SettableTooltipClient {
public:
    void SetLight(float red, float green);

private:
    void paint(juce::Graphics& g) override;

    float red_ = 0.0f;
    float green_ = 0.0f;
};

// Panel drawing, in the panel's reference coordinates.
juce::Font PanelFont(float height, bool bold = true);

// Labels on the panel: plain black text, or white text on a coloured pill.
void DrawLabel(juce::Graphics& g, const juce::String& text, juce::Point<float> centre,
               juce::Colour colour = colours::kInk);
void DrawPill(juce::Graphics& g, const juce::String& text, juce::Point<float> centre, juce::Colour colour);

void DrawScrewHole(juce::Graphics& g, juce::Point<float> centre);

// A simplified version of the ornamental rings behind the large knobs.
void DrawOrnament(juce::Graphics& g, juce::Point<float> centre);

}  // namespace panel
}  // namespace desktop
}  // namespace wf
