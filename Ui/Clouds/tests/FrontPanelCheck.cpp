#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

#include "wf/Rig.h"
#include "wf/clouds/Engine.h"
#include "wf/clouds/FrontPanel.h"

namespace {

using wf::clouds::Engine;
using wf::clouds::FrontPanel;
using Display = FrontPanel::Display;

int failures = 0;

void Expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

constexpr float kSampleRate = 44100.0f;
constexpr std::size_t kFrames = 32;
constexpr float kCallbackMs = 32.0f / 44100.0f * 1000.0f;
constexpr float kTop = wf::controls::Knob::kInputTop;

// The real engine in a solo rig, and its panel, driven callback by callback
// with readings as the pins give them.
class Bench {
public:
    Bench() : memory_(new unsigned char[Engine::kMemoryBytes + wf::kMemoryAlignment]()) {
        const auto address = reinterpret_cast<std::uintptr_t>(memory_.get());
        const std::size_t offset = (wf::kMemoryAlignment - address % wf::kMemoryAlignment) % wf::kMemoryAlignment;
        const wf::PrepareStatus status = rig.Prepare(
            kSampleRate, kFrames, wf::MemoryBlock{memory_.get() + offset, rig.MemoryBytes(kSampleRate, kFrames)});
        Expect(status == wf::PrepareStatus::Ok, "the rig prepares");
        panel.Prepare(kCallbackMs);
        for (float& knob : knobs) knob = 0.5f * kTop;
    }

    float Value(Engine::Param id) const { return rig.Slot(0)->ParamValue(id); }

    void Run(float ms) {
        const int callbacks = static_cast<int>(std::ceil(ms / kCallbackMs));
        for (int i = 0; i < callbacks; ++i) panel.Update(knobs, buttons, rig);
    }

    void Set(Engine::Param id, float value) {
        rig.Handle(wf::ControlEvent{wf::ControlEvent::Type::Param, 0, id, value});
    }

    // Holds a button for `ms`, then releases it and lets the release land.
    void Press(int button, float ms = 60.0f) {
        buttons[button] = true;
        Run(ms);
        buttons[button] = false;
        Run(10.0f);
    }

    // Moves a knob and waits for it to settle.
    void Turn(int knob, float unit) {
        knobs[knob] = unit * kTop;
        Run(1000.0f);
    }

    wf::SoloRig<Engine> rig;
    FrontPanel panel{0};
    float knobs[FrontPanel::kKnobCount];
    bool buttons[FrontPanel::kButtonCount] = {};

private:
    std::unique_ptr<unsigned char[]> memory_;
};

bool Near(float a, float b, float tolerance = 0.01f) {
    return std::fabs(a - b) < tolerance;
}

void CheckStart() {
    Bench bench;
    bench.knobs[FrontPanel::kPositionKnob] = 0.1f * kTop;
    bench.knobs[FrontPanel::kSizeKnob] = 0.2f * kTop;
    bench.knobs[FrontPanel::kPitchKnob] = 1.0f * kTop;
    bench.knobs[FrontPanel::kDensityKnob] = 0.3f * kTop;
    bench.knobs[FrontPanel::kTextureKnob] = 0.4f * kTop;
    bench.knobs[FrontPanel::kBlendKnob] = 0.6f * kTop;
    bench.Run(kCallbackMs);

    Expect(Near(bench.Value(Engine::kPosition), 0.1f, 1e-5f), "the first reading sets POSITION");
    Expect(Near(bench.Value(Engine::kSize), 0.2f, 1e-5f), "SIZE");
    Expect(bench.Value(Engine::kPitch) == 24.0f, "PITCH, through the table's end");
    Expect(Near(bench.Value(Engine::kDensity), 0.3f, 1e-5f), "DENSITY");
    Expect(Near(bench.Value(Engine::kTexture), 0.4f, 1e-5f), "TEXTURE");
    Expect(Near(bench.Value(Engine::kDryWet), 0.6f * 1.05f - 0.025f, 1e-5f), "and BLEND dry/wet, stretched");
    Expect(bench.Value(Engine::kStereoSpread) == 0.0f, "not spread");
    Expect(bench.Value(Engine::kFeedback) == 0.0f, "not feedback");
    Expect(bench.Value(Engine::kReverb) == 0.0f, "not reverb");
    Expect(bench.Value(Engine::kInputGain) == 0.0f, "IN GAIN is not on the panel");
    Expect(bench.panel.CurrentDisplay() == Display::Meter, "the display starts on the meter");
    Expect(!bench.panel.CurrentFocus().active, "presenting nothing");
}

void CheckPitchTable() {
    Expect(FrontPanel::PitchFromKnob(0.0f) == -24.0f, "the table's bottom");
    Expect(FrontPanel::PitchFromKnob(1.0f) == 24.0f, "the table's top, in bounds");
    Expect(FrontPanel::PitchFromKnob(0.5f) == 0.0f, "a flat noon");
    Expect(FrontPanel::PitchFromKnob(0.48f) == 0.0f, "wide enough to find");
    Expect(FrontPanel::PitchFromKnob(0.52f) == 0.0f, "on both sides");
    Expect(FrontPanel::PitchFromKnob(0.75f) > 3.0f && FrontPanel::PitchFromKnob(0.75f) < 12.0f, "notches above");
    Expect(FrontPanel::PitchFromKnob(0.25f) < -3.0f && FrontPanel::PitchFromKnob(0.25f) > -12.0f, "and below");
}

void CheckQuietKnobsAndLastMoved() {
    Bench bench;
    bench.Run(100.0f);

    // A value from elsewhere (the browser panel) survives knobs at rest.
    bench.Set(Engine::kPosition, 0.9f);
    bench.Set(Engine::kSize, 0.8f);
    bench.Run(2000.0f);
    Expect(bench.Value(Engine::kPosition) == 0.9f, "a knob at rest leaves POSITION alone");
    Expect(bench.Value(Engine::kSize) == 0.8f, "and SIZE");

    // The worst noise, near the top of a pot, too.
    bench.knobs[FrontPanel::kPositionKnob] = 0.5f * kTop + 0.0005f;
    bench.Run(500.0f);
    bench.knobs[FrontPanel::kPositionKnob] = 0.5f * kTop - 0.0005f;
    bench.Run(500.0f);
    Expect(bench.Value(Engine::kPosition) == 0.9f, "so does its noise");

    // The last control moved wins.
    bench.Turn(FrontPanel::kPositionKnob, 0.2f);
    Expect(Near(bench.Value(Engine::kPosition), 0.2f), "a moved knob takes over");
    Expect(bench.Value(Engine::kSize) == 0.8f, "only its own parameter");

    bench.Turn(FrontPanel::kPitchKnob, 1.0f);
    Expect(bench.Value(Engine::kPitch) == 24.0f, "PITCH reaches its top");
    bench.Turn(FrontPanel::kPitchKnob, 0.0f);
    Expect(bench.Value(Engine::kPitch) == -24.0f, "and its bottom");
    bench.Turn(FrontPanel::kPositionKnob, 1.0f);
    Expect(bench.Value(Engine::kPosition) == 1.0f, "POSITION its top, at rest");
    bench.Turn(FrontPanel::kPositionKnob, 0.0f);
    Expect(bench.Value(Engine::kPosition) == 0.0f, "and its bottom");
}

// CvScaler::UpdateBlendParameters() on a value the module holds, as a
// reference for the panel's, which holds it in the engine.
struct FirmwareBlend {
    float value;
    float previous;
    float origin;

    void Select() { origin = previous; }

    void Update(float knob) {
        float delta = knob - previous;
        float skew = delta > 0.0f ? (1.001f - value) / (1.001f - previous) : (0.001f + value) / (0.001f + previous);
        skew = skew > 0.1f ? (skew < 10.0f ? skew : 10.0f) : 0.1f;
        if (std::fabs(knob - origin) < 0.02f) {
            delta = 0.0f;
        } else {
            origin = -1.0f;
        }
        value += skew * delta;
        value = value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
        previous = knob;
    }
};

void CheckBlendCatchUp() {
    Bench bench;
    bench.Run(100.0f);  // BLEND at noon sets dry/wet

    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.panel.CurrentDisplay() == Display::Blending, "a press shows what BLEND sets");
    Expect(bench.panel.BlendParameter() == Engine::kDryWet, "and selects nothing yet");
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.panel.BlendParameter() == Engine::kStereoSpread, "the next press selects spread");

    const float dryWet = bench.Value(Engine::kDryWet);
    bench.Set(Engine::kStereoSpread, 0.2f);

    // A sweep up and back, in steps of knob value, compared with the module's
    // formula fed the same values.
    wf::controls::Knob reference;
    reference.Configure(0.05f, kCallbackMs);
    reference.Update(0.5f * kTop);
    FirmwareBlend firmware{0.2f, reference.Value(), reference.Value()};
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < 400; ++i) {
            const float t = static_cast<float>(i) / 400.0f;
            const float unit = pass == 0 ? 0.5f + 0.5f * t : 1.0f - t;
            bench.knobs[FrontPanel::kBlendKnob] = unit * kTop;
            bench.Run(kCallbackMs);
            reference.Update(unit * kTop);
            firmware.Update(reference.Value());
        }
        Expect(Near(bench.Value(Engine::kStereoSpread), firmware.value, 1e-3f), "the module's skewed catch-up");
    }
    Expect(bench.Value(Engine::kDryWet) == dryWet, "only the selected parameter moves");

    // The catch-up converges at the ends, to within the module's 0.001.
    bench.Turn(FrontPanel::kBlendKnob, 1.0f);
    Expect(Near(bench.Value(Engine::kStereoSpread), 1.0f, 0.002f), "reaching the top nears the parameter's");
    bench.Turn(FrontPanel::kBlendKnob, 0.0f);
    Expect(Near(bench.Value(Engine::kStereoSpread), 0.0f, 0.002f), "and the bottom its");
}

void CheckBlendSelection() {
    Bench bench;
    bench.Run(100.0f);
    bench.Set(Engine::kFeedback, 0.7f);

    bench.Press(FrontPanel::kSelectButton);
    bench.Press(FrontPanel::kSelectButton);
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.panel.BlendParameter() == Engine::kFeedback, "two more presses reach feedback");
    wf::controls::Panel::Focus focus = bench.panel.CurrentFocus();
    Expect(focus.active && focus.slot == 0 && focus.id == Engine::kFeedback, "the selection is what it presents");

    // No jump: the knob is at noon and feedback at 0.7, and just selecting it,
    // or wobbling inside the dead zone, changes nothing.
    bench.Run(100.0f);
    Expect(bench.Value(Engine::kFeedback) == 0.7f, "selecting does not jump");
    bench.knobs[FrontPanel::kBlendKnob] = 0.515f * kTop;
    bench.Run(1000.0f);
    Expect(bench.Value(Engine::kFeedback) == 0.7f, "inside 2% of the origin nothing moves");

    // The dead zone comes from the selection, wherever the knob then is. Here
    // the knob has moved since start-up, so the first origin has gone.
    bench.Turn(FrontPanel::kBlendKnob, 0.4f);
    bench.Run(3500.0f);
    bench.Press(FrontPanel::kSelectButton);
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.panel.BlendParameter() == Engine::kReverb, "reverb");
    bench.Set(Engine::kReverb, 0.3f);
    bench.knobs[FrontPanel::kBlendKnob] = 0.415f * kTop;
    bench.Run(1000.0f);
    Expect(bench.Value(Engine::kReverb) == 0.3f, "each selection has its own dead zone");
    bench.Run(3500.0f);
    bench.Press(FrontPanel::kSelectButton);
    bench.Press(FrontPanel::kSelectButton);
    bench.Press(FrontPanel::kSelectButton);
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.panel.BlendParameter() == Engine::kFeedback, "back to feedback");
    bench.Set(Engine::kFeedback, 0.7f);
    bench.knobs[FrontPanel::kBlendKnob] = 0.5f * kTop;
    bench.Run(1000.0f);
    bench.Set(Engine::kFeedback, 0.7f);

    bench.knobs[FrontPanel::kBlendKnob] = 0.6f * kTop;
    bench.Run(1000.0f);
    Expect(bench.Value(Engine::kFeedback) > 0.7f, "past it, the parameter follows from where it was");
    Expect(bench.Value(Engine::kFeedback) < 0.8f, "not from where the knob is");

    // The turn above left the blend meter on: the first press only shows, the
    // next two select reverb, then dry/wet again.
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.panel.BlendParameter() == Engine::kFeedback, "a press from the blend meter only shows");
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.panel.BlendParameter() == Engine::kReverb, "the next selects reverb");
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.panel.BlendParameter() == Engine::kDryWet, "the selection wraps");

    // Dry/wet through its stretch, from a value the browser panel set.
    bench.Set(Engine::kDryWet, 0.25f);
    bench.Run(100.0f);
    bench.knobs[FrontPanel::kBlendKnob] = 0.65f * kTop;
    bench.Run(1000.0f);
    const float wet = bench.Value(Engine::kDryWet);
    Expect(wet > 0.25f && wet < 0.45f, "dry/wet catches up from the browser panel's value");
    bench.Turn(FrontPanel::kBlendKnob, 1.0f);
    Expect(bench.Value(Engine::kDryWet) == 1.0f, "and reaches 1");
    bench.Turn(FrontPanel::kBlendKnob, 0.0f);
    Expect(bench.Value(Engine::kDryWet) == 0.0f, "and 0");
}

void CheckFreeze() {
    Bench bench;
    bench.Run(100.0f);
    bench.buttons[FrontPanel::kFreezeButton] = true;
    bench.Run(1.0f);
    Expect(bench.Value(Engine::kFreeze) == 0.0f, "not before the debounce");
    bench.Run(5.0f);
    Expect(bench.Value(Engine::kFreeze) == 1.0f, "the press freezes");
    bench.Run(6000.0f);
    Expect(bench.Value(Engine::kFreeze) == 1.0f, "holding changes nothing");
    bench.buttons[FrontPanel::kFreezeButton] = false;
    bench.Run(10.0f);
    Expect(bench.Value(Engine::kFreeze) == 1.0f, "nor does the release");
    bench.Press(FrontPanel::kFreezeButton);
    Expect(bench.Value(Engine::kFreeze) == 0.0f, "the next press releases it");
    bench.Set(Engine::kFreeze, 1.0f);
    bench.Press(FrontPanel::kFreezeButton);
    Expect(bench.Value(Engine::kFreeze) == 0.0f, "it toggles the engine's state, not a copy");
}

void CheckQuality() {
    Bench bench;
    bench.Run(100.0f);
    bench.buttons[FrontPanel::kSelectButton] = true;
    bench.Run(900.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Meter, "not before a second");
    bench.Run(200.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Quality, "held for a second: the quality, still held");
    wf::controls::Panel::Focus focus = bench.panel.CurrentFocus();
    Expect(focus.active && focus.id == Engine::kQuality, "presented");
    bench.buttons[FrontPanel::kSelectButton] = false;
    bench.Run(10.0f);
    Expect(bench.Value(Engine::kQuality) == 0.0f, "the long press's release selects nothing");

    bench.Press(FrontPanel::kFreezeButton);
    Expect(bench.Value(Engine::kFreeze) == 1.0f, "frozen");
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.Value(Engine::kQuality) == 1.0f, "a press selects the next quality");
    Expect(bench.Value(Engine::kFreeze) == 0.0f, "which releases freeze");
    bench.Press(FrontPanel::kFreezeButton);
    Expect(bench.Value(Engine::kFreeze) == 1.0f, "and the next press of FREEZE freezes again");
    bench.Press(FrontPanel::kSelectButton);
    bench.Press(FrontPanel::kSelectButton);
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.Value(Engine::kQuality) == 0.0f, "the qualities wrap");

    // From the engine, so a browser panel's choice counts.
    bench.Set(Engine::kQuality, 2.0f);
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.Value(Engine::kQuality) == 3.0f, "next after the engine's quality, not a copy");

    bench.buttons[FrontPanel::kSelectButton] = true;
    bench.Run(1100.0f);
    bench.buttons[FrontPanel::kSelectButton] = false;
    bench.Run(10.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Meter, "a long press in the quality display leaves it");
    Expect(bench.Value(Engine::kQuality) == 3.0f, "changing nothing");
}

void CheckPlaybackMode() {
    Bench bench;
    bench.Run(100.0f);
    bench.buttons[FrontPanel::kSelectButton] = true;
    bench.Run(3900.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Quality, "before four seconds, the quality");
    bench.Run(200.0f);
    Expect(bench.panel.CurrentDisplay() == Display::PlaybackMode, "held for four: the playback mode");
    wf::controls::Panel::Focus focus = bench.panel.CurrentFocus();
    Expect(focus.active && focus.id == Engine::kPlaybackMode, "presented");
    bench.buttons[FrontPanel::kSelectButton] = false;
    bench.Run(10.0f);
    Expect(bench.panel.CurrentDisplay() == Display::PlaybackMode, "the release leaves it");
    Expect(bench.Value(Engine::kPlaybackMode) == 0.0f, "selecting nothing");

    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.Value(Engine::kPlaybackMode) == 1.0f, "a press selects the next mode");
    bench.Press(FrontPanel::kSelectButton);
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.Value(Engine::kPlaybackMode) == 3.0f, "up to spectral");
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.Value(Engine::kPlaybackMode) == 0.0f, "the modes wrap");
    Expect(bench.Value(Engine::kQuality) == 0.0f, "the quality stays");

    // Very long from the quality display as well.
    bench.Run(4000.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Meter, "back on the meter");
    bench.buttons[FrontPanel::kSelectButton] = true;
    bench.Run(1100.0f);
    bench.buttons[FrontPanel::kSelectButton] = false;
    bench.Run(10.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Quality, "the quality display");
    bench.buttons[FrontPanel::kSelectButton] = true;
    bench.Run(4100.0f);
    bench.buttons[FrontPanel::kSelectButton] = false;
    bench.Run(10.0f);
    Expect(bench.panel.CurrentDisplay() == Display::PlaybackMode, "from it, four seconds reach the mode");
}

void CheckDisplayTimeouts() {
    Bench bench;
    bench.Run(100.0f);

    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.panel.CurrentDisplay() == Display::Blending, "blending");
    bench.Run(2500.0f);
    bench.Press(FrontPanel::kSelectButton);
    bench.Run(2500.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Blending, "a press keeps it");
    bench.Run(1000.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Meter, "3 s after the last, the meter");
    Expect(!bench.panel.CurrentFocus().active, "presenting nothing");

    // A press counts from when it starts: one that begins just before the
    // display lapses still selects, however long it takes to release.
    bench.Press(FrontPanel::kSelectButton);
    const Engine::Param selected = bench.panel.BlendParameter();
    bench.Run(2950.0f);
    bench.Press(FrontPanel::kSelectButton, 200.0f);
    Expect(bench.panel.BlendParameter() != selected, "a press as the display lapses still selects");

    // And again from its release.
    bench.Run(100.0f);
    bench.Press(FrontPanel::kSelectButton, 900.0f);
    bench.Run(2500.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Blending, "the display lasts 3 s from the release");

    // FREEZE keeps a display up too, as any switch does on the module.
    bench.Run(3500.0f);
    bench.Press(FrontPanel::kSelectButton);
    bench.Run(2500.0f);
    bench.Press(FrontPanel::kFreezeButton);
    bench.Run(1000.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Blending, "FREEZE keeps the display up");
    bench.Run(3500.0f);

    bench.Turn(FrontPanel::kBlendKnob, 0.8f);
    Expect(bench.panel.CurrentDisplay() == Display::BlendMeter, "turning BLEND shows the four values");
    Expect(!bench.panel.CurrentFocus().active, "which the screen always shows");
    bench.Run(3500.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Meter, "and lapses");

    bench.Press(FrontPanel::kSelectButton);
    bench.Turn(FrontPanel::kBlendKnob, 0.2f);
    Expect(bench.panel.CurrentDisplay() == Display::BlendMeter, "turning it leaves the blending display");
    const Engine::Param blending = bench.panel.BlendParameter();
    bench.Press(FrontPanel::kSelectButton);
    Expect(bench.panel.CurrentDisplay() == Display::Blending, "a press from the blend meter returns there");
    Expect(bench.panel.BlendParameter() == blending, "selecting nothing");
    bench.Run(3500.0f);

    // In the quality display BLEND still sets its parameter but changes no display.
    bench.buttons[FrontPanel::kSelectButton] = true;
    bench.Run(1100.0f);
    bench.buttons[FrontPanel::kSelectButton] = false;
    bench.Run(10.0f);
    const float before = bench.Value(blending);
    bench.knobs[FrontPanel::kBlendKnob] = 0.7f * kTop;
    bench.Run(500.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Quality, "the quality display stays while BLEND turns");
    Expect(bench.Value(blending) != before, "and BLEND still works");
    bench.Run(3500.0f);
    Expect(bench.panel.CurrentDisplay() == Display::Meter, "and it lapses");
    Expect(bench.Value(Engine::kQuality) == 0.0f, "no quality was selected on the way");
}

void CheckButtonsHeldAtStart() {
    Bench bench;
    bench.buttons[FrontPanel::kFreezeButton] = true;
    bench.buttons[FrontPanel::kSelectButton] = true;
    bench.Run(6000.0f);
    Expect(bench.Value(Engine::kFreeze) == 0.0f, "FREEZE held at start-up does not freeze");
    Expect(bench.panel.CurrentDisplay() == Display::Meter, "B held at start-up shows nothing");
    bench.buttons[FrontPanel::kFreezeButton] = false;
    bench.buttons[FrontPanel::kSelectButton] = false;
    bench.Run(10.0f);
    Expect(bench.Value(Engine::kFreeze) == 0.0f, "their releases do nothing");
    Expect(bench.panel.CurrentDisplay() == Display::Meter, "nor B's");
    bench.Press(FrontPanel::kFreezeButton);
    Expect(bench.Value(Engine::kFreeze) == 1.0f, "the next press works");
}

void CheckPrepareAgain() {
    Bench bench;
    bench.Run(100.0f);
    bench.Press(FrontPanel::kSelectButton);
    bench.Press(FrontPanel::kSelectButton);
    bench.panel.Prepare(kCallbackMs);
    Expect(bench.panel.CurrentDisplay() == Display::Meter, "Prepare() returns to the meter");
    Expect(bench.panel.BlendParameter() == Engine::kDryWet, "and to dry/wet");
    bench.knobs[FrontPanel::kPositionKnob] = 0.3f * kTop;
    bench.Run(kCallbackMs);
    Expect(Near(bench.Value(Engine::kPosition), 0.3f, 1e-5f), "and starts anew from the knobs");
}

}  // namespace

int main() {
    CheckStart();
    CheckPitchTable();
    CheckQuietKnobsAndLastMoved();
    CheckBlendCatchUp();
    CheckBlendSelection();
    CheckFreeze();
    CheckQuality();
    CheckPlaybackMode();
    CheckDisplayTimeouts();
    CheckButtonsHeldAtStart();
    CheckPrepareAgain();
    if (failures != 0) return 1;
    std::cout << "front panel check passed\n";
    return 0;
}
