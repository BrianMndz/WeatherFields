#include <cstdint>
#include <cstring>
#include <iostream>

#include "wf/clouds/Engine.h"
#include "wf/clouds/OledScreen.h"
#include "wf/oled/Display.h"

namespace {

using wf::clouds::Engine;

int failures = 0;

void Expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// Clouds in slot 0 at its defaults, with levels set by hand.
class FakeReadout final : public wf::oled::RigReadout {
public:
    FakeReadout() {
        const wf::ParamTable& table = engine_.Params();
        for (std::size_t i = 0; i < table.count; ++i) values[table.specs[i].id] = table.specs[i].def;
    }

    float Value(std::size_t slot, wf::ParamId id) const noexcept override {
        return slot == 0 && id <= Engine::kPlaybackMode ? values[id] : 0.0f;
    }
    float InputLevel() const noexcept override { return input; }
    float OutputLevel() const noexcept override { return output; }

    float values[Engine::kPlaybackMode + 1] = {};
    float input = 0.0f;
    float output = 0.0f;

private:
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

// The cloud's area, inside the freeze frame.
int CloudLit(const wf::oled::Display& display) {
    return Lit(display, 1, 13, 115, 40);
}

// The mean column and row of the cloud's lit pixels over a few frames.
void CloudCentre(const FakeReadout& rig, float& x, float& y) {
    wf::oled::Display display;
    wf::clouds::OledScreen screen(0);
    double sumX = 0.0, sumY = 0.0;
    int lit = 0;
    for (std::uint32_t ms = 0; ms < 4000; ms += 250) {
        screen.Draw(display.U8g2(), ms, rig);
        for (int row = 12; row < 40; ++row) {
            for (int column = 0; column < 117; ++column) {
                if (display.Pixel(column, row)) {
                    sumX += column;
                    sumY += row;
                    ++lit;
                }
            }
        }
    }
    x = lit != 0 ? static_cast<float>(sumX / lit) : -1.0f;
    y = lit != 0 ? static_cast<float>(sumY / lit) : -1.0f;
}

bool Same(const wf::oled::Display& a, const wf::oled::Display& b) {
    return std::memcmp(a.Buffer(), b.Buffer(), wf::oled::kBufferBytes) == 0;
}

void CheckDeterminism() {
    FakeReadout rig;
    wf::oled::Display first, second;
    wf::clouds::OledScreen a(0), b(0);
    a.Draw(first.U8g2(), 0, rig);
    b.Draw(second.U8g2(), 0, rig);
    a.Draw(first.U8g2(), 1234, rig);
    b.Draw(second.U8g2(), 1234, rig);
    Expect(Same(first, second), "a frame depends only on the values and the time");
}

void CheckFreeze() {
    FakeReadout rig;
    wf::oled::Display display;
    wf::clouds::OledScreen screen(0);
    screen.Draw(display.U8g2(), 0, rig);
    Expect(Lit(display, 108, 0, 127, 8) == 0, "no FRZ tag while not frozen");

    rig.values[Engine::kFreeze] = 1.0f;
    screen.Draw(display.U8g2(), 1000, rig);
    Expect(Lit(display, 108, 0, 127, 8) > 100, "a reversed FRZ tag while frozen");
    Expect(display.Pixel(0, 12) && display.Pixel(116, 12) && display.Pixel(0, 41) && display.Pixel(116, 41),
           "a frame around the cloud while frozen");

    wf::oled::Display later;
    screen.Draw(later.U8g2(), 1700, rig);
    Expect(Same(display, later), "the frozen cloud stands still");

    rig.values[Engine::kFreeze] = 0.0f;
    screen.Draw(later.U8g2(), 2400, rig);
    wf::oled::Display moved;
    screen.Draw(moved.U8g2(), 2700, rig);
    Expect(!Same(later, moved), "it moves again when released");
}

// The module's density: no grains around noon, more away from it.
void CheckDensity() {
    FakeReadout rig;
    wf::oled::Display display;
    wf::clouds::OledScreen screen(0);
    rig.values[Engine::kDensity] = 0.5f;
    int lit = 0;
    for (std::uint32_t ms = 0; ms < 2000; ms += 100) {
        screen.Draw(display.U8g2(), ms, rig);
        lit += CloudLit(display);
    }
    Expect(lit == 0, "no grains at noon");

    for (float density : {0.1f, 0.9f}) {
        rig.values[Engine::kDensity] = density;
        screen.Draw(display.U8g2(), 300, rig);
        Expect(CloudLit(display) > 0, "grains away from noon, either side");
    }
}

void CheckPlacement() {
    FakeReadout rig;
    rig.values[Engine::kTexture] = 0.0f;
    float lowX, lowY, highX, highY;

    rig.values[Engine::kPosition] = 0.1f;
    CloudCentre(rig, lowX, lowY);
    rig.values[Engine::kPosition] = 0.9f;
    CloudCentre(rig, highX, highY);
    Expect(lowX >= 0.0f && highX > lowX + 50.0f, "POSITION moves the cloud right");

    rig.values[Engine::kPosition] = 0.5f;
    rig.values[Engine::kPitch] = -24.0f;
    CloudCentre(rig, lowX, lowY);
    rig.values[Engine::kPitch] = 24.0f;
    CloudCentre(rig, highX, highY);
    Expect(lowY >= 0.0f && highY >= 0.0f && highY < lowY - 15.0f, "PITCH raises the cloud");
}

// MIX's bar: frame from (17, 47) to (62, 54), fill inside it.
void CheckBlendBars() {
    FakeReadout rig;
    wf::oled::Display display;
    wf::clouds::OledScreen screen(0);
    rig.values[Engine::kDryWet] = 0.0f;
    screen.Draw(display.U8g2(), 0, rig);
    Expect(Lit(display, 19, 49, 60, 52) == 0, "an empty bar at 0");
    rig.values[Engine::kDryWet] = 1.0f;
    screen.Draw(display.U8g2(), 0, rig);
    Expect(Lit(display, 19, 49, 60, 52) == 42 * 4, "a full bar at 1");
    rig.values[Engine::kDryWet] = 0.5f;
    screen.Draw(display.U8g2(), 0, rig);
    Expect(Lit(display, 19, 49, 39, 52) == 21 * 4 && Lit(display, 40, 49, 60, 52) == 0, "half a bar at 0.5");
}

// IN at x 119-121, OUT at 124-126, rows 12 to 44: 11 segments of 2x3 pixels,
// a dot where unlit.
void CheckMeters() {
    FakeReadout rig;
    wf::oled::Display display;
    wf::clouds::OledScreen screen(0);
    screen.Draw(display.U8g2(), 0, rig);
    Expect(Lit(display, 119, 12, 121, 44) == 11 && Lit(display, 124, 12, 126, 44) == 11, "silence: dots only");

    rig.output = 1.0f;
    screen.Draw(display.U8g2(), 0, rig);
    Expect(Lit(display, 124, 12, 126, 44) == 66, "full scale lights every segment");

    rig.input = 0.5f;  // -6 dB
    screen.Draw(display.U8g2(), 0, rig);
    const int plain = Lit(display, 119, 12, 121, 44);
    rig.values[Engine::kInputGain] = 6.0f;
    screen.Draw(display.U8g2(), 0, rig);
    const int boosted = Lit(display, 119, 12, 121, 44);
    Expect(plain > 11 && boosted == 66, "IN follows IN GAIN");
}

}  // namespace

int main() {
    CheckDeterminism();
    CheckFreeze();
    CheckDensity();
    CheckPlacement();
    CheckBlendBars();
    CheckMeters();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "wf_clouds_oled_screen_check passed\n";
    return 0;
}
