#include "PanelView.h"

#include "PanelControls.h"

namespace wf {
namespace desktop {
namespace panel {

namespace {

constexpr float kMinimumScale = 0.8f;
constexpr float kMaximumScale = 2.5f;

// The screw holes' distances from the plate's left and right edges, and
// from its top and bottom.
constexpr float kScrewInset = 46.0f;
constexpr float kScrewTop = 14.0f;
constexpr float kScrewBottom = 19.0f;

}  // namespace

int PanelView::HeightFor(int, int visibleHeight) const {
    return juce::jmax(visibleHeight, juce::roundToInt(height_ * kMinimumScale));
}

int PanelView::MinimumWidth() const noexcept {
    return juce::roundToInt(width_ * kMinimumScale);
}

juce::AffineTransform PanelView::Transform() const {
    return juce::AffineTransform::scale(scale_).translated(origin_);
}

juce::Rectangle<int> PanelView::Scaled(juce::Rectangle<float> area) const {
    return area.transformedBy(Transform()).getSmallestIntegerContainer();
}

void PanelView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff2a2a2d));
    g.addTransform(Transform());

    const auto plate = juce::Rectangle<float>(width_, height_);
    g.setGradientFill(juce::ColourGradient(colours::kPanel, plate.getTopLeft(), colours::kPanelShade,
                                           plate.getBottomLeft(), false));
    g.fillRect(plate);

    for (const float y : {kScrewTop, height_ - kScrewBottom}) {
        for (const float x : {kScrewInset, width_ - kScrewInset}) DrawScrewHole(g, {x, y});
    }

    PaintPanel(g);
}

void PanelView::resized() {
    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());
    scale_ = juce::jlimit(kMinimumScale, kMaximumScale, juce::jmin(width / width_, height / height_));
    origin_ = {juce::jmax(0.0f, (width - width_ * scale_) * 0.5f),
               juce::jmax(0.0f, (height - height_ * scale_) * 0.5f)};
    LayOut();
}

}  // namespace panel
}  // namespace desktop
}  // namespace wf
