#include "wf/stratus/OledScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "wf/oled/Display.h"
#include "wf/oled/Fonts.h"
#include "wf/oled/Widgets.h"
#include "wf/stratus/Engine.h"

namespace wf {
namespace stratus {

namespace {

// Layout, in panel pixels.
constexpr int kHeaderBaseline = 7;
constexpr int kHeaderHeight = 9;
constexpr int kRuleY = 10;
constexpr int kCellCount = 16;
constexpr int kCellX = 1;
constexpr int kCellY = 24;
constexpr int kCellWidth = 6;
constexpr int kCellPitch = 7;
constexpr int kCellHeight = 12;
constexpr int kHeadOverhang = 2;  // the playing cell, above and below
constexpr int kCellsRight = kCellX + (kCellCount - 1) * kCellPitch + kCellWidth;
constexpr int kEndsBaseline = 46;
constexpr int kMeterY = 12;
constexpr int kMeterHeight = 33;
constexpr int kInputMeterX = 119;
constexpr int kOutputMeterX = 124;
constexpr int kLevelY = 54;
constexpr int kLevelBarX = 17;
constexpr int kLevelBarWidth = 76;
constexpr int kLevelBarHeight = 8;
constexpr int kThruX = 99;

enum class State { Empty, Recording, Playing, Overdub, Stopped };

const char* const kStateNames[] = {"EMPTY", "RECORDING", "PLAYING", "OVERDUB", "STOPPED"};

u8g2_uint_t Px(int coordinate) {
    return static_cast<u8g2_uint_t>(coordinate);
}

float Unit(float value) {
    return value >= 0.0f ? std::min(value, 1.0f) : 0.0f;  // NaN gives 0
}

// The engine's table, for the ranges the screen scales by.
const ParamSpec& Spec(ParamId id) {
    static const Engine engine;
    return *engine.Params().Find(id);
}

void DrawRight(u8g2_t* u8g2, int right, int baseline, const char* text) {
    u8g2_DrawStr(u8g2, Px(right - u8g2_GetStrWidth(u8g2, text)), Px(baseline), text);
}

// The first take fills the cells over the maximum length, each from the
// bottom; empty cells are the time left.
void DrawTakeCells(u8g2_t* u8g2, float filled) {
    for (int i = 0; i < kCellCount; ++i) {
        const int x = kCellX + i * kCellPitch;
        u8g2_DrawFrame(u8g2, Px(x), kCellY, kCellWidth, kCellHeight);
        const float share = Unit(filled - static_cast<float>(i));
        const int rows = static_cast<int>(std::lround(static_cast<float>(kCellHeight - 2) * share));
        if (rows > 0) u8g2_DrawBox(u8g2, Px(x + 1), Px(kCellY + kCellHeight - 1 - rows), kCellWidth - 2, Px(rows));
    }
}

// A loop: the cell under the playhead stands tall while it plays; while
// overdubbing, the cells passed this lap are stippled.
void DrawLoopCells(u8g2_t* u8g2, int head, bool overdub) {
    for (int i = 0; i < kCellCount; ++i) {
        const int x = kCellX + i * kCellPitch;
        if (i == head) {
            u8g2_DrawBox(u8g2, Px(x), kCellY - kHeadOverhang, kCellWidth, kCellHeight + 2 * kHeadOverhang);
            continue;
        }
        u8g2_DrawFrame(u8g2, Px(x), kCellY, kCellWidth, kCellHeight);
        if (overdub && i < head) {
            for (int y = kCellY + 2; y < kCellY + kCellHeight - 1; y += 2) {
                u8g2_DrawPixel(u8g2, Px(x + 2 + (y / 2) % 2), Px(y));
            }
        }
    }
}

}  // namespace

void OledScreen::Draw(u8g2_t* u8g2, std::uint32_t, const oled::RigReadout& rig) noexcept {
    const float position = rig.Value(slot_, Engine::kPosition);
    const float length = rig.Value(slot_, Engine::kLength);
    const bool play = rig.Value(slot_, Engine::kPlay) > 0.5f;
    const bool record = rig.Value(slot_, Engine::kRecord) > 0.5f;
    const float levelDb = rig.Value(slot_, Engine::kLoopLevel);
    const bool thru = rig.Value(slot_, Engine::kThru) > 0.5f;

    const State state = !(length > 0.0f) ? (record ? State::Recording : State::Empty)
                        : record         ? State::Overdub
                        : play           ? State::Playing
                                         : State::Stopped;
    const float maxSeconds = Spec(Engine::kPosition).max;

    u8g2_ClearBuffer(u8g2);
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_SetFontMode(u8g2, 1);
    u8g2_SetFont(u8g2, oled::kFontSmall);

    // Header: the state, reversed while writing, and the time.
    const char* name = kStateNames[static_cast<int>(state)];
    if (state == State::Recording || state == State::Overdub) {
        oled::DrawReversed(u8g2, 0, 0, static_cast<int>(u8g2_GetStrWidth(u8g2, name)) + 4, kHeaderHeight, name);
    } else {
        u8g2_DrawStr(u8g2, 1, kHeaderBaseline, name);
    }
    char time[24];
    time[0] = '\0';
    if (state == State::Recording) {
        std::snprintf(time, sizeof time, "%.1fs left", static_cast<double>(std::max(0.0f, maxSeconds - position)));
    } else if (state != State::Empty) {
        std::snprintf(time, sizeof time, "%.1f/%.1fs", static_cast<double>(position), static_cast<double>(length));
    }
    DrawRight(u8g2, oled::kWidth - 1, kHeaderBaseline, time);
    u8g2_DrawHLine(u8g2, 0, kRuleY, oled::kWidth);

    // The cells and their ends.
    switch (state) {
        case State::Empty:
            for (int i = 0; i < kCellCount; ++i) {
                u8g2_DrawPixel(u8g2, Px(kCellX + i * kCellPitch + kCellWidth / 2), kCellY + kCellHeight / 2);
            }
            break;
        case State::Recording:
            DrawTakeCells(u8g2, position / maxSeconds * kCellCount);
            break;
        case State::Playing:
        case State::Overdub:
        case State::Stopped: {
            const int head = std::min(static_cast<int>(Unit(position / length) * kCellCount), kCellCount - 1);
            DrawLoopCells(u8g2, state == State::Stopped ? -1 : head, state == State::Overdub);
            break;
        }
    }
    u8g2_SetFont(u8g2, oled::kFontTiny);
    u8g2_DrawStr(u8g2, kCellX, kEndsBaseline, "0");
    char end[16];
    if (length > 0.0f) {
        std::snprintf(end, sizeof end, "%.1fs", static_cast<double>(length));
    } else {
        std::snprintf(end, sizeof end, "%.0fs", static_cast<double>(maxSeconds));
    }
    DrawRight(u8g2, kCellsRight, kEndsBaseline, end);

    // What Stratus hears and plays.
    oled::DrawMeter(u8g2, kInputMeterX, kMeterY, kMeterHeight, rig.InputLevel());
    oled::DrawMeter(u8g2, kOutputMeterX, kMeterY, kMeterHeight, rig.OutputLevel());

    // Bottom: Loop Level from its range (the bottom is silence) and Thru.
    u8g2_SetFont(u8g2, oled::kFontSmall);
    u8g2_DrawStr(u8g2, 1, kLevelY + 7, "LVL");
    const ParamSpec& level = Spec(Engine::kLoopLevel);
    u8g2_DrawFrame(u8g2, kLevelBarX, kLevelY, kLevelBarWidth, kLevelBarHeight);
    const float share = Unit((levelDb - level.min) / (level.max - level.min));
    const int fill = static_cast<int>(std::lround(static_cast<float>(kLevelBarWidth - 4) * share));
    if (fill > 0) u8g2_DrawBox(u8g2, kLevelBarX + 2, kLevelY + 2, Px(fill), kLevelBarHeight - 4);
    if (thru) {
        oled::DrawReversed(u8g2, kThruX, kLevelY - 1, oled::kWidth - kThruX, kHeaderHeight, "THRU");
    } else {
        u8g2_DrawStr(u8g2, kThruX + 2, kLevelY + 7, "thru");
    }
}

}  // namespace stratus
}  // namespace wf
