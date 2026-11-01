#include "wf/clouds/FrontPanel.h"

#include <cmath>

#include "clouds/resources.h"
#include "stmlib/dsp/dsp.h"
#include "wf/Events.h"

namespace wf {
namespace clouds {

namespace {

// The module's Ui.
constexpr double kLongPressMs = 1000.0;
constexpr double kVeryLongPressMs = 4000.0;
constexpr double kDisplayTimeoutMs = 3000.0;

// CvScaler's filter coefficients, per its 1 kHz Read(), in knob order.
const float kKnobCoefficients[FrontPanel::kKnobCount] = {0.05f, 0.01f, 0.01f, 0.01f, 0.01f, 0.05f};

// CvScaler::UpdateBlendParameters().
constexpr float kBlendTouch = 0.02f;
constexpr float kBlendOriginDeadZone = 0.02f;
constexpr float kNoOrigin = -1.0f;

// CvScaler::Read() stretches dry/wet so that both ends are reachable.
constexpr float kDryWetScale = 1.05f;
constexpr float kDryWetOffset = 0.025f;

// In the module's BlendParameter order.
const Engine::Param kBlendParams[] = {Engine::kDryWet, Engine::kStereoSpread, Engine::kFeedback, Engine::kReverb};

constexpr int kChoiceCount = 4;  // the qualities, and the playback modes

float Constrain(float value) {
    return value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
}

}  // namespace

float FrontPanel::PitchFromKnob(float knob) noexcept {
    // Interpolate() reads the entry after the index, and the module's ADC
    // never reaches exactly 1.
    if (knob >= 1.0f) return ::clouds::lut_quantized_pitch[LUT_QUANTIZED_PITCH_SIZE - 1];
    return stmlib::Interpolate(::clouds::lut_quantized_pitch, knob, static_cast<float>(LUT_QUANTIZED_PITCH_SIZE - 1));
}

void FrontPanel::Prepare(float callbackMs) noexcept {
    callbackMs_ = callbackMs;
    clockMs_ = 0.0;
    lastActivityMs_ = 0.0;
    for (int i = 0; i < kKnobCount; ++i) knobs_[i].Configure(kKnobCoefficients[i], callbackMs);
    for (controls::Button& button : buttons_) button.Reset();
    display_ = Display::Meter;
    longPressArmed_ = false;
    veryLongPressArmed_ = false;
    for (float& sent : blendSent_) sent = -1.0f;
    blendParameter_ = 0;
    blendStarted_ = false;
    blendKnobTouched_ = false;
}

void FrontPanel::Update(const float* knobs, const bool* buttons, Rig& rig) noexcept {
    clockMs_ += static_cast<double>(callbackMs_);

    if (knobs_[kPositionKnob].Update(knobs[kPositionKnob])) {
        Send(rig, Engine::kPosition, knobs_[kPositionKnob].Value());
    }
    if (knobs_[kSizeKnob].Update(knobs[kSizeKnob])) Send(rig, Engine::kSize, knobs_[kSizeKnob].Value());
    if (knobs_[kPitchKnob].Update(knobs[kPitchKnob])) {
        Send(rig, Engine::kPitch, PitchFromKnob(knobs_[kPitchKnob].Value()));
    }
    if (knobs_[kDensityKnob].Update(knobs[kDensityKnob])) Send(rig, Engine::kDensity, knobs_[kDensityKnob].Value());
    if (knobs_[kTextureKnob].Update(knobs[kTextureKnob])) Send(rig, Engine::kTexture, knobs_[kTextureKnob].Value());
    knobs_[kBlendKnob].Update(knobs[kBlendKnob]);
    UpdateBlend(knobs_[kBlendKnob].Value(), rig);

    for (int i = 0; i < kButtonCount; ++i) buttons_[i].Update(buttons[i]);
    UpdateFreeze(rig);
    UpdateSelect(rig);
    UpdateDisplay();
}

controls::Panel::Focus FrontPanel::CurrentFocus() const noexcept {
    switch (display_) {
        case Display::Blending: return Focus{true, slot_, BlendParameter()};
        case Display::Quality: return Focus{true, slot_, Engine::kQuality};
        case Display::PlaybackMode: return Focus{true, slot_, Engine::kPlaybackMode};
        case Display::Meter:
        case Display::BlendMeter: break;
    }
    return Focus{false, 0, 0};
}

Engine::Param FrontPanel::BlendParameter() const noexcept {
    return kBlendParams[blendParameter_];
}

void FrontPanel::Send(Rig& rig, ParamId id, float value) const noexcept {
    rig.Handle(ControlEvent{ControlEvent::Type::Param, slot_, id, value});
}

// The module's blend_[], kept here since the engine's dry/wet is stretched and
// clamped, which would lose the dead zones at its ends; unless the engine holds
// something else than was sent, which only the browser panel can have done.
float FrontPanel::ReadBlend(const Rig& rig, int parameter) const noexcept {
    const float value = rig.Slot(slot_)->ParamValue(kBlendParams[parameter]);
    if (value == blendSent_[parameter]) return blend_[parameter];
    return kBlendParams[parameter] == Engine::kDryWet ? (value + kDryWetOffset) / kDryWetScale : value;
}

void FrontPanel::SendBlend(Rig& rig, int parameter, float blend) noexcept {
    const float value = kBlendParams[parameter] == Engine::kDryWet ? blend * kDryWetScale - kDryWetOffset : blend;
    blend_[parameter] = blend;
    blendSent_[parameter] = Constrain(value);
    Send(rig, kBlendParams[parameter], blendSent_[parameter]);
}

// CvScaler::UpdateBlendParameters() without the CV. `knob` holds still at rest,
// so noise cannot walk the parameter, which on the module it does.
void FrontPanel::UpdateBlend(float knob, Rig& rig) noexcept {
    if (!blendStarted_) {
        blendStarted_ = true;
        blendKnobQuantized_ = knob;
        previousBlendKnob_ = knob;
        blendKnobOrigin_ = knob;
        SendBlend(rig, blendParameter_, knob);
        return;
    }

    blendKnobTouched_ = std::fabs(knob - blendKnobQuantized_) > kBlendTouch;
    if (blendKnobTouched_) blendKnobQuantized_ = knob;

    float delta = knob - previousBlendKnob_;
    if (std::fabs(knob - blendKnobOrigin_) < kBlendOriginDeadZone) {
        delta = 0.0f;
    } else {
        blendKnobOrigin_ = kNoOrigin;
    }
    if (delta != 0.0f) {
        const float parameter = ReadBlend(rig, blendParameter_);
        float skewRatio = delta > 0.0f ? (1.001f - parameter) / (1.001f - previousBlendKnob_)
                                       : (0.001f + parameter) / (0.001f + previousBlendKnob_);
        skewRatio = skewRatio > 0.1f ? (skewRatio < 10.0f ? skewRatio : 10.0f) : 0.1f;
        SendBlend(rig, blendParameter_, Constrain(parameter + skewRatio * delta));
    }
    previousBlendKnob_ = knob;
}

// The engine's value, not a copy: a quality change can release freeze.
void FrontPanel::UpdateFreeze(Rig& rig) noexcept {
    const controls::Button& button = buttons_[kFreezeButton];
    if (button.JustPressed()) {
        const bool frozen = rig.Slot(slot_)->ParamValue(Engine::kFreeze) > 0.5f;
        Send(rig, Engine::kFreeze, frozen ? 0.0f : 1.0f);
    }
    if (button.JustPressed() || button.JustReleased()) lastActivityMs_ = clockMs_;
}

// Ui::Poll()'s timing: the long press fires while B is held, and a press that
// fired it is no short press; the very long press fires later in the same
// hold.
void FrontPanel::UpdateSelect(Rig& rig) noexcept {
    const controls::Button& button = buttons_[kSelectButton];
    if (button.JustPressed()) {
        selectPressMs_ = clockMs_;
        longPressArmed_ = true;
        veryLongPressArmed_ = true;
        lastActivityMs_ = clockMs_;
    }
    if (button.Pressed()) {
        const double held = clockMs_ - selectPressMs_;
        if (longPressArmed_ && held > kLongPressMs) {
            longPressArmed_ = false;
            display_ = display_ == Display::Quality ? Display::Meter : Display::Quality;
            lastActivityMs_ = clockMs_;
        }
        if (veryLongPressArmed_ && held > kVeryLongPressMs) {
            veryLongPressArmed_ = false;
            display_ = Display::PlaybackMode;
            lastActivityMs_ = clockMs_;
        }
    }
    if (button.JustReleased()) {
        if (longPressArmed_) OnShortPress(rig);
        longPressArmed_ = false;
        veryLongPressArmed_ = false;
        lastActivityMs_ = clockMs_;
    }
}

// Ui::OnSwitchReleased().
void FrontPanel::OnShortPress(Rig& rig) noexcept {
    switch (display_) {
        case Display::Meter:
        case Display::BlendMeter:
            display_ = Display::Blending;
            break;
        case Display::Blending:
            blendParameter_ = (blendParameter_ + 1) % kBlendCount;
            blendKnobOrigin_ = previousBlendKnob_;
            break;
        case Display::Quality:
            NextChoice(rig, Engine::kQuality);
            break;
        case Display::PlaybackMode:
            NextChoice(rig, Engine::kPlaybackMode);
            break;
    }
}

void FrontPanel::NextChoice(Rig& rig, ParamId id) const noexcept {
    const int current = static_cast<int>(rig.Slot(slot_)->ParamValue(id));
    Send(rig, id, static_cast<float>((current + 1) % kChoiceCount));
}

// The end of Ui::DoEvents().
void FrontPanel::UpdateDisplay() noexcept {
    if (blendKnobTouched_ && (display_ == Display::Meter || display_ == Display::BlendMeter ||
                              display_ == Display::Blending)) {
        lastActivityMs_ = clockMs_;
        display_ = Display::BlendMeter;
    }
    if (clockMs_ - lastActivityMs_ > kDisplayTimeoutMs) {
        lastActivityMs_ = clockMs_;
        display_ = Display::Meter;
    }
}

}  // namespace clouds
}  // namespace wf
