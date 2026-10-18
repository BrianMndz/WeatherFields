#include "wf/clouds/OledScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "wf/clouds/Engine.h"
#include "wf/oled/Display.h"
#include "wf/oled/Fonts.h"

namespace wf {
namespace clouds {

namespace {

constexpr float kPi = 3.14159265359f;

// Layout, in panel pixels.
constexpr int kHeaderBaseline = 7;
constexpr int kQualityX = 62;
constexpr int kFreezeX = 108;
constexpr int kRuleY = 10;
constexpr int kCloudX = 0;
constexpr int kCloudY = 12;
constexpr int kCloudWidth = 117;
constexpr int kCloudHeight = 28;
constexpr int kFloorY = 42;
constexpr int kMarkerHalfWidth = 3;
constexpr int kMarkerBottom = 45;
constexpr int kMeterY = 12;
constexpr int kMeterHeight = 33;
constexpr int kMeterWidth = 3;
constexpr int kInputMeterX = 119;
constexpr int kOutputMeterX = 124;
constexpr int kBlendY = 47;
constexpr int kBlendRowPitch = 9;
constexpr int kBlendColumnPitch = 65;
constexpr int kBlendBarX = 17;
constexpr int kBlendBarWidth = 46;
constexpr int kBlendBarHeight = 8;

// The cloud.
constexpr int kMaxGrains = 28;
constexpr float kSilentDb = -48.0f;  // output level that shows no grains
constexpr float kFullDb = -24.0f;    // and every grain
constexpr float kMeterFloorDb = -48.0f;

// Popups: a changed knob in the header, a changed choice on its own.
constexpr std::int32_t kPopupMs = 1500;
constexpr int kPopupBarY = kRuleY - 1;  // two rows, ending on the rule's
constexpr int kChoiceY = 11;
constexpr int kChoicePitch = 11;

const char* const kModeNames[] = {"GRANULAR", "STRETCH", "LOOP DELAY", "SPECTRAL"};
const char* const kQualityNames[] = {"16b ST", "16b MO", "8b ST", "8b MO"};
const char* const kModeChoices[] = {"Granular", "Stretch", "Looping Delay", "Spectral"};
const char* const kQualityChoices[] = {"32k 16b ST  1s", "32k 16b MO  2s", "16k 8b  ST  4s", "16k 8b  MO  8s"};
constexpr int kSpectral = 3;

// How a parameter's popup shows it, with the engine's ranges.
enum class Shown { Nothing, Percent, Semitones, Decibels, Choice };

struct Popup {
    const char* name;
    Shown shown;
    float origin;  // where the bar starts, as a share of the range
};

// By parameter ID. Only what the main screen does not show as a number or
// bar: the four BLEND values have their bars, freeze its tag, and TRIG and
// its gate do not show.
const Popup kPopups[] = {
    {"POSITION", Shown::Percent, 0.0f},
    {"SIZE", Shown::Percent, 0.0f},
    {"PITCH", Shown::Semitones, 0.5f},
    {"DENSITY", Shown::Percent, 0.5f},
    {"TEXTURE", Shown::Percent, 0.0f},
    {nullptr, Shown::Nothing, 0.0f},
    {nullptr, Shown::Nothing, 0.0f},
    {nullptr, Shown::Nothing, 0.0f},
    {nullptr, Shown::Nothing, 0.0f},
    {"IN GAIN", Shown::Decibels, 0.75f},
    {nullptr, Shown::Nothing, 0.0f},
    {nullptr, Shown::Nothing, 0.0f},
    {nullptr, Shown::Nothing, 0.0f},
    {"QUALITY", Shown::Choice, 0.0f},
    {"MODE", Shown::Choice, 0.0f},
};
static_assert(sizeof(kPopups) / sizeof(kPopups[0]) == OledScreen::kParamCount, "a popup per parameter");
static_assert(Engine::kPlaybackMode + 1 == OledScreen::kParamCount, "the screen watches every parameter");

u8g2_uint_t Px(int coordinate) {
    return static_cast<u8g2_uint_t>(coordinate);
}

float Unit(float value) {
    return value >= 0.0f ? std::min(value, 1.0f) : 0.0f;  // NaN gives 0
}

int Index(float value, int count) {
    const float clamped = value >= 0.0f ? std::min(value, static_cast<float>(count - 1)) : 0.0f;
    return static_cast<int>(std::lround(clamped));
}

// A repeatable pseudo-random value in [0, 1] for `key`.
float Random(std::uint32_t key) {
    key ^= key >> 16;
    key *= 0x7feb352du;
    key ^= key >> 15;
    key *= 0x846ca68bu;
    key ^= key >> 16;
    return static_cast<float>(key & 0xffffu) / 65535.0f;
}

void DrawReversed(u8g2_t* u8g2, int x, int y, int width, int height, const char* text) {
    u8g2_DrawBox(u8g2, Px(x), Px(y), Px(width), Px(height));
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawStr(u8g2, Px(x + 2), Px(y + height - 2), text);
    u8g2_SetDrawColor(u8g2, 1);
}

struct Grains {
    float density;
    float size;
    float position;
    float pitch;  // semitones
    float texture;
    float activity;  // the share of grains shown, from the output level
};

// The module's grain overlap (granular_processor.cc): none from 0.47 to
// 0.53, rising to about 1 at either end.
float Overlap(float density) {
    if (density >= 0.53f) return (density - 0.53f) * 2.12f;
    if (density <= 0.47f) return (0.47f - density) * 2.12f;
    return 0.0f;
}

// Each grain is a horizontal stroke that grows and shrinks over its life,
// then starts again elsewhere (random grains) or in the same place (regular
// ones). Strokes are clipped to the cloud's area.
void DrawCloud(u8g2_t* u8g2, const Grains& grains, std::uint32_t ms) {
    const int count = static_cast<int>(std::lround(kMaxGrains * std::min(Overlap(grains.density), 1.0f)));
    const bool regular = grains.density < 0.5f;
    const float seconds = static_cast<float>(ms) / 1000.0f;
    const float lifetime = 0.25f + 1.25f * grains.size;
    const float maxLength = 3.0f + 18.0f * grains.size;
    const float centreX = kCloudX + 4 + grains.position * (kCloudWidth - 8);
    const float halfHeight = kCloudHeight / 2.0f;
    const float centreY = kCloudY + halfHeight - grains.pitch / 24.0f * (halfHeight - 2.0f);
    const float spreadX = 6.0f + 34.0f * grains.texture;
    const float spreadY = 1.0f + (halfHeight - 2.0f) * grains.texture;
    // Above 0.75 the module diffuses its output: softer strokes.
    const bool soft = grains.texture > 0.75f;

    for (int i = 0; i < count; ++i) {
        const auto key = static_cast<std::uint32_t>(i) * 8u;
        // A fixed draw per grain, so the same grains stay as the level moves.
        if (grains.activity < 1.0f && Random(key + 5) >= grains.activity) continue;
        float rate = 1.0f / lifetime;
        float phase = static_cast<float>(i) / static_cast<float>(count);
        if (!regular) {
            rate *= 0.6f + 0.8f * Random(key + 1);
            phase = Random(key + 2);
        }
        const float age = seconds * rate + phase;
        const auto generation = static_cast<std::uint32_t>(age);
        const float life = age - static_cast<float>(generation);

        // Regular grains keep their places, evenly along the cloud on three
        // rows, and swell one after another.
        const float slot = (static_cast<float>(i) + 0.5f) / static_cast<float>(count) * 2.0f - 1.0f;
        float x = centreX + slot * spreadX;
        float y = centreY + static_cast<float>(i % 3 - 1) * std::max(2.0f, spreadY / 2.0f);
        if (!regular) {
            const std::uint32_t place = generation * 977u + key;
            x += (Random(place + 3) * 2.0f - 1.0f) * spreadX;
            y = centreY + (Random(place + 4) * 2.0f - 1.0f) * spreadY;
        }

        const int row = static_cast<int>(std::lround(y));
        if (row < kCloudY || row >= kCloudY + kCloudHeight) continue;
        const int length = std::max(1, static_cast<int>(std::lround(maxLength * std::sin(kPi * life))));
        const int first = static_cast<int>(std::lround(x - length / 2.0f));
        for (int column = std::max(first, kCloudX); column < std::min(first + length, kCloudX + kCloudWidth);
             ++column) {
            if (soft && (column + row) % 2 != 0) continue;
            u8g2_DrawPixel(u8g2, Px(column), Px(row));
        }
    }
}

// 0 at kSilentDb and below, 1 at kFullDb and above, linear in dB between.
float Activity(float level) {
    if (!(level > 0.0f)) return 0.0f;
    return Unit((20.0f * std::log10(level) - kSilentDb) / (kFullDb - kSilentDb));
}

// Segments of two rows every three, from the bottom, for -48 to 0 dB; unlit
// segments keep a dot.
void DrawMeter(u8g2_t* u8g2, int x, float level) {
    const float db = level > 0.0f ? 20.0f * std::log10(level) : kMeterFloorDb;
    const float lit = Unit((db - kMeterFloorDb) / -kMeterFloorDb) * kMeterHeight;
    for (int offset = 0; offset + 2 <= kMeterHeight; offset += 3) {
        const int bottom = kMeterY + kMeterHeight - 1 - offset;
        if (static_cast<float>(offset) < lit) {
            u8g2_DrawBox(u8g2, Px(x), Px(bottom - 1), Px(kMeterWidth), 2);
        } else {
            u8g2_DrawPixel(u8g2, Px(x + kMeterWidth / 2), Px(bottom));
        }
    }
}

void DrawBar(u8g2_t* u8g2, int x, int y, float value) {
    u8g2_DrawFrame(u8g2, Px(x), Px(y), Px(kBlendBarWidth), Px(kBlendBarHeight));
    const int fill = static_cast<int>(std::lround(static_cast<float>(kBlendBarWidth - 4) * Unit(value)));
    if (fill > 0) u8g2_DrawBox(u8g2, Px(x + 2), Px(y + 2), Px(fill), Px(kBlendBarHeight - 4));
}

// The share of the range `value` is at, for the popup's bar.
float Fraction(Shown shown, float value) {
    switch (shown) {
        case Shown::Semitones: return Unit((value + 24.0f) / 48.0f);
        case Shown::Decibels: return Unit((value + 18.0f) / 24.0f);
        default: return Unit(value);
    }
}

// The header gives way to the knob: its name on the left, its value (and,
// for DENSITY, what the module does with it) on the right, and the rule
// becomes a bar from the origin (marked when not at the left) to the value.
void DrawHeaderPopup(u8g2_t* u8g2, int id, float value) {
    const Popup& popup = kPopups[id];
    u8g2_DrawStr(u8g2, 1, kHeaderBaseline, popup.name);

    char text[32];
    switch (popup.shown) {
        case Shown::Semitones:
            std::snprintf(text, sizeof text, "%+.1f st", static_cast<double>(value) + 0.0);
            break;
        case Shown::Decibels:
            std::snprintf(text, sizeof text, "%+.1f dB", static_cast<double>(value) + 0.0);
            break;
        default: {
            const int percent = static_cast<int>(std::lround(Unit(value) * 100.0f));
            if (id == Engine::kDensity) {
                const char* kind = Overlap(value) == 0.0f ? "no grains" : value < 0.5f ? "regular" : "random";
                std::snprintf(text, sizeof text, "%s %d%%", kind, percent);
            } else {
                std::snprintf(text, sizeof text, "%d%%", percent);
            }
            break;
        }
    }
    u8g2_DrawStr(u8g2, Px(oled::kWidth - 1 - u8g2_GetStrWidth(u8g2, text)), kHeaderBaseline, text);

    for (int x = 0; x < oled::kWidth; x += 2) u8g2_DrawPixel(u8g2, Px(x), kRuleY);
    const int origin = static_cast<int>(std::lround(oled::kWidth * popup.origin));
    const int end = static_cast<int>(std::lround(oled::kWidth * Fraction(popup.shown, value)));
    if (end != origin) u8g2_DrawBox(u8g2, Px(std::min(origin, end)), kPopupBarY, Px(std::abs(end - origin)), 2);
    if (popup.origin > 0.0f) u8g2_DrawVLine(u8g2, Px(origin), kPopupBarY - 1, 4);
}

// The whole screen: the choice's options, the current one reversed, and a
// note when the change erased the recording.
void DrawChoicePopup(u8g2_t* u8g2, const char* title, const char* const* options, int current, bool erased) {
    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, oled::kFontSmall);
    DrawReversed(u8g2, 0, 0, oled::kWidth, kRuleY - 1, title);
    u8g2_SetFont(u8g2, oled::kFontText);
    for (int i = 0; i < 4; ++i) {
        const int y = kChoiceY + i * kChoicePitch;
        if (i == current) {
            u8g2_DrawRBox(u8g2, 2, Px(y), oled::kWidth - 4, kChoicePitch, 2);
            u8g2_SetDrawColor(u8g2, 0);
        }
        u8g2_DrawStr(u8g2, 6, Px(y + 9), options[i]);
        u8g2_SetDrawColor(u8g2, 1);
    }
    if (erased) {
        const char* note = "recording erased, freeze off";
        u8g2_SetFont(u8g2, oled::kFontTiny);
        u8g2_DrawStr(u8g2, Px((oled::kWidth - u8g2_GetStrWidth(u8g2, note)) / 2), oled::kHeight - 1, note);
    }
}

}  // namespace

void OledScreen::Draw(u8g2_t* u8g2, std::uint32_t ms, const oled::RigReadout& rig) noexcept {
    float values[kParamCount];
    for (int id = 0; id < kParamCount; ++id) values[id] = rig.Value(slot_, static_cast<ParamId>(id));
    auto value = [&values](Engine::Param id) { return values[id]; };
    const bool frozen = value(Engine::kFreeze) > 0.5f;

    // A changed value opens its popup, replacing any other; choices first,
    // as a quality or mode change can change other values with it. The
    // first frame only learns the values.
    if (started_) {
        for (int id = kParamCount - 1; id >= 0; --id) {
            if (kPopups[id].shown == Shown::Nothing || values[id] == shown_[id]) continue;
            changed_ = id;
            changedMs_ = ms;
            const bool spectral = Index(values[id], 4) == kSpectral || Index(shown_[id], 4) == kSpectral;
            erased_ = id == Engine::kQuality || (id == Engine::kPlaybackMode && spectral);
            break;
        }
    }
    std::copy(values, values + kParamCount, shown_);

    // The last change shows for kPopupMs; otherwise a choice the view
    // presents shows for as long as it does. The erased note stays with its
    // choice's popup until no popup shows.
    const bool recent = changed_ >= 0 && static_cast<std::int32_t>(ms - changedMs_) < kPopupMs;
    const oled::RigReadout::Focus focus = rig.ViewFocus();
    const bool focused = focus.active && focus.slot == slot_ && focus.id < kParamCount &&
                         kPopups[focus.id].shown == Shown::Choice;
    popup_ = recent ? changed_ : focused ? static_cast<int>(focus.id) : -1;
    if (popup_ < 0) changed_ = -1;
    const bool erased = popup_ >= 0 && popup_ == changed_ && erased_;

    // The cloud's time runs only while not frozen.
    if (started_ && !frozen) cloudMs_ += ms - lastMs_;
    started_ = true;
    lastMs_ = ms;

    u8g2_SetDrawColor(u8g2, 1);
    u8g2_SetFontMode(u8g2, 1);
    if (popup_ >= 0 && kPopups[popup_].shown == Shown::Choice) {
        const bool quality = popup_ == Engine::kQuality;
        DrawChoicePopup(u8g2, kPopups[popup_].name, quality ? kQualityChoices : kModeChoices,
                        Index(values[popup_], 4), erased);
        return;
    }

    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, oled::kFontSmall);

    if (popup_ >= 0) {
        DrawHeaderPopup(u8g2, popup_, values[popup_]);
    } else {
        u8g2_DrawStr(u8g2, 1, kHeaderBaseline, kModeNames[Index(value(Engine::kPlaybackMode), 4)]);
        u8g2_DrawStr(u8g2, kQualityX, kHeaderBaseline, kQualityNames[Index(value(Engine::kQuality), 4)]);
        if (frozen) DrawReversed(u8g2, kFreezeX, 0, oled::kWidth - kFreezeX, kRuleY - 1, "FRZ");
        u8g2_DrawHLine(u8g2, 0, kRuleY, oled::kWidth);
    }

    const Grains grains{Unit(value(Engine::kDensity)), Unit(value(Engine::kSize)), Unit(value(Engine::kPosition)),
                        std::max(-24.0f, std::min(value(Engine::kPitch), 24.0f)), Unit(value(Engine::kTexture)),
                        Activity(rig.OutputLevel())};
    DrawCloud(u8g2, grains, cloudMs_);
    if (frozen) u8g2_DrawFrame(u8g2, kCloudX, kCloudY, kCloudWidth, Px(kFloorY - kCloudY));
    for (int x = kCloudX; x < kCloudX + kCloudWidth; x += 2) u8g2_DrawPixel(u8g2, Px(x), kFloorY);
    const int marker = kCloudX + 4 + static_cast<int>(std::lround(grains.position * (kCloudWidth - 8)));
    u8g2_DrawTriangle(u8g2, static_cast<int16_t>(marker - kMarkerHalfWidth), kMarkerBottom,
                      static_cast<int16_t>(marker + kMarkerHalfWidth), kMarkerBottom,
                      static_cast<int16_t>(marker), kFloorY);

    // What Clouds hears: the rig's input after IN GAIN, as the desktop panel
    // shows it (the follower scales with its input, so this is exact).
    const float inputGain = std::pow(10.0f, value(Engine::kInputGain) / 20.0f);
    DrawMeter(u8g2, kInputMeterX, rig.InputLevel() * inputGain);
    DrawMeter(u8g2, kOutputMeterX, rig.OutputLevel());

    const char* const names[] = {"MIX", "SPR", "FBK", "REV"};
    const Engine::Param blend[] = {Engine::kDryWet, Engine::kStereoSpread, Engine::kFeedback, Engine::kReverb};
    for (int i = 0; i < 4; ++i) {
        const int x = (i % 2) * kBlendColumnPitch;
        const int y = kBlendY + (i / 2) * kBlendRowPitch;
        u8g2_DrawStr(u8g2, Px(x + 1), Px(y + 7), names[i]);
        DrawBar(u8g2, x + kBlendBarX, y, value(blend[i]));
    }
}

}  // namespace clouds
}  // namespace wf
