#include "StratusPanel.h"

#include <cmath>

namespace wf {
namespace desktop {

namespace {

namespace colours = clouds_panel::colours;
using clouds_panel::DrawLabel;
using clouds_panel::DrawOrnament;
using clouds_panel::DrawPill;
using clouds_panel::DrawScrewHole;
using clouds_panel::PanelFont;

// Reference coordinates: the Clouds panel's size, so both rigs fill the
// window alike.
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

constexpr float kLedFirstX = 58.0f;
constexpr float kLedSpacing = 20.0f;
constexpr float kLedY = 84.0f;
constexpr float kLedRadius = 8.0f;
constexpr float kLedLabelY = 104.0f;
constexpr float kLastLedX = kLedFirstX + kLedSpacing * 15.0f;

constexpr Circle kLevel{208.0f, 192.0f, 41.0f};
constexpr float kLevelLabelY = 252.0f;

constexpr float kButtonY = 300.0f;
constexpr float kButtonLabelY = 333.0f;
constexpr Circle kRecord{80.0f, kButtonY, 20.0f};
constexpr Circle kPlay{165.0f, kButtonY, 20.0f};
constexpr Circle kClear{260.0f, kButtonY, 11.0f};
constexpr Circle kThru{328.0f, kButtonY, 11.0f};
constexpr Circle kThruLed{356.0f, kButtonY, 8.0f};
constexpr float kThruLabelX = 340.0f;

constexpr float kDividerY = 361.5f;
constexpr float kBottomRowY = 392.0f;
constexpr float kScrewY[]{14.0f, kHeight - 19.0f};

}  // namespace layout

constexpr int kRefreshHz = 30;

juce::Rectangle<float> EndLabelArea() {
    return juce::Rectangle<float>(80.0f, 12.0f).withCentre({layout::kLastLedX, layout::kLedLabelY});
}

const juce::Colour kRecordRed{0xffe0484f};
const juce::Colour kRecordCore{0xfffde2df};
const juce::Colour kPlayCore{0xfff4fbd8};

}  // namespace

StratusPanel::StratusPanel(AudioHost& host, std::size_t slot)
    : host_(host),
      slot_(slot),
      recordButton_("REC", kRecordRed, kRecordCore),
      playButton_("PLAY", colours::kLedGreen, kPlayCore) {
    for (std::size_t id = 0; id < indices_.size(); ++id) {
        indices_[id] = host_.FindParam(slot_, static_cast<ParamId>(id));
        jassert(indices_[id] != AudioHost::kNoParam);  // slot_ holds a stratus::Engine
    }
    maxSeconds_ = host_.Params()[indices_[Engine::kPosition]].spec->max;

    setLookAndFeel(&lookAndFeel_);

    recordButton_.setTooltip("Record: when empty, records the first take (PLAY or REC again ends it); "
                             "with a loop, overdub on and off.");
    recordButton_.onClick = [this] { Send(Engine::kRecord, recordButton_.getToggleState() ? 1.0f : 0.0f); };
    addAndMakeVisible(recordButton_);

    playButton_.setTooltip("Play: on plays the loop from the start, off stops it.");
    playButton_.onClick = [this] { Send(Engine::kPlay, playButton_.getToggleState() ? 1.0f : 0.0f); };
    addAndMakeVisible(playButton_);

    clearButton_.setTooltip("Clear: empties the loop.");
    clearButton_.onClick = [this] { Send(Engine::kClear, 1.0f); };
    addAndMakeVisible(clearButton_);

    thruButton_.setTooltip("Thru: the input to the output, beside the loop.");
    thruButton_.onClick = [this] { Send(Engine::kThru, Latest(Engine::kThru) != 0.0f ? 0.0f : 1.0f); };
    addAndMakeVisible(thruButton_);
    addAndMakeVisible(thruLed_);

    const ParamSpec& level = *host_.Params()[indices_[Engine::kLoopLevel]].spec;
    levelKnob_.Configure(level.min, level.max, level.def, Value(Engine::kLoopLevel), " dB", 1);
    levelKnob_.setTooltip("Loop level: the loop's volume in the output (fully down is silent); "
                          "the recording keeps its level.");
    levelKnob_.onValueChange = [this] {
        Send(Engine::kLoopLevel, static_cast<float>(juce::jlimit(levelKnob_.getMinimum(), levelKnob_.getMaximum(),
                                                                 levelKnob_.getValue())));
    };
    addAndMakeVisible(levelKnob_);

    for (auto& led : leds_) {
        led.setTooltip("While the first take records, the LEDs fill over " + juce::String(maxSeconds_, 0) +
                       " s: the unlit ones are the time left. While the loop plays, a light crosses them once "
                       "per lap, amber when overdubbing.");
        addAndMakeVisible(led);
    }

    SyncControls();
    UpdateLeds();
    startTimerHz(kRefreshHz);
}

StratusPanel::~StratusPanel() {
    stopTimer();
    setLookAndFeel(nullptr);
}

int StratusPanel::HeightFor(int, int visibleHeight) const {
    return juce::jmax(visibleHeight, juce::roundToInt(layout::kHeight * layout::kMinimumScale));
}

float StratusPanel::Value(ParamId id) const {
    const std::size_t index = indices_[id];
    return index != AudioHost::kNoParam ? host_.PublishedValue(index) : 0.0f;
}

float StratusPanel::Latest(ParamId id) const {
    const std::size_t index = indices_[id];
    return index != AudioHost::kNoParam ? host_.LatestValue(index) : 0.0f;
}

void StratusPanel::Send(ParamId id, float value) {
    const std::size_t index = indices_[id];
    if (index != AudioHost::kNoParam) host_.SendFromGui(index, value);
}

// Shows the published values, leaving alone a control the user is holding or
// whose value still waits to be sent. The engine sets REC and PLAY itself (a
// first take's end, Play refused while empty), and the buttons follow.
void StratusPanel::SyncControls() {
    const auto syncButton = [this](juce::Button& button, ParamId id) {
        if (host_.IsPending(indices_[id])) return;
        const bool on = Value(id) != 0.0f;
        if (button.getToggleState() != on) button.setToggleState(on, juce::dontSendNotification);
    };
    syncButton(recordButton_, Engine::kRecord);
    syncButton(playButton_, Engine::kPlay);

    const std::size_t level = indices_[Engine::kLoopLevel];
    if (!levelKnob_.isMouseButtonDown() && !host_.IsPending(level)) {
        const double value = host_.PublishedValue(level);
        // Within the knob's 0.001 interval, the value is already shown.
        if (std::abs(levelKnob_.getValue() - value) > 0.0005) levelKnob_.setValue(value, juce::dontSendNotification);
    }

    thruLed_.SetLight(0.0f, Value(Engine::kThru) != 0.0f ? 1.0f : 0.0f);
}

// No loop: the first take fills the row red as it records, else the row is
// dark. A loop: while it plays, the light sits between the two LEDs nearest
// the playhead, crossfading from one to the next and from the last back to
// the first; stopped, the row is dark.
void StratusPanel::UpdateLeds() {
    const float position = Value(Engine::kPosition);
    const float length = Value(Engine::kLength);
    const bool record = Value(Engine::kRecord) != 0.0f;
    const bool play = Value(Engine::kPlay) != 0.0f;
    const auto count = static_cast<float>(kLedCount);

    for (std::size_t i = 0; i < kLedCount; ++i) {
        const auto led = static_cast<float>(i);
        if (length <= 0.0f) {
            const float fill = record && maxSeconds_ > 0.0f ? position / maxSeconds_ * count : 0.0f;
            leds_[i].SetLight(juce::jlimit(0.0f, 1.0f, fill - led), 0.0f);
        } else if (play) {
            const float head = position / length * count;
            float distance = std::abs(led + 0.5f - head);
            distance = juce::jmin(distance, count - distance);
            const float light = juce::jlimit(0.0f, 1.0f, 1.0f - distance);
            leds_[i].SetLight(record ? light : 0.0f, record ? light * 0.6f : light);
        } else {
            leds_[i].SetLight(0.0f, 0.0f);
        }
    }

    // The row's end is the take's limit, or the loop's length once there is
    // one.
    if (length != shownLength_) {
        shownLength_ = length;
        repaint(Scaled(EndLabelArea()));
    }
}

void StratusPanel::timerCallback() {
    host_.FlushGui();
    SyncControls();
    UpdateLeds();
}

juce::AffineTransform StratusPanel::Transform() const {
    return juce::AffineTransform::scale(scale_).translated(origin_);
}

juce::Rectangle<int> StratusPanel::Scaled(juce::Rectangle<float> area) const {
    return area.transformedBy(Transform()).getSmallestIntegerContainer();
}

void StratusPanel::paint(juce::Graphics& g) {
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

    // Header, as Clouds': the name beside a framed square, here of flat
    // layers.
    g.setColour(colours::kInk);
    g.setFont(PanelFont(24.0f, false));
    g.drawText("Stratus", juce::Rectangle<float>(100.0f, 9.0f, 120.0f, 24.0f), juce::Justification::centredLeft,
               false);
    const auto logo = juce::Rectangle<float>(80.0f, 10.0f, 19.0f, 19.0f);
    g.drawRect(logo, 1.6f);
    for (int row = 0; row < 3; ++row) {
        const auto r = static_cast<float>(row);
        const float y = logo.getY() + 5.0f + 4.5f * r;
        g.drawLine(logo.getX() + 3.0f + r * 1.5f, y, logo.getRight() - 3.0f - (2.0f - r) * 1.5f, y, 1.2f);
    }
    g.setFont(PanelFont(12.0f, false));
    g.drawText("looper", juce::Rectangle<float>(228.0f, 16.0f, 130.0f, 16.0f), juce::Justification::centredLeft,
               false);

    g.setColour(colours::kOrnament);
    g.fillRect(juce::Rectangle<float>(14.0f, 34.0f, layout::kWidth - 28.0f, 6.0f));

    // The LED row's ends.
    g.setColour(colours::kInk);
    g.setFont(PanelFont(9.0f, false));
    g.drawText("0", juce::Rectangle<float>(20.0f, 12.0f).withCentre({layout::kLedFirstX, layout::kLedLabelY}),
               juce::Justification::centred, false);
    const float end = shownLength_ > 0.0f ? shownLength_ : maxSeconds_;
    g.drawText(juce::String(end, shownLength_ > 0.0f ? 1 : 0) + " s",
               EndLabelArea(),
               juce::Justification::centred, false);

    DrawOrnament(g, layout::kLevel.Centre());
    DrawPill(g, "LOOP LEVEL", {layout::kLevel.x, layout::kLevelLabelY}, colours::kTeal);

    DrawPill(g, "REC", {layout::kRecord.x, layout::kButtonLabelY}, colours::kRed);
    DrawPill(g, "PLAY", {layout::kPlay.x, layout::kButtonLabelY}, colours::kTeal);
    DrawLabel(g, "CLEAR", {layout::kClear.x, layout::kButtonLabelY});
    DrawLabel(g, "THRU", {layout::kThruLabelX, layout::kButtonLabelY});

    g.setColour(colours::kOrnament);
    g.fillRect(juce::Rectangle<float>(30.0f, layout::kDividerY - 1.0f, 360.0f, 2.0f));
    g.setColour(colours::kInk);
    g.setFont(PanelFont(13.0f, false));
    g.drawText("WeatherFields",
               juce::Rectangle<float>(layout::kWidth, 20.0f).withCentre({layout::kWidth * 0.5f, layout::kBottomRowY}),
               juce::Justification::centred, false);
}

void StratusPanel::resized() {
    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());
    scale_ = juce::jlimit(layout::kMinimumScale, layout::kMaximumScale,
                          juce::jmin(width / layout::kWidth, height / layout::kHeight));
    origin_ = {juce::jmax(0.0f, (width - layout::kWidth * scale_) * 0.5f),
               juce::jmax(0.0f, (height - layout::kHeight * scale_) * 0.5f)};

    static_assert(kLedCount == 16, "layout::kLastLedX assumes sixteen LEDs");
    for (std::size_t i = 0; i < kLedCount; ++i) {
        const float x = layout::kLedFirstX + layout::kLedSpacing * static_cast<float>(i);
        leds_[i].setBounds(Scaled(layout::Circle{x, layout::kLedY, layout::kLedRadius}.Bounds()));
    }
    levelKnob_.setBounds(Scaled(layout::kLevel.Bounds()));
    recordButton_.setBounds(Scaled(layout::kRecord.Bounds()));
    playButton_.setBounds(Scaled(layout::kPlay.Bounds()));
    clearButton_.setBounds(Scaled(layout::kClear.Bounds()));
    thruButton_.setBounds(Scaled(layout::kThru.Bounds()));
    thruLed_.setBounds(Scaled(layout::kThruLed.Bounds()));
}

}  // namespace desktop
}  // namespace wf
