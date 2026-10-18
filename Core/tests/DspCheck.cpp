#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include "wf/Dsp.h"

namespace {

int failures = 0;

// Bit patterns of the values that must fail IsFinite(). Building them from
// bits, as they arrive from hardware, avoids constants that -ffast-math treats
// as undefined.
template <typename T>
struct Patterns;

template <>
struct Patterns<float> {
    using Bits = std::uint32_t;
    static constexpr Bits kSign = 0x80000000u;
    static constexpr Bits kInfinity = 0x7f800000u;
    static constexpr Bits kQuietNaN = 0x7fc00000u;
    static constexpr Bits kSignalingNaN = 0x7f800001u;
    static constexpr Bits kNaNAllMantissa = 0x7fffffffu;
};

template <>
struct Patterns<double> {
    using Bits = std::uint64_t;
    static constexpr Bits kSign = 0x8000000000000000u;
    static constexpr Bits kInfinity = 0x7ff0000000000000u;
    static constexpr Bits kQuietNaN = 0x7ff8000000000000u;
    static constexpr Bits kSignalingNaN = 0x7ff0000000000001u;
    static constexpr Bits kNaNAllMantissa = 0x7fffffffffffffffu;
};

template <typename T>
T FromBits(typename Patterns<T>::Bits bits) {
    T value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

template <typename T>
void Expect(T value, bool finite, const char* type, const char* name) {
    if (wf::IsFinite(value) != finite) {
        std::cerr << "FAIL: IsFinite(" << type << ' ' << name << ") should be "
                  << (finite ? "true" : "false") << '\n';
        ++failures;
    }
}

template <typename T>
void CheckIsFinite(const char* type) {
    using Limits = std::numeric_limits<T>;
    using P = Patterns<T>;

    Expect(T(0), true, type, "zero");
    Expect(-T(0), true, type, "negative zero");
    Expect(T(1), true, type, "one");
    Expect(-T(1), true, type, "minus one");
    Expect(Limits::max(), true, type, "largest");
    Expect(Limits::lowest(), true, type, "lowest");
    Expect(Limits::min(), true, type, "smallest normal");
    Expect(Limits::denorm_min(), true, type, "smallest subnormal");

    Expect(FromBits<T>(P::kInfinity), false, type, "infinity");
    Expect(FromBits<T>(P::kSign | P::kInfinity), false, type, "minus infinity");
    Expect(FromBits<T>(P::kQuietNaN), false, type, "quiet NaN");
    Expect(FromBits<T>(P::kSign | P::kQuietNaN), false, type, "negative quiet NaN");
    Expect(FromBits<T>(P::kSignalingNaN), false, type, "signaling NaN");
    Expect(FromBits<T>(P::kNaNAllMantissa), false, type, "NaN, all mantissa bits");
}

}  // namespace

void ExpectTrue(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void CheckGainRamp() {
    constexpr std::size_t kFrames = 8;
    float in[kFrames];
    for (std::size_t i = 0; i < kFrames; ++i) in[i] = 1.0f + static_cast<float>(i);

    // The ramp's gains, computed the way the function must: start, then one
    // increment per frame.
    float out[kFrames];
    wf::ApplyGainRamp(in, out, kFrames, 1.0f, 0.0f);
    const float increment = (0.0f - 1.0f) / static_cast<float>(kFrames);
    float gain = 1.0f;
    bool ramped = true;
    for (std::size_t i = 0; i < kFrames; ++i) {
        ramped = ramped && out[i] == in[i] * gain;
        gain += increment;
    }
    ExpectTrue(ramped, "ApplyGainRamp starts at `start` and steps by (end - start) / frames");
    ExpectTrue(out[0] == in[0] && out[kFrames - 1] == in[kFrames - 1] * 0.125f,
               "the last frame stops one step short of `end`");

    float inPlace[kFrames];
    std::memcpy(inPlace, in, sizeof(in));
    wf::ApplyGainRamp(inPlace, inPlace, kFrames, 1.0f, 0.0f);
    ExpectTrue(std::memcmp(inPlace, out, sizeof(out)) == 0, "in place gives the same ramp");

    wf::ApplyGainRamp(in, out, kFrames, 0.5f, 0.5f);
    bool constant = true;
    for (std::size_t i = 0; i < kFrames; ++i) constant = constant && out[i] == in[i] * 0.5f;
    ExpectTrue(constant, "equal gains apply a constant gain");

    wf::ApplyGainRamp(in, out, kFrames, 1.0f, 1.0f);
    ExpectTrue(std::memcmp(in, out, sizeof(in)) == 0, "unity gain copies exactly");

    out[0] = -1.0f;
    wf::ApplyGainRamp(in, out, 0, 0.0f, 1.0f);
    ExpectTrue(out[0] == -1.0f, "zero frames touch nothing");
}

int main() {
    CheckIsFinite<float>("float");
    CheckIsFinite<double>("double");
    CheckGainRamp();

    // A NaN made at run time, as a blown-up feedback path makes one.
    volatile float zero = 0.0f;
    Expect(zero / zero, false, "float", "0/0");

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
