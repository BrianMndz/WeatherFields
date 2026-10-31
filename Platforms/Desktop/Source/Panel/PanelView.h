#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../RigView.h"

namespace wf {
namespace desktop {
namespace panel {

// A circle in a panel's reference coordinates: where a knob, button or LED
// goes.
struct Circle {
    float x;
    float y;
    float radius;

    juce::Point<float> Centre() const noexcept { return {x, y}; }
    juce::Rectangle<float> Bounds() const noexcept {
        return juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(Centre());
    }
};

// A rig view drawn as a module's front panel in the Clouds panel's look: a
// fixed reference size, scaled with its aspect ratio (0.8 to 2.5 times) and
// centred in its bounds, on the panel's shaded plate with a screw hole in
// each corner. Subclasses lay out and draw in reference coordinates.
class PanelView : public RigView {
public:
    // Fills the visible height, so the panel always fits without scrolling
    // down to its smallest scale.
    int HeightFor(int width, int visibleHeight) const override;

    void paint(juce::Graphics& g) final;
    void resized() final;

protected:
    // The panel's reference size.
    PanelView(float width, float height) noexcept : width_(width), height_(height) {}

    // Draws what is on the plate, with the reference transform applied.
    virtual void PaintPanel(juce::Graphics& g) = 0;

    // Places the child components, with Scaled().
    virtual void LayOut() = 0;

    // An area in reference coordinates, in the component's pixels.
    juce::Rectangle<int> Scaled(juce::Rectangle<float> area) const;

private:
    juce::AffineTransform Transform() const;

    float width_;
    float height_;
    float scale_ = 1.0f;
    juce::Point<float> origin_;
};

}  // namespace panel
}  // namespace desktop
}  // namespace wf
