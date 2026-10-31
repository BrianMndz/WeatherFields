#include "StratocumulusView.h"

#include "wf/stratocumulus/Rig.h"

namespace wf {
namespace desktop {

namespace {

using Rig = stratocumulus::Rig;

}  // namespace

StratocumulusView::StratocumulusView(AudioHost& host)
    : clouds_(host, Rig::kCloudsSlot), stratus_(host, Rig::kStratusSlot) {
    addAndMakeVisible(clouds_);
    addAndMakeVisible(stratus_);
}

bool StratocumulusView::SideBySide(int width) const noexcept {
    return width / 2 >= clouds_.MinimumWidth() && width - width / 2 >= stratus_.MinimumWidth();
}

int StratocumulusView::HeightFor(int width, int visibleHeight) const {
    if (SideBySide(width)) {
        return juce::jmax(clouds_.HeightFor(width / 2, visibleHeight),
                          stratus_.HeightFor(width - width / 2, visibleHeight));
    }
    // Stacked, each panel takes half the visible height, or its smallest.
    const int half = visibleHeight / 2;
    return clouds_.HeightFor(width, half) + stratus_.HeightFor(width, visibleHeight - half);
}

void StratocumulusView::resized() {
    auto area = getLocalBounds();
    if (SideBySide(getWidth())) {
        clouds_.setBounds(area.removeFromLeft(getWidth() / 2));
        stratus_.setBounds(area);
    } else {
        clouds_.setBounds(area.removeFromTop(getHeight() / 2));
        stratus_.setBounds(area);
    }
}

}  // namespace desktop
}  // namespace wf
