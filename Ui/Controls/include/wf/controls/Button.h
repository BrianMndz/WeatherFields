#pragma once

namespace wf {
namespace controls {

// A push button on the board, debounced from one sample per callback: a change
// counts after kStableSamples equal samples, about 3 ms at 32-frame callbacks.
// The panel probe (2026-10-24) saw bounces of at most 0.54 ms and a quickest
// tap of 27 ms. The first sample only sets the state, so a button held
// at start-up is no press.
class Button {
public:
    static constexpr int kStableSamples = 4;

    // Not real-time. The next Update() starts a new reading.
    void Reset() noexcept {
        started_ = false;
        pressed_ = false;
        justPressed_ = false;
        justReleased_ = false;
        differing_ = 0;
    }

    // Real-time, once per callback.
    void Update(bool sample) noexcept {
        justPressed_ = false;
        justReleased_ = false;
        if (!started_) {
            started_ = true;
            pressed_ = sample;
            return;
        }
        if (sample == pressed_) {
            differing_ = 0;
            return;
        }
        if (++differing_ < kStableSamples) return;
        differing_ = 0;
        pressed_ = sample;
        justPressed_ = sample;
        justReleased_ = !sample;
    }

    bool Pressed() const noexcept { return pressed_; }
    // For the one Update() that saw the change.
    bool JustPressed() const noexcept { return justPressed_; }
    bool JustReleased() const noexcept { return justReleased_; }

private:
    bool started_ = false;
    bool pressed_ = false;
    bool justPressed_ = false;
    bool justReleased_ = false;
    int differing_ = 0;
};

}  // namespace controls
}  // namespace wf
