#pragma once

#include <array>
#include <cstddef>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../AudioHost.h"
#include "../Panel/PanelControls.h"
#include "../Panel/PanelView.h"
#include "wf/clouds/Engine.h"

namespace wf {
namespace desktop {

// A copy of the Clouds front panel, ported from CloudsEngine's JUCE editor
// (Source/PluginEditor, e7738d9), for a wf::clouds::Engine in `slot` of the
// host's rig, on a panel::PanelView; all positions are in the panel's
// reference coordinates.
//
// Unlike the CloudsEngine editor, it never touches the engine: controls send
// events through the host, and show the values the audio thread published,
// so changes from elsewhere (a quality change releasing freeze, later MIDI)
// appear on the panel. The LED meter reads the host's levels: the input
// times IN GAIN, or the output while frozen, as the module meters.
class CloudsPanel final : public panel::PanelView, private juce::Timer {
public:
    CloudsPanel(AudioHost& host, std::size_t slot);
    ~CloudsPanel() override;

private:
    using Engine = clouds::Engine;

    // What the four LEDs show, following the module's UI modes: the level
    // meter by default, the selected blend parameter (green) after pressing
    // B, all four blend values while BLEND turns, the quality (red) after
    // holding B, or the playback mode (pulsing orange) after holding it for
    // four seconds.
    enum class LedMode { Meter, Blending, BlendMeter, Quality, PlaybackMode };

    // The parameter BLEND sets, in the module's order: dry/wet, stereo
    // spread, feedback, reverb (Engine IDs kDryWet to kReverb).
    static constexpr int kBlendParameterCount = 4;

    float Value(ParamId id) const;
    float Latest(ParamId id) const;
    void Send(ParamId id, float value);
    ParamId BlendParam() const noexcept;

    void AddKnob(panel::PanelKnob& knob, ParamId id, int decimalPlaces);
    void SyncBlendKnob();
    void SyncControls();
    void SelectNextQuality();
    void SelectNextPlaybackMode();
    void UpdateModeTooltips(int mode);
    void ShowLeds(LedMode mode);
    void UpdateLeds();
    void timerCallback() override;
    void PaintPanel(juce::Graphics& g) override;
    void LayOut() override;

    AudioHost& host_;
    std::size_t slot_;
    std::array<std::size_t, Engine::kPlaybackMode + 1> indices_{};  // host index per ID

    // Declared before the controls so it outlives every one that uses it.
    panel::PanelLookAndFeel lookAndFeel_;

    panel::LitButton freezeButton_{"FREEZE", panel::colours::kLedGreen, juce::Colour(0xfff4fbd8)};
    std::array<panel::PanelLed, 4> leds_;
    panel::PanelPushButton blendButton_{"Blend parameter"};
    panel::PanelPushButton saveButton_{"Load/save"};
    panel::PanelPushButton triggerButton_{"Trigger"};

    panel::PanelKnob positionKnob_{panel::PanelKnob::Size::Large, panel::colours::kRedCap};
    panel::PanelKnob sizeKnob_{panel::PanelKnob::Size::Large, panel::colours::kTealCap};
    panel::PanelKnob pitchKnob_{panel::PanelKnob::Size::Large, panel::colours::kWhiteCap};

    panel::PanelKnob inputGainKnob_{panel::PanelKnob::Size::Small, panel::colours::kRedCap};
    panel::PanelKnob densityKnob_{panel::PanelKnob::Size::Small, panel::colours::kRedCap};
    panel::PanelKnob textureKnob_{panel::PanelKnob::Size::Small, panel::colours::kTealCap};
    panel::PanelKnob blendKnob_{panel::PanelKnob::Size::Small, panel::colours::kWhiteCap};

    LedMode ledMode_ = LedMode::Meter;
    juce::uint32 lastLedActivity_ = 0;
    int blendParameter_ = 0;
    int shownMode_ = -1;    // the playback mode the tooltips describe
    bool gate_ = false;     // TRIG held, as last sent
    bool bufferWarning_ = false;
};

}  // namespace desktop
}  // namespace wf
