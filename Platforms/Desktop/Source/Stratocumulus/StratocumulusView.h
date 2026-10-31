#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../AudioHost.h"
#include "../Clouds/CloudsPanel.h"
#include "../RigView.h"
#include "../Stratus/StratusPanel.h"

namespace wf {
namespace desktop {

// The Stratocumulus rig's view: its engines' own panels in signal order,
// Clouds (slot 0) then Stratus (slot 1), side by side when both fit at
// their smallest scale, else one above the other.
class StratocumulusView final : public RigView {
public:
    explicit StratocumulusView(AudioHost& host);

    int HeightFor(int width, int visibleHeight) const override;
    void resized() override;

private:
    // Side by side from this width: two panels at their smallest scale.
    bool SideBySide(int width) const noexcept;

    CloudsPanel clouds_;
    StratusPanel stratus_;
};

}  // namespace desktop
}  // namespace wf
