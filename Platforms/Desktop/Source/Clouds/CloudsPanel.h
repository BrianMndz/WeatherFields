#pragma once

#include <array>
#include <cstddef>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../AudioHost.h"
#include "../RigView.h"
#include "PanelControls.h"
#include "wf/clouds/Engine.h"

namespace wf {
namespace desktop {

// A copy of the Clouds front panel, ported from CloudsEngine's JUCE editor
// (Source/PluginEditor, e7738d9), for a wf::clouds::Engine in `slot` of the
// host's rig. It scales with a fixed aspect ratio, centred in its bounds; all
// positions are in the panel's reference coordinates.
//
// Unlike the CloudsEngine editor, it never touches the engine: controls send
// events through the host, and show the values the audio thread published,
// so changes from elsewhere (a quality change releasing freeze, later MIDI)
// appear on the panel. The LED meter reads the host's levels: the input
// times IN GAIN, or the output while frozen, as the module meters.
class CloudsPanel final : public RigView, private juce::Timer {
public:
    CloudsPanel(AudioHost& host, std::size_t slot);
    ~CloudsPanel() override;

    // Fills the visible height, so the panel always fits without scrolling
    // down to its smallest scale.
    int HeightFor(int width, int visibleHeight) const override;

    void paint(juce::Graphics& g) override;
    void resized() override;

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

    void AddKnob(clouds_panel::PanelKnob& knob, ParamId id, int decimalPlaces);
    void SyncBlendKnob();
    void SyncControls();
    void SelectNextQuality();
    void SelectNextPlaybackMode();
    void UpdateModeTooltips(int mode);
    void ShowLeds(LedMode mode);
    void UpdateLeds();
    void timerCallback() override;

    juce::Rectangle<int> Scaled(juce::Rectangle<float> area) const;
    juce::AffineTransform Transform() const;

    AudioHost& host_;
    std::size_t slot_;
    std::array<std::size_t, Engine::kPlaybackMode + 1> indices_{};  // host index per ID

    // Declared before the controls so it outlives every one that uses it.
    clouds_panel::PanelLookAndFeel lookAndFeel_;

    clouds_panel::FreezeButton freezeButton_;
    std::array<clouds_panel::PanelLed, 4> leds_;
    clouds_panel::PanelPushButton blendButton_{"Blend parameter"};
    clouds_panel::PanelPushButton saveButton_{"Load/save"};
    clouds_panel::PanelPushButton triggerButton_{"Trigger"};

    clouds_panel::PanelKnob positionKnob_{clouds_panel::PanelKnob::Size::Large, clouds_panel::colours::kRedCap};
    clouds_panel::PanelKnob sizeKnob_{clouds_panel::PanelKnob::Size::Large, clouds_panel::colours::kTealCap};
    clouds_panel::PanelKnob pitchKnob_{clouds_panel::PanelKnob::Size::Large, clouds_panel::colours::kWhiteCap};

    clouds_panel::PanelKnob inputGainKnob_{clouds_panel::PanelKnob::Size::Small, clouds_panel::colours::kRedCap};
    clouds_panel::PanelKnob densityKnob_{clouds_panel::PanelKnob::Size::Small, clouds_panel::colours::kRedCap};
    clouds_panel::PanelKnob textureKnob_{clouds_panel::PanelKnob::Size::Small, clouds_panel::colours::kTealCap};
    clouds_panel::PanelKnob blendKnob_{clouds_panel::PanelKnob::Size::Small, clouds_panel::colours::kWhiteCap};

    juce::TooltipWindow tooltipWindow_{this};

    float scale_ = 1.0f;
    juce::Point<float> origin_;

    LedMode ledMode_ = LedMode::Meter;
    juce::uint32 lastLedActivity_ = 0;
    int blendParameter_ = 0;
    int shownMode_ = -1;    // the playback mode the tooltips describe
    bool gate_ = false;     // TRIG held, as last sent
    bool bufferWarning_ = false;
};

}  // namespace desktop
}  // namespace wf
