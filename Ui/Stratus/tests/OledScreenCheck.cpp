#include <cstdint>
#include <cstring>
#include <iostream>

#include "wf/oled/Display.h"
#include "wf/stratus/Engine.h"
#include "wf/stratus/OledScreen.h"

namespace {

using wf::stratus::Engine;

int failures = 0;

void Expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// Stratus in slot 0 at its defaults (empty, Thru on, Loop Level 0 dB), with
// levels set by hand.
class FakeReadout final : public wf::oled::RigReadout {
public:
    FakeReadout() {
        const wf::ParamTable& table = engine_.Params();
        for (std::size_t i = 0; i < table.count; ++i) values[table.specs[i].id] = table.specs[i].def;
    }

    float Value(std::size_t slot, wf::ParamId id) const noexcept override {
        return slot == 0 && id <= Engine::kLength ? values[id] : 0.0f;
    }
    float InputLevel() const noexcept override { return input; }
    float OutputLevel() const noexcept override { return output; }

    // The published values of each state.
    void Recording(float seconds) { Set(1, 1, seconds, 0); }
    void Playing(float position, float length) { Set(1, 0, position, length); }
    void Overdub(float position, float length) { Set(1, 1, position, length); }
    void Stopped(float position, float length) { Set(0, 0, position, length); }

    float values[Engine::kLength + 1] = {};
    float input = 0.0f;
    float output = 0.0f;

private:
    void Set(float play, float record, float position, float length) {
        values[Engine::kPlay] = play;
        values[Engine::kRecord] = record;
        values[Engine::kPosition] = position;
        values[Engine::kLength] = length;
    }

    Engine engine_;  // only for its table
};

// Lit pixels in the rectangle from (x0, y0) to (x1, y1), inclusive.
int Lit(const wf::oled::Display& display, int x0, int y0, int x1, int y1) {
    int lit = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) lit += display.Pixel(x, y);
    }
    return lit;
}

// The layout the checks probe: cell i spans x 1 + 7i to 6 + 7i and y 24 to
// 35; the playing cell also covers rows 22-23 and 36-37.
int CellX(int i) {
    return 1 + 7 * i;
}
int CellInterior(const wf::oled::Display& display, int i) {
    return Lit(display, CellX(i) + 1, 25, CellX(i) + 4, 34);
}
int TallCells(const wf::oled::Display& display) {
    return Lit(display, 0, 22, 117, 23);
}
bool HeaderReversed(const wf::oled::Display& display) {
    return display.Pixel(0, 0);
}

struct Frame {
    wf::oled::Display display;
    wf::stratus::OledScreen screen{0};

    const wf::oled::Display& Draw(const FakeReadout& rig, std::uint32_t ms = 0) {
        screen.Draw(display.U8g2(), ms, rig);
        return display;
    }
};

void CheckDeterminism() {
    FakeReadout rig;
    rig.Overdub(2.5f, 6.0f);
    rig.input = 0.3f;
    rig.output = 0.6f;
    Frame a, b;
    a.Draw(rig, 0);
    b.Draw(rig, 0);
    Expect(std::memcmp(a.display.Buffer(), b.display.Buffer(), wf::oled::kBufferBytes) == 0,
           "the same values draw the same frame");
    b.Draw(rig, 5000);
    Expect(std::memcmp(a.display.Buffer(), b.display.Buffer(), wf::oled::kBufferBytes) == 0,
           "nothing moves with time alone");
}

void CheckEmpty() {
    FakeReadout rig;
    Frame f;
    const auto& d = f.Draw(rig);
    Expect(!HeaderReversed(d), "EMPTY is not reversed");
    Expect(Lit(d, 70, 0, 127, 8) == 0, "no time in the header while empty");
    Expect(!d.Pixel(CellX(0), 24) && d.Pixel(CellX(0) + 3, 30), "empty cells are dots, not frames");
    Expect(TallCells(d) == 0, "no playhead while empty");
}

void CheckRecording() {
    FakeReadout rig;
    Frame f;
    rig.Recording(15.0f);  // half of 30 s
    const auto& d = f.Draw(rig);
    Expect(HeaderReversed(d), "RECORDING is reversed");
    Expect(Lit(d, 70, 0, 127, 8) > 0, "the time left shows in the header");
    Expect(CellInterior(d, 7) == 40 && CellInterior(d, 8) == 0, "half the take fills eight cells");
    Expect(d.Pixel(CellX(15), 24), "the empty cells keep their frames: the time left");
    Expect(TallCells(d) == 0, "no playhead during the first take");

    rig.Recording(15.9375f);  // half of the ninth cell
    f.Draw(rig);
    Expect(d.Pixel(CellX(8) + 2, 34) && d.Pixel(CellX(8) + 2, 30) && !d.Pixel(CellX(8) + 2, 29),
           "the filling cell fills from the bottom");

    rig.Recording(30.0f);
    f.Draw(rig);
    Expect(CellInterior(d, 15) == 40, "the full take fills every cell");
}

void CheckPlaying() {
    FakeReadout rig;
    Frame f;
    rig.Playing(3.0f, 6.0f);
    const auto& d = f.Draw(rig);
    Expect(!HeaderReversed(d), "PLAYING is not reversed");
    Expect(Lit(d, CellX(8), 22, CellX(8) + 5, 23) == 12 && TallCells(d) == 12,
           "halfway through the loop, only the ninth cell stands tall");
    Expect(CellInterior(d, 0) == 0 && CellInterior(d, 9) == 0, "the other cells are empty frames");

    rig.Playing(0.0f, 6.0f);
    f.Draw(rig);
    Expect(Lit(d, CellX(0), 22, CellX(0) + 5, 23) == 12, "the downbeat is the first cell");
    rig.Playing(6.0f, 6.0f);
    f.Draw(rig);
    Expect(Lit(d, CellX(15), 22, CellX(15) + 5, 23) == 12 && TallCells(d) == 12,
           "a position at the length stays on the last cell");
}

void CheckOverdubAndStop() {
    FakeReadout rig;
    Frame f;
    rig.Overdub(3.0f, 6.0f);
    const auto& d = f.Draw(rig);
    Expect(HeaderReversed(d), "OVERDUB is reversed");
    Expect(TallCells(d) == 12, "the playhead stands tall while overdubbing");
    Expect(CellInterior(d, 0) > 0 && CellInterior(d, 7) > 0 && CellInterior(d, 9) == 0,
           "the cells passed this lap are stippled");

    rig.Stopped(3.0f, 6.0f);
    f.Draw(rig);
    Expect(!HeaderReversed(d) && TallCells(d) == 0, "stopped: no playhead");
    Expect(d.Pixel(CellX(0), 24) && CellInterior(d, 0) == 0, "stopped: the loop's cells stay as frames");
    Expect(Lit(d, 70, 0, 127, 8) > 0, "stopped: position and length stay in the header");
}

void CheckLevelAndThru() {
    FakeReadout rig;
    Frame f;
    const auto& d = f.Draw(rig);
    Expect(d.Pixel(19, 57) && d.Pixel(90, 57), "0 dB fills the LVL bar");
    Expect(d.Pixel(127, 53), "THRU is reversed while on");

    rig.values[Engine::kLoopLevel] = -30.0f;
    rig.values[Engine::kThru] = 0.0f;
    f.Draw(rig);
    Expect(d.Pixel(54, 57) && !d.Pixel(55, 57), "-30 dB fills half the LVL bar");
    Expect(!d.Pixel(127, 53), "thru is plain while off");

    rig.values[Engine::kLoopLevel] = -60.0f;
    f.Draw(rig);
    Expect(!d.Pixel(19, 57), "the bottom of Loop Level, silence, leaves the bar empty");
}

void CheckMeters() {
    FakeReadout rig;
    Frame f;
    const auto& d = f.Draw(rig);
    const int silentIn = Lit(d, 119, 12, 121, 44);
    const int silentOut = Lit(d, 124, 12, 126, 44);
    rig.input = 1.0f;
    f.Draw(rig);
    Expect(Lit(d, 119, 12, 121, 44) > silentIn && Lit(d, 124, 12, 126, 44) == silentOut,
           "the IN meter follows the input alone");
    rig.output = 0.5f;
    f.Draw(rig);
    Expect(Lit(d, 124, 12, 126, 44) > silentOut, "the OUT meter follows the output");
}

}  // namespace

int main() {
    CheckDeterminism();
    CheckEmpty();
    CheckRecording();
    CheckPlaying();
    CheckOverdubAndStop();
    CheckLevelAndThru();
    CheckMeters();

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
