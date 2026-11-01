#include <cmath>
#include <cstdint>
#include <iostream>

#include "wf/controls/Button.h"
#include "wf/controls/Knob.h"

namespace {

using wf::controls::Button;
using wf::controls::Knob;

int failures = 0;

void Expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

constexpr float kCallbackMs = 32.0f / 44100.0f * 1000.0f;
constexpr float kInputTop = Knob::kInputTop;
constexpr float kFirmwareSlow = 0.01f;  // CvScaler's coefficient for most knobs

// A callback mean with the worst noise the probe saw near the top: 0.12% of
// the knob's range, peak to peak, as a repeatable pattern.
struct Noise {
    std::uint32_t state = 1;
    float operator()() {
        state = state * 1664525u + 1013904223u;
        const float unit = static_cast<float>(state >> 16) / 65535.0f - 0.5f;
        return unit * 0.0012f * kInputTop;
    }
};

int CallbacksFor(float ms, float callbackMs = kCallbackMs) {
    return static_cast<int>(std::ceil(ms / callbackMs));
}

// Holds `reading`, returning how many callbacks changed the value.
int Hold(Knob& knob, float reading, float ms, float callbackMs = kCallbackMs) {
    int changes = 0;
    for (int i = 0; i < CallbacksFor(ms, callbackMs); ++i) changes += knob.Update(reading) ? 1 : 0;
    return changes;
}

Knob Started(float reading) {
    Knob knob;
    knob.Configure(kFirmwareSlow, kCallbackMs);
    knob.Update(reading);
    return knob;
}

void CheckCalibration() {
    Knob knob;
    knob.Configure(kFirmwareSlow, kCallbackMs);
    Expect(knob.Update(0.41f), "the first reading is a change");
    Expect(std::fabs(knob.Value() - 0.5f) < 1e-6f, "0.41 reads as the middle");
    Expect(!knob.Update(0.41f), "and the second, the same, is none");

    Expect(Started(0.0f).Value() == 0.0f, "the bottom is 0");
    Expect(Started(0.00003f).Value() == 0.0f, "the bottom with 2 ADC steps of noise is 0");
    Expect(Started(0.8275f).Value() == 1.0f, "the lowest top seen is 1");
    Expect(Started(0.8285f).Value() == 1.0f, "the highest top seen is 1");
    Expect(Started(1.0f).Value() == 1.0f, "the start-up's full-scale reading is 1, not more");
    Expect(Started(-0.1f).Value() == 0.0f, "nothing reads below 0");

    knob.Configure(kFirmwareSlow, kCallbackMs);
    knob.Update(0.1f);
    Expect(std::fabs(knob.Value() - 0.1f / kInputTop) < 1e-6f, "Configure() starts a new reading");
}

void CheckQuietAtRest() {
    for (float reading : {0.05f, 0.41f, 0.81f}) {
        Knob knob = Started(reading);
        const float value = knob.Value();
        Noise noise;
        int changes = 0;
        for (int i = 0; i < CallbacksFor(60000.0f); ++i) changes += knob.Update(reading + noise()) ? 1 : 0;
        Expect(changes == 0, "a minute of the worst noise changes nothing");
        Expect(knob.Value() == value, "and leaves the value");

        // The resting values' slow drift, about 20 ADC steps.
        Knob drifting = Started(reading);
        Expect(Hold(drifting, reading + 20.0f / 65536.0f, 10000.0f) == 0, "drift changes nothing");
    }
}

void CheckTouch() {
    Knob knob = Started(0.41f);
    const float below = 0.41f + 0.008f * kInputTop;  // past nothing, even settled
    Expect(Hold(knob, below, 5000.0f) == 0, "0.8% of the range stays quiet");
    const float past = 0.41f + 0.015f * kInputTop;
    Expect(Hold(knob, past, 5000.0f) > 0, "1.5% of the range wakes it");
    Expect(std::fabs(knob.Value() - past / kInputTop) < Knob::kRest * 2.0f, "and it follows");
}

void CheckFilterTimeConstant() {
    // The firmware's coefficient at 1 kHz: from 0 towards 1, this share after
    // n milliseconds.
    for (float callbackMs : {kCallbackMs, 64.0f / 44100.0f * 1000.0f, 128.0f / 48000.0f * 1000.0f}) {
        Knob knob;
        knob.Configure(kFirmwareSlow, callbackMs);
        knob.Update(0.0f);
        const int callbacks = CallbacksFor(100.0f, callbackMs);
        for (int i = 0; i < callbacks; ++i) knob.Update(kInputTop);
        const float expected = 1.0f - std::pow(1.0f - kFirmwareSlow, static_cast<float>(callbacks) * callbackMs);
        Expect(std::fabs(knob.Value() - expected) < 1e-3f, "the firmware's time constant at this callback size");
    }
}

void CheckSettles() {
    Knob knob = Started(0.0f);
    Expect(Hold(knob, 0.41f, 100.0f) > 0, "a turn changes the value");
    Noise noise;
    int changes = 1;
    int quietCallbacks = 0;
    for (int i = 0; i < CallbacksFor(5000.0f); ++i) {
        changes = knob.Update(0.41f + noise()) ? 1 : 0;
        quietCallbacks = changes != 0 ? 0 : quietCallbacks + 1;
    }
    Expect(quietCallbacks >= CallbacksFor(3000.0f), "it goes quiet after it stops");
    Expect(std::fabs(knob.Value() - 0.5f) < 0.005f, "where the knob is");

    // Still moving slowly, no pause long enough to rest: every callback counts.
    Knob slow = Started(0.0f);
    Hold(slow, 0.2f, 2000.0f);
    float reading = 0.2f;
    int changed = 0;
    const int callbacks = CallbacksFor(1000.0f);
    for (int i = 0; i < callbacks; ++i) {
        reading += 0.1f / static_cast<float>(callbacks);
        changed += slow.Update(reading) ? 1 : 0;
    }
    Expect(changed > callbacks * 3 / 4, "a steady turn sends steadily, after the touch");
}

void CheckReachesEnds() {
    Knob knob = Started(0.41f);
    bool inRange = true;
    for (int i = 0; i < CallbacksFor(5000.0f); ++i) {
        knob.Update(0.8285f);
        inRange = inRange && knob.Value() <= 1.0f;
    }
    Expect(inRange, "the value never passes 1 on its way");
    Expect(knob.Value() == 1.0f, "turned fully up it settles at exactly 1");
    Expect(Hold(knob, 0.828f, 2000.0f) == 0, "and stays quiet");

    Hold(knob, 0.0f, 5000.0f);
    Expect(knob.Value() == 0.0f, "turned fully down it settles at exactly 0");
    Expect(Hold(knob, 0.00003f, 2000.0f) == 0, "and stays quiet");

    Hold(knob, 0.41f, 5000.0f);
    Expect(knob.Value() > 0.49f && knob.Value() < 0.51f, "and leaves the end again");
}

int Tap(Button& button, int callbacks) {
    int presses = 0;
    for (int i = 0; i < callbacks; ++i) {
        button.Update(true);
        presses += button.JustPressed() ? 1 : 0;
    }
    return presses;
}

void CheckButton() {
    Button button;
    button.Update(false);
    Expect(!button.Pressed() && !button.JustPressed(), "released at start");
    for (int i = 0; i < Button::kStableSamples - 1; ++i) {
        button.Update(true);
        Expect(!button.Pressed(), "not before enough equal samples");
    }
    button.Update(true);
    Expect(button.Pressed() && button.JustPressed(), "the change after them");
    button.Update(true);
    Expect(button.Pressed() && !button.JustPressed(), "reported for one callback");

    for (int i = 0; i < Button::kStableSamples - 1; ++i) button.Update(false);
    Expect(button.Pressed(), "still pressed");
    button.Update(false);
    Expect(!button.Pressed() && button.JustReleased(), "released after them");
    button.Update(false);
    Expect(!button.JustReleased(), "reported for one callback");

    // A bounce of 0.54 ms is one sample at 32-frame callbacks.
    int presses = 0;
    for (int i = 0; i < 100; ++i) {
        button.Update(i % 2 == 0);
        presses += button.JustPressed() ? 1 : 0;
    }
    Expect(presses == 0, "alternating samples are no press");
    for (int i = 0; i < 10; ++i) button.Update(false);

    // Bouncing into a press: one press.
    const bool edge[] = {true, false, true, false, true, true, true, true, true};
    presses = 0;
    for (bool sample : edge) {
        button.Update(sample);
        presses += button.JustPressed() ? 1 : 0;
    }
    Expect(presses == 1, "a bouncing press is one press");
    for (int i = 0; i < 10; ++i) button.Update(false);

    // The quickest tap seen, 27 ms, at 32 and at 128 frames.
    Expect(Tap(button, static_cast<int>(27.0f / kCallbackMs)) == 1, "a 27 ms tap at 32 frames");
    for (int i = 0; i < 10; ++i) button.Update(false);
    Expect(Tap(button, static_cast<int>(27.0f / (128.0f / 44.1f))) == 1, "and at 128 frames");

    // Held at start-up: no press, and its release is only a release.
    Button held;
    held.Update(true);
    Expect(held.Pressed() && !held.JustPressed(), "a button held at start-up is no press");
    for (int i = 0; i < Button::kStableSamples; ++i) held.Update(false);
    Expect(!held.Pressed() && held.JustReleased(), "its release is seen");

    held.Reset();
    held.Update(true);
    Expect(!held.JustPressed(), "Reset() starts a new reading");
}

}  // namespace

int main() {
    CheckCalibration();
    CheckQuietAtRest();
    CheckTouch();
    CheckFilterTimeConstant();
    CheckSettles();
    CheckReachesEnds();
    CheckButton();
    if (failures != 0) return 1;
    std::cout << "controls check passed\n";
    return 0;
}
