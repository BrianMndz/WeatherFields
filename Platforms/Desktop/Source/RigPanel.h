#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioHost.h"
#include "RigView.h"

namespace wf {
namespace desktop {

// A panel generated from the rig's parameter tables: per slot, a knob for
// each Continuous parameter, a menu of labels for each Choice, a latching
// button for each Toggle and a button for each Trigger. Controls send
// through the host and show only what the audio thread published, so state
// the engine changes itself (Clouds' freeze after a quality change) appears
// here too. Built for the host's current rig; rebuild it after SetRig().
class RigPanel final : public RigView, private juce::Timer {
public:
    explicit RigPanel(AudioHost& host);
    ~RigPanel() override;

    // The height the controls need at `width`.
    int HeightFor(int width, int visibleHeight) const override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Control {
        std::size_t index;  // the host's flat parameter index
        std::size_t slot;
        ParamKind kind;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> menu;
        std::unique_ptr<juce::TextButton> button;
    };

    void timerCallback() override;

    // Places the controls when `apply` is set; returns the height used.
    int Layout(int width, bool apply) const;

    AudioHost& host_;
    std::vector<std::unique_ptr<juce::Label>> titles_;  // one per slot
    std::vector<std::unique_ptr<Control>> controls_;    // in the host's order
};

}  // namespace desktop
}  // namespace wf
