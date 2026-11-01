#pragma once

#include <cstddef>
#include <cstdint>

#include "wf/clouds/Engine.h"
#include "wf/controls/Button.h"
#include "wf/controls/Knob.h"
#include "wf/controls/Panel.h"

namespace wf {
namespace clouds {

// Clouds' front panel on real knobs and buttons, following the module's Ui
// (eurorack/clouds/ui.cc) and CvScaler (cv_scaler.cc):
//
//   FREEZE   a press toggles freeze
//   B        a press shows which parameter BLEND sets, and while it shows,
//            selects the next (dry/wet, spread, feedback, reverb); held for 1 s
//            it shows the quality, held for 4 s the playback mode, where
//            presses select the next; a display lapses 3 s after the last
//            press or turn of BLEND
//   BLEND    sets the selected parameter with the module's skewed catch-up:
//            it starts where the parameter is, not where the knob is, and
//            converges as the knob nears an end. Turning it shows all four
//            values
//
// The other knobs are absolute. The browser panel can change every value too:
// the last control moved wins, and BLEND catches up from the browser's value.
//
// Deliberate deviations from the firmware: knobs are quiet at rest
// (controls::Knob); the first reading sets the knobs' parameters, with BLEND
// setting dry/wet, rather than the saved settings, as nothing is saved yet;
// no save, load or calibration, as there is no third button and nothing to
// save to; freeze held has no long press, which on the module only keeps its
// display from lapsing. A release at 999 ms is a short press (the module's
// counts as long by 1 ms).
//
// Feedback is the OLED: CurrentFocus() gives what the module's LEDs would be
// presenting for selection, which the screen shows (OledScreen). There are no
// LEDs yet.
class FrontPanel final : public controls::Panel {
public:
    enum KnobIndex {
        kPositionKnob,
        kSizeKnob,
        kPitchKnob,
        kDensityKnob,
        kTextureKnob,
        kBlendKnob,
        kKnobCount,
    };

    enum ButtonIndex {
        kFreezeButton,
        kSelectButton,  // the module's B
        kButtonCount,
    };

    // The module's UI modes that apply here.
    enum class Display {
        Meter,
        BlendMeter,
        Blending,
        Quality,
        PlaybackMode,
    };

    // `slot`: the rig slot that holds the Clouds engine.
    explicit FrontPanel(std::size_t slot) noexcept : slot_(static_cast<std::uint8_t>(slot)) {}

    std::size_t KnobCount() const noexcept override { return kKnobCount; }
    std::size_t ButtonCount() const noexcept override { return kButtonCount; }

    void Prepare(float callbackMs) noexcept override;
    void Update(const float* knobs, const bool* buttons, Rig& rig) noexcept override;
    Focus CurrentFocus() const noexcept override;

    Display CurrentDisplay() const noexcept { return display_; }

    // The parameter BLEND sets.
    Engine::Param BlendParameter() const noexcept;

    // The pitch knob's position as semitones: the module's table, with its
    // notches and its flat noon.
    static float PitchFromKnob(float knob) noexcept;

private:
    void Send(Rig& rig, ParamId id, float value) const noexcept;
    float ReadBlend(const Rig& rig, int parameter) const noexcept;
    void SendBlend(Rig& rig, int parameter, float blend) noexcept;
    void UpdateBlend(float knob, Rig& rig) noexcept;
    void UpdateFreeze(Rig& rig) noexcept;
    void UpdateSelect(Rig& rig) noexcept;
    void OnShortPress(Rig& rig) noexcept;
    void NextChoice(Rig& rig, ParamId id) const noexcept;
    void UpdateDisplay() noexcept;

    std::uint8_t slot_;
    float callbackMs_ = 1.0f;
    double clockMs_ = 0.0;
    double lastActivityMs_ = 0.0;  // the module's event queue idle time
    controls::Knob knobs_[kKnobCount];
    controls::Button buttons_[kButtonCount];

    Display display_ = Display::Meter;
    double selectPressMs_ = 0.0;
    bool longPressArmed_ = false;
    bool veryLongPressArmed_ = false;

    // CvScaler::UpdateBlendParameters().
    static constexpr int kBlendCount = 4;
    float blend_[kBlendCount] = {};      // the module's values, before dry/wet's stretch and clamp
    float blendSent_[kBlendCount] = {};  // what the engine got for them; negative for nothing yet
    int blendParameter_ = 0;
    bool blendStarted_ = false;
    float previousBlendKnob_ = 0.0f;
    float blendKnobOrigin_ = 0.0f;
    float blendKnobQuantized_ = 0.0f;
    bool blendKnobTouched_ = false;
};

}  // namespace clouds
}  // namespace wf
