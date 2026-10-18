#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wf {

namespace detail {

// Hides a value from the optimiser without emitting an instruction.
inline std::uint32_t Opaque(std::uint32_t value) noexcept {
#if defined(__GNUC__)
    __asm__("" : "+r"(value));
    return value;
#else
    volatile std::uint32_t copy = value;
    return copy;
#endif
}

}  // namespace detail

// Bela builds with -ffast-math, which lets the compiler assume that no value
// is NaN or infinite. On the board's Clang 3.9, std::isfinite() then passes
// NaN; current Clang also recognises an exponent-bit test as a class test
// and folds it to true. Testing the bits behind an optimisation barrier
// survives both.
inline bool IsFinite(float value) noexcept {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return (detail::Opaque(bits) & 0x7f800000u) != 0x7f800000u;
}

// The exponent lives in the high word, so the barrier stays one 32-bit
// register on 32-bit targets.
inline bool IsFinite(double value) noexcept {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t high = static_cast<std::uint32_t>(bits >> 32);
    return (detail::Opaque(high) & 0x7ff00000u) != 0x7ff00000u;
}

// Writes `in` times a gain into `out`: the gain starts at `start` on the
// first frame and moves linearly towards `end`, which it would reach on the
// frame after the last, as JUCE's AudioBuffer::applyGainRamp() does. A
// change of gain then spreads over one callback instead of clicking.
// `out` may be `in`.
inline void ApplyGainRamp(const float* in, float* out, std::size_t frames,
                          float start, float end) noexcept {
    if (frames == 0) return;
    if (start == end) {
        if (start == 1.0f) {
            if (in != out) std::memcpy(out, in, frames * sizeof(float));
            return;
        }
        for (std::size_t i = 0; i < frames; ++i) out[i] = in[i] * start;
        return;
    }
    const float increment = (end - start) / static_cast<float>(frames);
    float gain = start;
    for (std::size_t i = 0; i < frames; ++i) {
        out[i] = in[i] * gain;
        gain += increment;
    }
}

// The Clouds module's level meter (eurorack/clouds/meter.h) in floating
// point, as CloudsEngine's JUCE and Bela builds had it: each channel's peak
// rises towards |sample| with a 10 ms time constant and falls with a 250 ms
// one. It scales with its input, so a level times a constant gain is the
// level of the signal times that gain.
class PeakFollower {
public:
    // Not real-time. Also resets the peaks.
    void Configure(float sampleRate) noexcept {
        const bool valid = IsFinite(sampleRate) && sampleRate > 0.0f;
        attack_ = valid ? 100.0f / sampleRate : 0.0f;
        release_ = valid ? 4.0f / sampleRate : 0.0f;
        Reset();
    }

    void Reset() noexcept {
        left_ = 0.0f;
        right_ = 0.0f;
    }

    void Process(const float* left, const float* right, std::size_t frames) noexcept {
        float peakLeft = left_;
        float peakRight = right_;
        for (std::size_t i = 0; i < frames; ++i) {
            Follow(peakLeft, left[i]);
            Follow(peakRight, right[i]);
        }
        // One NaN or infinite sample would otherwise stick in the peak.
        left_ = IsFinite(peakLeft) ? peakLeft : 0.0f;
        right_ = IsFinite(peakRight) ? peakRight : 0.0f;
    }

    // The louder channel's peak, linear (1 is full scale).
    float Level() const noexcept { return left_ > right_ ? left_ : right_; }

private:
    void Follow(float& peak, float sample) const noexcept {
        const float magnitude = sample < 0.0f ? -sample : sample;
        const float error = magnitude - peak;
        peak += error * (error > 0.0f ? attack_ : release_);
    }

    float attack_ = 0.0f;
    float release_ = 0.0f;
    float left_ = 0.0f;
    float right_ = 0.0f;
};

}  // namespace wf
