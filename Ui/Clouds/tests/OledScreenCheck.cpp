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

// Clouds in slot 0 at its defaults, with levels set by hand: the output at
// full scale, so every grain shows.
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
    Focus ViewFocus() const noexcept override { return focus; }

    float values[Engine::kPlaybackMode + 1] = {};
    float input = 0.0f;
    float output = 1.0f;
    Focus focus{false, 0, 0};

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
        wf::clouds::OledScreen fresh(0);  // no popup: its first frame
        fresh.Draw(display.U8g2(), 300, rig);
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

// The output level thins the cloud, keeping the same grains.
void CheckActivity() {
    FakeReadout rig;
    auto litOver = [&rig](float output) {
        rig.output = output;
        wf::oled::Display display;
        wf::clouds::OledScreen screen(0);
        int lit = 0;
        for (std::uint32_t ms = 0; ms < 4000; ms += 100) {
            screen.Draw(display.U8g2(), ms, rig);
            lit += CloudLit(display);
        }
        return lit;
    };
    const int silent = litOver(0.0f);
    const int quiet = litOver(0.0001f);  // -80 dB
    const int half = litOver(0.0158f);   // -36 dB
    const int full = litOver(0.0631f);   // -24 dB
    const int loud = litOver(1.0f);
    Expect(silent == 0 && quiet == 0, "no grains in silence");
    Expect(half > 0 && half < full, "fewer grains between -48 and -24 dB");
    Expect(full == loud, "every grain from -24 dB");
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
    rig.output = 0.0f;
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

// The header popup: its bar on rows 9 and 10, where the main screen has only
// its solid rule on row 10. A bar at its origin leaves row 9 empty, but then
// row 10 is dotted.
bool HeaderPopupShown(const wf::oled::Display& display) {
    return Lit(display, 0, 9, 127, 9) > 0 || Lit(display, 0, 10, 127, 10) < 128;
}

// A screen that has drawn its first frame at 0 ms with no grains, so popups
// open on the changes that follow.
struct PopupFixture {
    PopupFixture() {
        rig.values[Engine::kDensity] = 0.5f;
        screen.Draw(display.U8g2(), 0, rig);
    }
    void Draw(std::uint32_t ms) { screen.Draw(display.U8g2(), ms, rig); }

    FakeReadout rig;
    wf::oled::Display display;
    wf::clouds::OledScreen screen{0};
};

void CheckHeaderPopup() {
    {
        PopupFixture f;
        Expect(!HeaderPopupShown(f.display), "the first frame opens no popup");
        f.rig.values[Engine::kPitch] = 24.0f;
        f.Draw(1000);
        Expect(HeaderPopupShown(f.display), "a changed knob opens its popup");
        // PITCH's origin is the middle, x 64, marked on rows 8 to 11.
        Expect(Lit(f.display, 64, 9, 127, 10) == 64 * 2 && Lit(f.display, 0, 9, 63, 9) == 0,
               "+24 st fills the bar's right half");
        Expect(Lit(f.display, 64, 8, 64, 11) == 4, "the origin is marked");
        Expect(Lit(f.display, 0, 12, 127, 63) > 0 && Lit(f.display, 119, 12, 126, 44) > 0,
               "the rest of the screen stays");
        f.Draw(2499);
        Expect(HeaderPopupShown(f.display), "it stays for 1.5 s");
        f.Draw(2500);
        Expect(!HeaderPopupShown(f.display), "then closes");

        f.rig.values[Engine::kPitch] = -24.0f;
        f.Draw(3000);
        f.rig.values[Engine::kPitch] = -23.0f;
        f.Draw(4000);
        f.Draw(5400);
        Expect(HeaderPopupShown(f.display), "each change restarts its time");
        f.rig.values[Engine::kPitch] = -24.0f;
        f.Draw(5500);
        Expect(Lit(f.display, 0, 9, 63, 9) == 64 && Lit(f.display, 65, 9, 127, 9) == 0,
               "-24 st fills the left half");
    }
    {
        PopupFixture f;
        f.rig.values[Engine::kPosition] = 0.0f;
        f.Draw(100);
        Expect(HeaderPopupShown(f.display) && Lit(f.display, 0, 9, 127, 9) == 0,
               "a knob at its origin opens an empty bar");
    }
    {
        PopupFixture f;
        const Engine::Param quiet[] = {Engine::kDryWet, Engine::kStereoSpread, Engine::kFeedback,
                                       Engine::kReverb, Engine::kFreeze,     Engine::kGate};
        std::uint32_t ms = 0;
        for (Engine::Param id : quiet) {
            f.rig.values[id] = f.rig.values[id] > 0.5f ? 0.0f : 1.0f;  // a change from any default
            f.Draw(ms += 100);
            Expect(!HeaderPopupShown(f.display), "the BLEND values, freeze and the gate open no popup");
        }
    }
}

// The choice popup: a reversed title, the options at rows 11, 22, 33 and 44,
// 11 high, the current one reversed, and a note at the bottom.
void CheckChoicePopup() {
    {
        PopupFixture f;
        f.rig.values[Engine::kQuality] = 2.0f;
        f.rig.values[Engine::kFreeze] = 0.0f;  // as the engine releases it
        f.Draw(100);
        Expect(Lit(f.display, 0, 0, 127, 8) > 1000, "a changed quality shows its title reversed");
        Expect(Lit(f.display, 2, 33, 125, 43) > 900 && Lit(f.display, 2, 11, 125, 21) < 300,
               "the new quality reversed, the others not");
        Expect(Lit(f.display, 0, 57, 127, 63) > 0, "a quality change notes the erased recording");
        f.Draw(1600);
        Expect(Lit(f.display, 0, 0, 127, 8) < 400, "it closes after 1.5 s");
    }
    {
        PopupFixture f;
        f.rig.values[Engine::kPlaybackMode] = 1.0f;
        f.Draw(100);
        Expect(Lit(f.display, 2, 22, 125, 32) > 900, "a changed mode shows the new one reversed");
        Expect(Lit(f.display, 0, 56, 127, 63) == 0, "granular to stretch keeps the recording");
        f.rig.values[Engine::kPlaybackMode] = 3.0f;
        f.Draw(200);
        Expect(Lit(f.display, 0, 57, 127, 63) > 0, "to spectral erases it");
        f.rig.values[Engine::kPlaybackMode] = 0.0f;
        f.Draw(300);
        Expect(Lit(f.display, 0, 57, 127, 63) > 0, "and so does from spectral");
        f.rig.values[Engine::kSize] = 0.1f;
        f.Draw(400);
        Expect(HeaderPopupShown(f.display) && Lit(f.display, 0, 47, 127, 54) > 0,
               "a newer change replaces the popup");
    }
}

// The view's focus on a choice shows its popup for as long as it lasts,
// changed or not, as the panel's LEDs show the quality or mode display.
void CheckFocus() {
    auto title = [](const wf::oled::Display& display) { return Lit(display, 0, 0, 127, 8) > 1000; };
    auto note = [](const wf::oled::Display& display) { return Lit(display, 0, 57, 127, 63) > 0; };
    {
        PopupFixture f;
        f.rig.focus = {true, 0, Engine::kPlaybackMode};
        f.Draw(100);
        Expect(title(f.display) && Lit(f.display, 2, 11, 125, 21) > 900,
               "a focused mode shows its popup, the current mode reversed, unchanged");
        Expect(!note(f.display), "no erased note without a change");
        f.Draw(5000);
        Expect(title(f.display), "it stays while focused");

        f.rig.values[Engine::kPlaybackMode] = 3.0f;
        f.Draw(5100);
        f.Draw(9000);
        Expect(title(f.display) && Lit(f.display, 2, 44, 125, 54) > 900 && note(f.display),
               "a change while focused moves the selection, and its note stays while focused");

        f.rig.values[Engine::kPitch] = 5.0f;
        f.Draw(9100);
        Expect(!title(f.display) && HeaderPopupShown(f.display), "a knob's change still shows over it");
        f.Draw(10700);
        Expect(title(f.display) && !note(f.display), "then the focused popup returns, its note gone");

        f.rig.focus = {false, 0, 0};
        f.Draw(10800);
        Expect(!title(f.display) && !HeaderPopupShown(f.display), "losing the focus closes it");
    }
    {
        PopupFixture f;
        f.rig.focus = {true, 0, Engine::kQuality};
        f.Draw(100);
        f.rig.values[Engine::kQuality] = 1.0f;
        f.Draw(200);
        f.rig.focus = {false, 0, 0};
        f.Draw(300);
        Expect(title(f.display) && note(f.display), "a change keeps its popup for 1.5 s after the focus ends");
        f.Draw(1700);
        Expect(!title(f.display), "then it closes");
    }
    {
        PopupFixture f;
        f.rig.focus = {true, 0, Engine::kPlaybackMode};
        f.Draw(100);
        f.rig.values[Engine::kQuality] = 1.0f;  // from elsewhere, MIDI say
        f.Draw(200);
        Expect(note(f.display) && Lit(f.display, 2, 22, 125, 32) > 900, "the quality change shows over the mode");
        f.Draw(1800);
        Expect(title(f.display) && Lit(f.display, 2, 11, 125, 21) > 900 && !note(f.display),
               "the focused mode returns without the quality's note");
    }
    {
        PopupFixture f;
        f.rig.focus = {true, 1, Engine::kQuality};
        f.Draw(100);
        Expect(!title(f.display), "a focus on another slot shows nothing");
        f.rig.focus = {true, 0, Engine::kPitch};
        f.Draw(200);
        Expect(!title(f.display) && !HeaderPopupShown(f.display), "nor does one on a knob");
    }
}

}  // namespace

int main() {
    CheckDeterminism();
    CheckFreeze();
    CheckDensity();
    CheckPlacement();
    CheckActivity();
    CheckBlendBars();
    CheckMeters();
    CheckHeaderPopup();
    CheckChoicePopup();
    CheckFocus();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "wf_clouds_oled_screen_check passed\n";
    return 0;
}
