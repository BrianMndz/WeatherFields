#pragma once

#include <array>
#include <cstddef>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../AudioHost.h"
#include "../Clouds/PanelControls.h"
#include "../RigView.h"
#include "wf/stratus/Engine.h"

namespace wf {
namespace desktop {

// A front panel for a wf::stratus::Engine in `slot` of the host's rig, in
// the Clouds panel's look (proposal C of the 2026-10-31 mockups): REC and
// PLAY lit buttons, CLEAR, THRU with its LED, LOOP LEVEL, and a row of
// sixteen LEDs for the playhead. While the first take records, the row fills
// red over the maximum length, so the unlit LEDs are the time left; while
// the loop plays, a light walks across it once per lap, green, or amber when
// overdubbing. It scales with a fixed aspect ratio, centred in its bounds.
//
// Like the Clouds panel, it sends events through the host and shows only
// published values, including the engine's Position and Length readouts.
class StratusPanel final : public RigView, private juce::Timer {
public:
    StratusPanel(AudioHost& host, std::size_t slot);
    ~StratusPanel() override;

    int HeightFor(int width, int visibleHeight) const override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using Engine = stratus::Engine;

    static constexpr std::size_t kLedCount = 16;

    float Value(ParamId id) const;
    float Latest(ParamId id) const;
    void Send(ParamId id, float value);

    void SyncControls();
    void UpdateLeds();
    void timerCallback() override;

    juce::Rectangle<int> Scaled(juce::Rectangle<float> area) const;
    juce::AffineTransform Transform() const;

    AudioHost& host_;
    std::size_t slot_;
    std::array<std::size_t, Engine::kLength + 1> indices_{};  // host index per ID
    float maxSeconds_ = 0.0f;  // Position's range: the longest first take

    // Declared before the controls so it outlives every one that uses it.
    clouds_panel::PanelLookAndFeel lookAndFeel_;

    clouds_panel::LitButton recordButton_;
    clouds_panel::LitButton playButton_;
    clouds_panel::PanelPushButton clearButton_{"Clear"};
    clouds_panel::PanelPushButton thruButton_{"Thru"};
    clouds_panel::PanelLed thruLed_;
    clouds_panel::PanelKnob levelKnob_{clouds_panel::PanelKnob::Size::Large, clouds_panel::colours::kTealCap};
    std::array<clouds_panel::PanelLed, kLedCount> leds_;

    juce::TooltipWindow tooltipWindow_{this};

    float scale_ = 1.0f;
    juce::Point<float> origin_;
    float shownLength_ = -1.0f;  // the loop length under the LED row
};

}  // namespace desktop
}  // namespace wf
