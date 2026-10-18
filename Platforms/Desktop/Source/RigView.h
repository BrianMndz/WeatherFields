#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wf {
namespace desktop {

// What the main window shows for the current rig, inside a vertical
// scroller: the generic RigPanel, or a rig's own view (the Clouds panel).
// A view talks to the rig only through the AudioHost.
class RigView : public juce::Component {
public:
    // The height to give the view at `width` when `visibleHeight` is on
    // screen; above visibleHeight, the window scrolls.
    virtual int HeightFor(int width, int visibleHeight) const = 0;
};

}  // namespace desktop
}  // namespace wf
