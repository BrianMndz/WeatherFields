#include "PanelControls.h"

namespace wf {
namespace desktop {
namespace panel {

void PanelLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPosProportional, float rotaryStartAngle,
                                        float rotaryEndAngle, juce::Slider& slider) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(1.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();

    // Skirt, lit from the top left, over a soft shadow that stays inside the
    // component's bounds.
    const auto skirt = juce::Rectangle<float>(radius * 1.94f, radius * 1.94f).withCentre(centre);
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.fillEllipse(skirt.translated(0.0f, radius * 0.03f).expanded(radius * 0.02f));

    g.setGradientFill(juce::ColourGradient(colours::kSkirt.brighter(0.5f), skirt.getTopLeft(),
                                           colours::kSkirt.darker(0.6f), skirt.getBottomRight(), false));
    g.fillEllipse(skirt);

    // Pointer line across the skirt.
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const auto* knob = dynamic_cast<const PanelKnob*>(&slider);
    const float capRatio = knob != nullptr ? knob->CapRatio() : 0.65f;
    const auto pointerStart = centre.getPointOnCircumference(radius * (capRatio - 0.05f), angle);
    const auto pointerEnd = centre.getPointOnCircumference(radius * 0.9f, angle);
    g.setColour(juce::Colours::white);
    g.drawLine({pointerStart, pointerEnd}, radius * 0.09f);

    // Cap, slightly domed.
    const auto capColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
    const auto cap = juce::Rectangle<float>(radius * 2.0f * capRatio, radius * 2.0f * capRatio).withCentre(centre);
    g.setGradientFill(juce::ColourGradient(capColour.brighter(0.25f), cap.getTopLeft(),
                                           capColour.darker(0.25f), cap.getBottomRight(), false));
    g.fillEllipse(cap);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawEllipse(cap, radius * 0.03f);
}

PanelKnob::PanelKnob(Size size, juce::Colour capColour) : size_(size) {
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setColour(juce::Slider::rotarySliderFillColourId, capColour);
}

void PanelKnob::Configure(double minimum, double maximum, double defaultValue, double value,
                          const juce::String& suffix, int decimalPlaces) {
    setRange(minimum, maximum, 0.001);
    setDoubleClickReturnValue(true, defaultValue);
    setTextValueSuffix(suffix);
    setNumDecimalPlacesToDisplay(decimalPlaces);
    setPopupDisplayEnabled(true, true, nullptr);
    setValue(value, juce::dontSendNotification);
}

float PanelKnob::CapRatio() const noexcept {
    return size_ == Size::Large ? 0.70f : 0.60f;
}

LitButton::LitButton(const juce::String& name, juce::Colour colour, juce::Colour core)
    : juce::Button(name), colour_(colour), core_(core) {
    setClickingTogglesState(true);
}

void LitButton::paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) {
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const bool on = getToggleState();

    // Bezel.
    g.setColour(colours::kSkirt);
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));

    // Lens: a pale disc that glows from the centre when lit.
    const auto lens = juce::Rectangle<float>(radius * 1.7f, radius * 1.7f).withCentre(centre);
    const auto rim = on ? colour_ : colour_.withSaturation(0.25f).darker(0.1f);
    const auto core = on ? core_ : rim.brighter(0.2f);
    g.setGradientFill(juce::ColourGradient(core, centre, rim, lens.getTopLeft(), true));
    g.fillEllipse(lens);

    if (isDown || isHighlighted) {
        g.setColour(juce::Colours::white.withAlpha(isDown ? 0.25f : 0.12f));
        g.fillEllipse(lens);
    }
}

PanelPushButton::PanelPushButton(const juce::String& name) : juce::Button(name) {}

// Presses come from the mouse events and clicked(), not from the button's
// state: JUCE also changes that state to flash a quick click and when the
// pointer leaves the button while held, neither of which is a press.
void PanelPushButton::mouseDown(const juce::MouseEvent& e) {
    juce::Button::mouseDown(e);
    longPressReported_ = false;
    if (isDown()) startTimer(kLongPressMilliseconds);
}

void PanelPushButton::mouseDrag(const juce::MouseEvent& e) {
    juce::Button::mouseDrag(e);
    // Dragging off the button cancels the press, as it cancels a click.
    if (!isDown()) stopTimer();
}

void PanelPushButton::mouseUp(const juce::MouseEvent& e) {
    stopTimer();
    juce::Button::mouseUp(e);
    longPressReported_ = false;
}

// Called once per completed click: released over the button, or a keyboard
// or accessibility press.
void PanelPushButton::clicked() {
    if (!longPressReported_ && onShortPress != nullptr) onShortPress();
}

// Fires at the long press and, for buttons with a very long press, again at
// that one; both are measured from the press.
void PanelPushButton::timerCallback() {
    if (longPressReported_) {
        stopTimer();
        if (onVeryLongPress != nullptr) onVeryLongPress();
        return;
    }

    longPressReported_ = true;
    if (onVeryLongPress != nullptr) {
        startTimer(kVeryLongPressMilliseconds - kLongPressMilliseconds);
    } else {
        stopTimer();
    }

    if (onLongPress != nullptr) onLongPress();
}

void PanelPushButton::paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) {
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto button = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);

    g.setColour(juce::Colour(0xff8d8c8e));
    g.fillEllipse(button);

    const auto cap = button.reduced(radius * 0.12f);
    auto top = juce::Colour(0xff4a4b4d);
    if (isHighlighted && isEnabled()) top = top.brighter(0.2f);
    g.setGradientFill(juce::ColourGradient(isDown ? top.darker(0.4f) : top, cap.getTopLeft(),
                                           juce::Colour(0xff121314), cap.getBottomRight(), false));
    g.fillEllipse(cap);
}

void PanelLed::SetLight(float red, float green) {
    red = juce::jlimit(0.0f, 1.0f, red);
    green = juce::jlimit(0.0f, 1.0f, green);
    if (!juce::exactlyEqual(red, red_) || !juce::exactlyEqual(green, green_)) {
        red_ = red;
        green_ = green;
        repaint();
    }
}

void PanelLed::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();

    // Grey bezel, then a diffused lens between unlit and the mixed colour of
    // the two elements: green, red, or yellow when both are on.
    g.setColour(juce::Colour(0xffa9a8a2));
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));

    // The lit colour carries only the hue, from the elements' ratio;
    // intensity alone sets how far the lens moves towards it.
    const float intensity = juce::jmax(red_, green_);
    const float red = intensity > 0.0f ? red_ / intensity : 0.0f;
    const float green = intensity > 0.0f ? green_ / intensity : 0.0f;
    const auto lit = juce::Colour::fromFloatRGBA(juce::jmin(1.0f, red + green * 0.55f),
                                                 juce::jmin(1.0f, green * 0.9f + red * 0.2f), 0.15f, 1.0f);
    const auto lens = juce::Rectangle<float>(radius * 1.6f, radius * 1.6f).withCentre(centre);
    const auto unlit = juce::Colour(0xff8f9a78);
    const auto rim = unlit.interpolatedWith(lit, intensity);
    const auto core = rim.interpolatedWith(lit.brighter(0.6f), intensity);
    g.setGradientFill(juce::ColourGradient(core, centre, rim, lens.getTopLeft(), true));
    g.fillEllipse(lens);
}

juce::Font PanelFont(float height, bool bold) {
    return juce::Font(juce::FontOptions{height, bold ? juce::Font::bold : juce::Font::plain});
}

void DrawLabel(juce::Graphics& g, const juce::String& text, juce::Point<float> centre, juce::Colour colour) {
    g.setColour(colour);
    g.setFont(PanelFont(11.0f));
    g.drawText(text, juce::Rectangle<float>(120.0f, 16.0f).withCentre(centre), juce::Justification::centred,
               false);
}

void DrawPill(juce::Graphics& g, const juce::String& text, juce::Point<float> centre, juce::Colour colour) {
    const juce::Font font = PanelFont(11.0f);
    const float width = juce::GlyphArrangement::getStringWidth(font, text) + 12.0f;
    const auto pill = juce::Rectangle<float>(width, 15.0f).withCentre(centre);
    g.setColour(colour);
    g.fillRoundedRectangle(pill, 3.0f);
    g.setColour(juce::Colours::white);
    g.setFont(font);
    g.drawText(text, pill, juce::Justification::centred, false);
}

void DrawScrewHole(juce::Graphics& g, juce::Point<float> centre) {
    const auto hole = juce::Rectangle<float>(12.0f, 12.0f).withCentre(centre);
    g.setColour(juce::Colour(0xff3b3c3e));
    g.fillEllipse(hole);
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawEllipse(hole.expanded(0.5f), 1.0f);
}

void DrawOrnament(juce::Graphics& g, juce::Point<float> centre) {
    g.setColour(colours::kOrnament);
    for (const float radius : {50.0f, 56.0f, 70.0f}) {
        g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), 0.8f);
    }
    for (int i = 0; i < 48; ++i) {
        const float angle = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 48.0f;
        const auto dot = centre.getPointOnCircumference(63.0f, angle);
        g.fillEllipse(juce::Rectangle<float>(2.4f, 2.4f).withCentre(dot));
    }
}

}  // namespace panel
}  // namespace desktop
}  // namespace wf
