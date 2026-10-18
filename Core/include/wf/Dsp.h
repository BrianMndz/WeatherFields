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

}  // namespace wf
