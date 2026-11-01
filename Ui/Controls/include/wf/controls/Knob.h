#pragma once

#include <cmath>

namespace wf {
namespace controls {

// A pot on the board, from each callback's mean reading to a value from 0 to
// 1 that stays quiet at rest. Every published change opens an OLED popup and
// reaches the browser page, so unlike the firmware, which feeds its ADC
// straight to the DSP, a knob changes nothing until it is moved past
// kTouch or reaches an end, and stops changing once it settles.
//
// The numbers come from the panel probe (2026-10-24, PROJECT_GUIDE): the pots
// sit across the 3.3 V rail and read 0.8275-0.8285 at the top against the
// ADC's 4.096 V; the callback mean's noise is at most 0.12% of the range, near
// nothing at the bottom, and resting values drift by about 0.04%.
class Knob {
public:
    static constexpr float kInputTop = 0.82f;  // reaches the top with 1% to spare
    static constexpr float kTouch = 0.01f;     // 8 times the worst noise
    static constexpr float kRest = 0.0025f;    // 2 times the worst noise
    static constexpr float kRestMs = 250.0f;   // still for this long: at rest
    static constexpr float kEnd = 0.001f;      // filtered this near an end: at that end

    // Not real-time. `firmwareCoefficient`: the one-pole coefficient of the
    // module's CvScaler, which runs once a millisecond, kept as a time
    // constant at any callback size. The next Update() starts a new reading.
    void Configure(float firmwareCoefficient, float callbackMs) noexcept {
        coefficient_ = 1.0f - std::pow(1.0f - firmwareCoefficient, callbackMs);
        callbackMs_ = callbackMs;
        started_ = false;
        moving_ = false;
    }

    // Real-time, once per callback. Returns whether Value() changed; the
    // first call always does, so the hardware is the truth from the start.
    bool Update(float reading) noexcept {
        const float input = Calibrated(reading);
        if (!started_) {
            started_ = true;
            filtered_ = input;
            value_ = AtEnd(input);
            return true;
        }
        filtered_ += coefficient_ * (input - filtered_);

        if (!moving_) {
            if (std::fabs(filtered_ - value_) <= kTouch) {
                // The filter only approaches its input, so a knob turned
                // fully would otherwise settle just short of the end; and a
                // turn from nearby, inside kTouch, would never wake it.
                const float end = AtEnd(filtered_);
                if ((end != 0.0f && end != 1.0f) || end == value_) return false;
                value_ = end;
                return true;
            }
            moving_ = true;
            restFrom_ = filtered_;
            restMs_ = 0.0f;
        } else if (std::fabs(filtered_ - restFrom_) > kRest) {
            restFrom_ = filtered_;
            restMs_ = 0.0f;
        } else {
            restMs_ += callbackMs_;
            if (restMs_ >= kRestMs) {
                moving_ = false;
                return false;
            }
        }
        value_ = filtered_;
        return true;
    }

    float Value() const noexcept { return value_; }

private:
    static float Calibrated(float reading) noexcept {
        const float input = reading / kInputTop;
        return input > 0.0f ? (input < 1.0f ? input : 1.0f) : 0.0f;
    }

    static float AtEnd(float input) noexcept {
        return input <= kEnd ? 0.0f : input >= 1.0f - kEnd ? 1.0f : input;
    }

    float coefficient_ = 1.0f;
    float callbackMs_ = 1.0f;
    float filtered_ = 0.0f;
    float value_ = 0.0f;
    float restFrom_ = 0.0f;
    float restMs_ = 0.0f;
    bool started_ = false;
    bool moving_ = false;
};

}  // namespace controls
}  // namespace wf
