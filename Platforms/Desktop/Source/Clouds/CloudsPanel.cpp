#include "CloudsPanel.h"

#include <cmath>
#include <cstdint>

namespace wf {
namespace desktop {

namespace {

namespace colours = clouds_panel::colours;

// Reference coordinates, measured from CloudsEngine's
// references/cloudsfrontpanel.png (416 x 554 pixels). The module's jack
// section is left out: there are no CV inputs, and the audio comes from the
// host. The panel therefore ends below the knobs, with a row for the
// trigger.
namespace layout {

constexpr float kWidth = 416.0f;
constexpr float kHeight = 440.0f;
constexpr float kMinimumScale = 0.8f;
constexpr float kMaximumScale = 2.5f;

struct Circle {
    float x;
    float y;
    float radius;

    juce::Point<float> Centre() const noexcept { return {x, y}; }
    juce::Rectangle<float> Bounds() const noexcept {
        return juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(Centre());
    }
};

constexpr Circle kFreeze{56.0f, 82.5f, 20.0f};
constexpr float kLedX[]{141.5f, 187.5f, 231.5f, 276.0f};
constexpr float kLedY = 82.5f;
constexpr float kLedRadius = 11.0f;
constexpr float kQualityIconY = 61.0f;
constexpr float kBlendIconY = 107.5f;
constexpr float kIconRadius = 7.5f;
constexpr Circle kBlendButton{327.5f, 82.5f, 11.0f};
constexpr Circle kSaveButton{370.0f, 82.5f, 11.0f};

constexpr Circle kPosition{92.5f, 175.0f, 41.0f};
constexpr Circle kSize{210.0f, 175.0f, 41.0f};
constexpr Circle kPitch{327.5f, 175.0f, 41.0f};
constexpr float kLargeLabelY = 231.0f;

constexpr Circle kInputGain{67.5f, 292.5f, 29.0f};
constexpr Circle kDensity{161.0f, 292.5f, 29.0f};
constexpr Circle kTexture{256.0f, 292.5f, 29.0f};
constexpr Circle kBlend{350.0f, 291.0f, 29.0f};
constexpr float kSmallLabelY = 339.0f;

constexpr float kDividerY = 361.5f;
constexpr float kBottomRowY = 392.0f;
constexpr Circle kTrigger{370.0f, kBottomRowY, 11.0f};
constexpr float kScrewY[]{14.0f, kHeight - 19.0f};

}  // namespace layout

// As in the module's UI (eurorack/clouds/ui.cc): the LEDs return to the
// meter this long after the last button press or BLEND movement.
constexpr juce::uint32 kLedTimeoutMilliseconds = 3000;
constexpr int kRefreshHz = 30;

// A spectral hop, an FFT pair every 1024 engine frames, runs whole in the
// callback that completes its engine block: about 0.4 ms in a Release build
// on an Apple Silicon Mac. Shorter device buffers overrun their callback, so
// spectral mode needs at least this many frames per buffer.
constexpr int kMinSpectralBufferFrames = 64;

constexpr int kQualityCount = 4;
constexpr int kPlaybackModeCount = 4;
constexpr int kSpectral = static_cast<int>(clouds::PlaybackMode::Spectral);

juce::Font PanelFont(float height, bool bold = true) {
    return juce::Font(juce::FontOptions{height, bold ? juce::Font::bold : juce::Font::plain});
}

// Labels on the panel: plain black text, or white text on a coloured pill.
void DrawLabel(juce::Graphics& g, const juce::String& text, juce::Point<float> centre,
               juce::Colour colour = colours::kInk) {
    g.setColour(colour);
    g.setFont(PanelFont(11.0f));
    g.drawText(text, juce::Rectangle<float>(120.0f, 16.0f).withCentre(centre), juce::Justification::centred,
               false);
}

void DrawPill(juce::Graphics& g, const juce::String& text, juce::Point<float> centre, juce::Colour colour) {
    const juce::Font font = PanelFont(11.0f);
    const float width = juce::GlyphArrangement::getStringWidth(font, text) + 12.0f;
    const auto pill = juce::Rectangle<float>(width, 15.0f).withCentre(centre);
    g.setColour(colour);
    g.fillRoundedRectangle(pill, 3.0f);
    g.setColour(juce::Colours::white);
    g.setFont(font);
    g.drawText(text, pill, juce::Justification::centred, false);
}

void DrawScrewHole(juce::Graphics& g, juce::Point<float> centre) {
    const auto hole = juce::Rectangle<float>(12.0f, 12.0f).withCentre(centre);
    g.setColour(juce::Colour(0xff3b3c3e));
    g.fillEllipse(hole);
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawEllipse(hole.expanded(0.5f), 1.0f);
}

// A simplified version of the ornamental rings behind the large knobs.
void DrawOrnament(juce::Graphics& g, juce::Point<float> centre) {
    g.setColour(colours::kOrnament);
    for (const float radius : {50.0f, 56.0f, 70.0f}) {
        g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), 0.8f);
    }
    for (int i = 0; i < 48; ++i) {
        const float angle = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 48.0f;
        const auto dot = centre.getPointOnCircumference(63.0f, angle);
        g.fillEllipse(juce::Rectangle<float>(2.4f, 2.4f).withCentre(dot));
    }
}

// The icons beside the LEDs: white glyphs on red (audio quality) or teal
// (blend parameter) discs, and a pencil on a red square for button C.
enum class Glyph { DoubleRing, Ring, RingWithMarks, Droplet, Spread, Feedback, Cube, Pencil };

void DrawGlyph(juce::Graphics& g, Glyph glyph, juce::Point<float> c) {
    g.setColour(juce::Colours::white);
    const auto ring = [&g](juce::Point<float> centre, float radius) {
        g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), 1.2f);
    };

    switch (glyph) {
        case Glyph::DoubleRing:
            ring(c.translated(-1.6f, 0.0f), 2.6f);
            ring(c.translated(1.6f, 0.0f), 2.6f);
            break;

        case Glyph::Ring:
            ring(c, 3.0f);
            break;

        case Glyph::RingWithMarks:
            ring(c, 2.8f);
            for (int i = 0; i < 4; ++i) {
                const float angle = juce::MathConstants<float>::halfPi * static_cast<float>(i);
                g.fillEllipse(juce::Rectangle<float>(1.8f, 1.8f).withCentre(c.getPointOnCircumference(3.6f, angle)));
            }
            break;

        case Glyph::Droplet: {
            juce::Path drop;
            drop.startNewSubPath(c.translated(0.0f, -4.0f));
            drop.quadraticTo(c.translated(3.2f, 0.5f), c.translated(0.0f, 3.2f));
            drop.quadraticTo(c.translated(-3.2f, 0.5f), c.translated(0.0f, -4.0f));
            g.fillPath(drop);
            break;
        }

        case Glyph::Spread: {
            juce::Path arrows;
            arrows.addArrow({c, c.translated(-4.2f, 0.0f)}, 1.0f, 3.0f, 2.0f);
            arrows.addArrow({c, c.translated(4.2f, 0.0f)}, 1.0f, 3.0f, 2.0f);
            g.fillPath(arrows);
            break;
        }

        case Glyph::Feedback: {
            juce::Path arc;
            arc.addCentredArc(c.x, c.y, 3.2f, 3.2f, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.3f, true);
            g.strokePath(arc, juce::PathStrokeType(1.2f));
            juce::Path head;
            const auto tip = c.getPointOnCircumference(3.2f, 0.5f);
            head.addTriangle(tip.translated(-2.0f, -1.6f), tip.translated(1.2f, -2.2f), tip.translated(0.4f, 1.0f));
            g.fillPath(head);
            break;
        }

        case Glyph::Cube: {
            juce::Path cube;
            const auto top = c.translated(0.0f, -3.8f);
            const auto left = c.translated(-3.4f, -1.9f);
            const auto right = c.translated(3.4f, -1.9f);
            cube.startNewSubPath(top);
            cube.lineTo(right);
            cube.lineTo(right.translated(0.0f, 3.8f));
            cube.lineTo(c.translated(0.0f, 3.8f));
            cube.lineTo(left.translated(0.0f, 3.8f));
            cube.lineTo(left);
            cube.closeSubPath();
            cube.startNewSubPath(left);
            cube.lineTo(c);
            cube.lineTo(right);
            cube.startNewSubPath(c);
            cube.lineTo(c.translated(0.0f, 3.8f));
            g.strokePath(cube, juce::PathStrokeType(1.0f));
            break;
        }

        case Glyph::Pencil:
            g.drawLine({c.translated(-3.0f, 3.0f), c.translated(3.0f, -3.0f)}, 2.0f);
            break;
    }
}

void DrawIcon(juce::Graphics& g, Glyph glyph, juce::Point<float> centre, juce::Colour colour) {
    const float size = layout::kIconRadius * 2.0f;
    g.setColour(colour);
    if (glyph == Glyph::Pencil) {
        g.fillRoundedRectangle(juce::Rectangle<float>(size, size).withCentre(centre), 2.0f);
    } else {
        g.fillEllipse(juce::Rectangle<float>(size, size).withCentre(centre));
    }
    DrawGlyph(g, glyph, centre);
}

void DrawDottedLine(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to) {
    const float length = from.getDistanceFrom(to);
    const int dots = static_cast<int>(length / 3.0f);
    g.setColour(colours::kInk);
    for (int i = 0; i <= dots; ++i) {
        const auto point = from + (to - from) * (static_cast<float>(i) / static_cast<float>(juce::jmax(1, dots)));
        g.fillEllipse(juce::Rectangle<float>(1.4f, 1.4f).withCentre(point));
    }
}

const char* BlendParameterName(int parameter) {
    switch (parameter) {
        case 0: return "dry/wet";
        case 1: return "stereo spread";
        case 2: return "feedback";
        case 3: return "reverb";
    }
    return "";
}

// The module's colour scale for a value from 0 to 1 (Leds::set_intensity in
// eurorack/clouds/drivers/leds.h): off, green, yellow, then red.
void ShowIntensity(clouds_panel::PanelLed& led, float value) {
    if (value < 0.5f) {
        led.SetLight(0.0f, value * 2.0f);
    } else if (value < 0.75f) {
        led.SetLight((value - 0.5f) * 4.0f, 1.0f);
    } else {
        led.SetLight(1.0f, 1.0f - (value - 0.75f) * 4.0f);
    }
}

// The playback mode display's pulse (Ui::PaintLeds in the firmware): a
// triangle wave over 512 ms, squared, from 0 to 254. The selected LED moves
// between yellow (fade 0) and orange (fade 254).
void ShowPlaybackModeLed(clouds_panel::PanelLed& led, juce::uint32 milliseconds) {
    auto fade = static_cast<std::uint8_t>(milliseconds >> 1);
    fade = static_cast<std::uint8_t>(fade <= 127 ? fade << 1 : 255 - (fade << 1));
    const int squared = static_cast<int>(fade) * fade >> 8;
    led.SetLight(static_cast<float>(128 + (squared >> 1)) / 255.0f,
                 static_cast<float>(255 - (squared >> 1)) / 255.0f);
}

// The module's input meter (Leds::PaintBar with lut_db in the firmware):
// each LED covers 6 dB and fades in across it. LEDs 1 and 2 light green from
// -24 to -12 dBFS, LED 3 yellow up to -6 dBFS, LED 4 red up to full scale.
void ShowLevel(std::array<clouds_panel::PanelLed, 4>& leds, float peak) {
    // Steps of 6.02 dB (one octave of amplitude) above -24 dBFS, the
    // firmware's 8192 * log2(16 * peak) in units of 8192.
    const float level = peak > 0.0f ? std::log2(peak) + 4.0f : -1.0f;
    for (std::size_t i = 0; i < leds.size(); ++i) {
        const float fill = juce::jlimit(0.0f, 1.0f, level - static_cast<float>(i));
        const float red = i >= 2 ? fill : 0.0f;
        const float green = i <= 2 ? fill : 0.0f;
        leds[i].SetLight(red, green);
    }
}

// What the knobs and TRIG do in each mode, after the module's manual and its
// notes on the alternate modes (CloudsEngine's references/secrets.md).
struct Tooltips {
    const char* position;
    const char* size;
    const char* density;
    const char* texture;
    const char* trigger;
};

const Tooltips kModeTooltips[kPlaybackModeCount] = {
    {
        // Granular
        "Position: where in the recording grains start, from the newest audio "
        "(counterclockwise) back to the oldest.",
        "Size: grain length.",
        "Density: grain rate. None at 12 o'clock; clockwise, more and more grains at "
        "random times; counterclockwise, regular grains, faster and faster.",
        "Texture: grain envelope, from square to smooth; past 3 o'clock it adds diffusion.",
        "Trigger: plays a single grain. With DENSITY at 12 o'clock, only triggers create grains.",
    },
    {
        // Stretch
        "Position: where in the recording playback reads; while frozen, moving it scrubs "
        "through the audio.",
        "Size: length of the overlapping windows, from a grainy drilling sound to smooth "
        "fragments of loops.",
        "Density: granular diffusion (all-pass filters).",
        "Texture: low-pass filter counterclockwise, high-pass clockwise.",
        "Trigger: a stutter, like a decaying envelope on POSITION; while frozen, repeated "
        "presses make a clock-synchronised loop.",
    },
    {
        // Looping delay
        "Position: delay time; while frozen, where the loop starts.",
        "Size: while frozen, the loop length; otherwise the pitch shifter's windows, grainy "
        "counterclockwise, smooth clockwise.",
        "Density: granular diffusion (all-pass filters).",
        "Texture: low-pass filter counterclockwise, high-pass clockwise.",
        "Trigger: tap tempo, the time between presses sets the delay; while frozen, a "
        "synchronised stutter loop.",
    },
    {
        // Spectral
        "Position: which spectral buffer the audio goes into, or, while frozen, is played "
        "from; turning it while frozen morphs between the textures captured.",
        "Size: frequency warping, from spectral shifting to spectral reversal.",
        "Density: below 12 o'clock, frequency bins randomly hold (a partial freeze); above, "
        "successive frames smear together, with random phase at the extreme.",
        "Texture: below 12 o'clock, quantizes the amplitudes like a low-bitrate file; above, "
        "evens out the spectrum towards noise.",
        "Trigger: hold for glitches like a corrupted audio file; the longer the press, the "
        "stronger the effect.",
    },
};

}  // namespace

CloudsPanel::CloudsPanel(AudioHost& host, std::size_t slot) : host_(host), slot_(slot) {
    for (std::size_t id = 0; id < indices_.size(); ++id) {
        indices_[id] = host_.FindParam(slot_, static_cast<ParamId>(id));
        jassert(indices_[id] != AudioHost::kNoParam);  // slot_ holds a clouds::Engine
    }

    setLookAndFeel(&lookAndFeel_);

    AddKnob(positionKnob_, Engine::kPosition, 2);
    AddKnob(sizeKnob_, Engine::kSize, 2);
    AddKnob(pitchKnob_, Engine::kPitch, 2);
    AddKnob(inputGainKnob_, Engine::kInputGain, 1);
    AddKnob(densityKnob_, Engine::kDensity, 2);
    AddKnob(textureKnob_, Engine::kTexture, 2);
    inputGainKnob_.setTooltip("In gain");

    // BLEND sets whichever parameter button B selected; turning it shows all
    // four values on the LEDs.
    AddKnob(blendKnob_, Engine::kDryWet, 2);
    blendKnob_.onValueChange = [this] {
        Send(BlendParam(), static_cast<float>(blendKnob_.getValue()));
        // As on the module, turning BLEND leaves the quality and playback
        // mode displays alone.
        if (ledMode_ != LedMode::Quality && ledMode_ != LedMode::PlaybackMode) ShowLeds(LedMode::BlendMeter);
    };
    SyncBlendKnob();

    freezeButton_.setTooltip("Freeze");
    freezeButton_.onClick = [this] { Send(Engine::kFreeze, freezeButton_.getToggleState() ? 1.0f : 0.0f); };
    addAndMakeVisible(freezeButton_);

    for (auto& led : leds_) addAndMakeVisible(led);

    // Button B, as in the module's Ui::OnSwitchReleased(): the first press
    // shows which parameter BLEND controls and further presses select the
    // next one. Holding it for a second switches to the quality display,
    // where presses select the next quality; holding it again switches back.
    // Held on for four seconds, it passes through the quality display to the
    // playback mode display, where presses select the next mode.
    blendButton_.setTooltip(
        "Blend parameter: press to show which parameter BLEND controls, press again to select "
        "the next. Hold for one second to select the audio quality (red LED); changing it erases "
        "the recording. Hold for four seconds to select the playback mode (orange LED): granular, "
        "pitch shifter/time stretcher, looping delay, spectral. Switching to or from spectral "
        "erases the recording.");
    blendButton_.onShortPress = [this] {
        switch (ledMode_) {
            case LedMode::Meter:
            case LedMode::BlendMeter:
                ShowLeds(LedMode::Blending);
                break;
            case LedMode::Blending:
                blendParameter_ = (blendParameter_ + 1) % kBlendParameterCount;
                SyncBlendKnob();
                ShowLeds(LedMode::Blending);
                break;
            case LedMode::Quality:
                SelectNextQuality();
                ShowLeds(LedMode::Quality);
                break;
            case LedMode::PlaybackMode:
                SelectNextPlaybackMode();
                ShowLeds(LedMode::PlaybackMode);
                break;
        }
    };
    blendButton_.onLongPress = [this] {
        // Holding B for a second while C is held is the module's "secret
        // handshake" (Ui::OnSecretHandshake), a shortcut to the mode display.
        if (saveButton_.isDown()) {
            ShowLeds(LedMode::PlaybackMode);
        } else {
            ShowLeds(ledMode_ == LedMode::Quality ? LedMode::Meter : LedMode::Quality);
        }
    };
    blendButton_.onVeryLongPress = [this] { ShowLeds(LedMode::PlaybackMode); };

    // C has no function yet.
    saveButton_.setTooltip("Load/save (not yet implemented)");
    addAndMakeVisible(blendButton_);
    addAndMakeVisible(saveButton_);

    triggerButton_.setTriggeredOnMouseDown(true);
    triggerButton_.onClick = [this] { Send(Engine::kTrigger, 1.0f); };
    // Held, the button is the TRIG input's gate, which the spectral mode
    // uses.
    triggerButton_.onStateChange = [this] {
        const bool down = triggerButton_.isDown();
        if (down == gate_) return;
        gate_ = down;
        Send(Engine::kGate, down ? 1.0f : 0.0f);
    };
    addAndMakeVisible(triggerButton_);

    SyncControls();
    UpdateLeds();
    startTimerHz(kRefreshHz);
}

CloudsPanel::~CloudsPanel() {
    stopTimer();
    // A button destroyed while held reports no state change, so a TRIG held
    // while the panel goes would leave the gate high.
    if (gate_) Send(Engine::kGate, 0.0f);
    setLookAndFeel(nullptr);
}

int CloudsPanel::HeightFor(int, int visibleHeight) const {
    return juce::jmax(visibleHeight, juce::roundToInt(layout::kHeight * layout::kMinimumScale));
}

float CloudsPanel::Value(ParamId id) const {
    const std::size_t index = indices_[id];
    return index != AudioHost::kNoParam ? host_.PublishedValue(index) : 0.0f;
}

float CloudsPanel::Latest(ParamId id) const {
    const std::size_t index = indices_[id];
    return index != AudioHost::kNoParam ? host_.LatestValue(index) : 0.0f;
}

void CloudsPanel::Send(ParamId id, float value) {
    const std::size_t index = indices_[id];
    if (index != AudioHost::kNoParam) host_.SendFromGui(index, value);
}

ParamId CloudsPanel::BlendParam() const noexcept {
    return static_cast<ParamId>(Engine::kDryWet + blendParameter_);
}

// Range, default and suffix come from the engine's table.
void CloudsPanel::AddKnob(clouds_panel::PanelKnob& knob, ParamId id, int decimalPlaces) {
    const std::size_t index = indices_[id];
    if (index == AudioHost::kNoParam) return;
    const ParamSpec& spec = *host_.Params()[index].spec;
    const juce::String suffix = spec.unit[0] != '\0' ? juce::String(" ") + spec.unit : juce::String();
    knob.Configure(spec.min, spec.max, spec.def, Value(id), suffix, decimalPlaces);
    knob.setTooltip(spec.name);
    // The slider's range is snapped to its interval, which can land a hair
    // outside the parameter's; the rig would clamp it anyway.
    knob.onValueChange = [this, &knob, id] {
        Send(id, static_cast<float>(juce::jlimit(knob.getMinimum(), knob.getMaximum(), knob.getValue())));
    };
    addAndMakeVisible(knob);
}

void CloudsPanel::SyncBlendKnob() {
    // The module's knob cannot move, so after B selects another parameter it
    // catches up gradually with that parameter's value. Here the knob simply
    // moves to the value.
    const ParamId id = BlendParam();
    blendKnob_.setValue(Latest(id), juce::dontSendNotification);
    blendKnob_.setDoubleClickReturnValue(true, host_.Params()[indices_[id]].spec->def);
    blendKnob_.setTooltip(juce::String("Blend: ") + BlendParameterName(blendParameter_));
}

// Shows the published values, leaving alone a control the user is holding or
// whose value still waits to be sent.
void CloudsPanel::SyncControls() {
    const auto syncKnob = [this](clouds_panel::PanelKnob& knob, ParamId id) {
        const std::size_t index = indices_[id];
        if (index == AudioHost::kNoParam || knob.isMouseButtonDown() || host_.IsPending(index)) return;
        const double value = host_.PublishedValue(index);
        // Within the knob's 0.001 interval, the value is already shown.
        if (std::abs(knob.getValue() - value) > 0.0005) knob.setValue(value, juce::dontSendNotification);
    };
    syncKnob(positionKnob_, Engine::kPosition);
    syncKnob(sizeKnob_, Engine::kSize);
    syncKnob(pitchKnob_, Engine::kPitch);
    syncKnob(inputGainKnob_, Engine::kInputGain);
    syncKnob(densityKnob_, Engine::kDensity);
    syncKnob(textureKnob_, Engine::kTexture);
    syncKnob(blendKnob_, BlendParam());

    // A quality change, or entering or leaving spectral, releases freeze in
    // the engine; the button follows.
    if (!host_.IsPending(indices_[Engine::kFreeze])) {
        const bool frozen = Value(Engine::kFreeze) != 0.0f;
        if (freezeButton_.getToggleState() != frozen) {
            freezeButton_.setToggleState(frozen, juce::dontSendNotification);
        }
    }

    const int mode = static_cast<int>(Value(Engine::kPlaybackMode));
    if (mode != shownMode_) UpdateModeTooltips(mode);
}

void CloudsPanel::SelectNextQuality() {
    const int next = (static_cast<int>(Latest(Engine::kQuality)) + 1) % kQualityCount;
    Send(Engine::kQuality, static_cast<float>(next));
}

void CloudsPanel::SelectNextPlaybackMode() {
    const int next = (static_cast<int>(Latest(Engine::kPlaybackMode)) + 1) % kPlaybackModeCount;
    Send(Engine::kPlaybackMode, static_cast<float>(next));
}

void CloudsPanel::UpdateModeTooltips(int mode) {
    shownMode_ = mode;
    const Tooltips& tooltips = kModeTooltips[juce::jlimit(0, kPlaybackModeCount - 1, mode)];
    positionKnob_.setTooltip(tooltips.position);
    sizeKnob_.setTooltip(tooltips.size);
    densityKnob_.setTooltip(tooltips.density);
    textureKnob_.setTooltip(tooltips.texture);
    triggerButton_.setTooltip(tooltips.trigger);
}

void CloudsPanel::ShowLeds(LedMode mode) {
    ledMode_ = mode;
    lastLedActivity_ = juce::Time::getMillisecondCounter();
    UpdateLeds();
}

// Quality and mode show what the engine runs, so a selection appears once
// the audio thread has applied it.
void CloudsPanel::UpdateLeds() {
    switch (ledMode_) {
        case LedMode::Meter: {
            // As the module meters: the input after IN GAIN, or the output
            // while frozen.
            const bool frozen = Value(Engine::kFreeze) != 0.0f;
            const float level = frozen ? host_.OutputLevel()
                                       : host_.InputLevel() * juce::Decibels::decibelsToGain(Value(Engine::kInputGain));
            ShowLevel(leds_, level);
            break;
        }
        case LedMode::Blending:
            for (std::size_t i = 0; i < leds_.size(); ++i) {
                leds_[i].SetLight(0.0f, static_cast<int>(i) == blendParameter_ ? 1.0f : 0.0f);
            }
            break;
        case LedMode::BlendMeter:
            for (std::size_t i = 0; i < leds_.size(); ++i) {
                ShowIntensity(leds_[i], Value(static_cast<ParamId>(Engine::kDryWet + i)));
            }
            break;
        case LedMode::Quality: {
            const int quality = static_cast<int>(Value(Engine::kQuality));
            for (std::size_t i = 0; i < leds_.size(); ++i) {
                leds_[i].SetLight(static_cast<int>(i) == quality ? 1.0f : 0.0f, 0.0f);
            }
            break;
        }
        case LedMode::PlaybackMode: {
            const int mode = static_cast<int>(Value(Engine::kPlaybackMode));
            const juce::uint32 now = juce::Time::getMillisecondCounter();
            for (std::size_t i = 0; i < leds_.size(); ++i) {
                if (static_cast<int>(i) == mode) {
                    ShowPlaybackModeLed(leds_[i], now);
                } else {
                    leds_[i].SetLight(0.0f, 0.0f);
                }
            }
            break;
        }
    }
}

void CloudsPanel::timerCallback() {
    host_.FlushGui();
    SyncControls();

    // B's very long press falls 3 s after its long press showed the quality
    // display, the same as the timeout, so the display waits while B is held
    // rather than flash the meter just before the mode display.
    if (ledMode_ != LedMode::Meter && !blendButton_.isDown() &&
        juce::Time::getMillisecondCounter() - lastLedActivity_ > kLedTimeoutMilliseconds) {
        ledMode_ = LedMode::Meter;
    }
    UpdateLeds();

    const int frames = host_.GetStatus().blockSize;
    const bool warn = static_cast<int>(Value(Engine::kPlaybackMode)) == kSpectral && frames > 0 &&
                      frames < kMinSpectralBufferFrames;
    if (warn != bufferWarning_) {
        bufferWarning_ = warn;
        repaint();
    }
}

juce::AffineTransform CloudsPanel::Transform() const {
    return juce::AffineTransform::scale(scale_).translated(origin_);
}

juce::Rectangle<int> CloudsPanel::Scaled(juce::Rectangle<float> area) const {
    return area.transformedBy(Transform()).getSmallestIntegerContainer();
}

void CloudsPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff2a2a2d));
    g.addTransform(Transform());

    // Panel.
    const auto panel = juce::Rectangle<float>(layout::kWidth, layout::kHeight);
    g.setGradientFill(juce::ColourGradient(colours::kPanel, panel.getTopLeft(), colours::kPanelShade,
                                           panel.getBottomLeft(), false));
    g.fillRect(panel);

    for (const float y : layout::kScrewY) {
        for (const float x : {46.0f, 370.0f}) DrawScrewHole(g, {x, y});
    }

    // Header.
    g.setColour(colours::kInk);
    g.setFont(PanelFont(24.0f, false));
    g.drawText("Clouds", juce::Rectangle<float>(100.0f, 9.0f, 120.0f, 24.0f), juce::Justification::centredLeft,
               false);
    // Logo: a framed square of cloud-like waves.
    const auto logo = juce::Rectangle<float>(80.0f, 10.0f, 19.0f, 19.0f);
    g.drawRect(logo, 1.6f);
    for (int row = 0; row < 3; ++row) {
        juce::Path wave;
        const float y = logo.getY() + 5.0f + 4.5f * static_cast<float>(row);
        wave.startNewSubPath(logo.getX() + 3.0f, y);
        for (int i = 0; i < 3; ++i) {
            const float x = logo.getX() + 3.0f + 4.4f * static_cast<float>(i);
            wave.quadraticTo(x + 2.2f, y + (i % 2 == row % 2 ? -2.5f : 2.5f), x + 4.4f, y);
        }
        g.strokePath(wave, juce::PathStrokeType(1.1f));
    }
    g.setFont(PanelFont(12.0f, false));
    g.drawText("texture synthesizer", juce::Rectangle<float>(228.0f, 16.0f, 130.0f, 16.0f),
               juce::Justification::centredLeft, false);

    g.setColour(colours::kOrnament);
    g.fillRect(juce::Rectangle<float>(14.0f, 34.0f, layout::kWidth - 28.0f, 6.0f));

    // Ornaments behind the large knobs.
    for (const auto& knob : {layout::kPosition, layout::kSize, layout::kPitch}) DrawOrnament(g, knob.Centre());

    // Freeze, LED icons and buttons B and C.
    DrawLabel(g, "FREEZE", {99.0f, layout::kFreeze.y});

    const Glyph qualityGlyphs[]{Glyph::DoubleRing, Glyph::Ring, Glyph::DoubleRing, Glyph::RingWithMarks};
    const Glyph blendGlyphs[]{Glyph::Droplet, Glyph::Spread, Glyph::Feedback, Glyph::Cube};
    for (std::size_t i = 0; i < leds_.size(); ++i) {
        DrawIcon(g, qualityGlyphs[i], {layout::kLedX[i], layout::kQualityIconY}, colours::kRed);
        DrawIcon(g, blendGlyphs[i], {layout::kLedX[i], layout::kBlendIconY}, colours::kTeal);
    }

    const float lastIconX = layout::kLedX[3] + layout::kIconRadius + 2.0f;
    const float bx = layout::kBlendButton.x;
    DrawDottedLine(g, {lastIconX, layout::kQualityIconY}, {bx, layout::kQualityIconY});
    DrawDottedLine(g, {bx, layout::kQualityIconY},
                   {bx, layout::kBlendButton.y - layout::kBlendButton.radius - 2.0f});
    DrawDottedLine(g, {lastIconX, layout::kBlendIconY}, {bx, layout::kBlendIconY});
    DrawDottedLine(g, {bx, layout::kBlendIconY}, {bx, layout::kBlendButton.y + layout::kBlendButton.radius + 2.0f});
    DrawIcon(g, Glyph::Pencil, {layout::kSaveButton.x, layout::kQualityIconY}, colours::kRed);

    // Pitch's centre mark.
    g.setColour(colours::kInk);
    g.drawLine(layout::kPitch.x, layout::kPitch.y - layout::kPitch.radius - 10.0f, layout::kPitch.x,
               layout::kPitch.y - layout::kPitch.radius - 2.0f, 1.4f);

    // Knob labels.
    DrawPill(g, "POSITION", {layout::kPosition.x, layout::kLargeLabelY}, colours::kRed);
    DrawPill(g, "SIZE", {layout::kSize.x, layout::kLargeLabelY}, colours::kTeal);
    DrawLabel(g, "PITCH", {layout::kPitch.x, layout::kLargeLabelY});

    DrawPill(g, "IN GAIN", {layout::kInputGain.x, layout::kSmallLabelY}, colours::kRed);
    DrawPill(g, "DENSITY", {layout::kDensity.x, layout::kSmallLabelY}, colours::kRed);
    DrawPill(g, "TEXTURE", {layout::kTexture.x, layout::kSmallLabelY}, colours::kTeal);
    DrawLabel(g, "BLEND", {layout::kBlend.x - 4.0f, layout::kSmallLabelY});
    g.setColour(colours::kTeal);
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({layout::kBlend.x + 26.0f, layout::kSmallLabelY}));

    // Bottom row: the trigger, which replaces the module's TRIG input, and
    // the name.
    g.setColour(colours::kOrnament);
    g.fillRect(juce::Rectangle<float>(30.0f, layout::kDividerY - 1.0f, 360.0f, 2.0f));

    DrawLabel(g, "TRIG", {layout::kTrigger.x - 33.0f, layout::kTrigger.y});

    if (bufferWarning_) {
        // Kept clear of the TRIG label on the right.
        g.setColour(colours::kRed);
        g.setFont(PanelFont(11.0f, true));
        g.drawFittedText("Spectral mode needs audio buffers of " + juce::String(kMinSpectralBufferFrames) +
                             " samples or more",
                         juce::Rectangle<int>(60, juce::roundToInt(layout::kBottomRowY) - 15, 250, 30),
                         juce::Justification::centred, 2);
    } else {
        g.setColour(colours::kInk);
        g.setFont(PanelFont(13.0f, false));
        g.drawText("WeatherFields",
                   juce::Rectangle<float>(layout::kWidth, 20.0f).withCentre({layout::kWidth * 0.5f, layout::kBottomRowY}),
                   juce::Justification::centred, false);
    }
}

void CloudsPanel::resized() {
    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());
    scale_ = juce::jlimit(layout::kMinimumScale, layout::kMaximumScale,
                          juce::jmin(width / layout::kWidth, height / layout::kHeight));
    origin_ = {juce::jmax(0.0f, (width - layout::kWidth * scale_) * 0.5f),
               juce::jmax(0.0f, (height - layout::kHeight * scale_) * 0.5f)};

    freezeButton_.setBounds(Scaled(layout::kFreeze.Bounds()));
    for (std::size_t i = 0; i < leds_.size(); ++i) {
        leds_[i].setBounds(Scaled(layout::Circle{layout::kLedX[i], layout::kLedY, layout::kLedRadius}.Bounds()));
    }
    blendButton_.setBounds(Scaled(layout::kBlendButton.Bounds()));
    saveButton_.setBounds(Scaled(layout::kSaveButton.Bounds()));

    positionKnob_.setBounds(Scaled(layout::kPosition.Bounds()));
    sizeKnob_.setBounds(Scaled(layout::kSize.Bounds()));
    pitchKnob_.setBounds(Scaled(layout::kPitch.Bounds()));

    inputGainKnob_.setBounds(Scaled(layout::kInputGain.Bounds()));
    densityKnob_.setBounds(Scaled(layout::kDensity.Bounds()));
    textureKnob_.setBounds(Scaled(layout::kTexture.Bounds()));
    blendKnob_.setBounds(Scaled(layout::kBlend.Bounds()));
    triggerButton_.setBounds(Scaled(layout::kTrigger.Bounds()));
}

}  // namespace desktop
}  // namespace wf
