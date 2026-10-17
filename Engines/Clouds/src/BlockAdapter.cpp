#include "wf/clouds/BlockAdapter.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
    std::int16_t FloatToInt16(float sample) noexcept {
        if (std::isnan(sample)) return 0;
        if (sample >= 1.0f)
            return std::numeric_limits<std::int16_t>::max();
        if (sample <= -1.0f)
            return std::numeric_limits<std::int16_t>::min();

        return static_cast<std::int16_t>(sample * 32768.0f);
    }

    float Int16ToFloat(std::int16_t sample) noexcept {
        return static_cast<float>(sample) / 32768.0f;
    }
}

// C++14 needs this definition if the constant is ever bound to a reference
// (for example by std::min); C++17 makes static constexpr members inline.
constexpr std::size_t clouds_engine::BlockAdapter::kLatencyFrames;

clouds_engine::BlockAdapter::BlockAdapter(Engine &engine, ServicePolicy servicePolicy) noexcept
: engine_(engine), servicePolicy_(servicePolicy) {
}

void clouds_engine::BlockAdapter::Process(const float *inputLeft, const float *inputRight, float *outputLeft,
    float *outputRight, std::size_t frameCount, const ControlSnapshot &controls, BlockEvents events) noexcept {

    if (frameCount == 0) return;
    assert(engine_.IsInitialized());
    assert(inputLeft);
    assert(inputRight);
    assert(outputLeft);
    assert(outputRight);

    pendingTrigger_ |= events.trigger;

    for (std::size_t i = 0; i < frameCount; i++) {
        const auto inL = inputLeft[i];
        const auto inR = inputRight[i];

        const StereoFrame output = outputBlock_[frameIndex_];
        outputLeft[i] = Int16ToFloat(output.left);
        outputRight[i] = Int16ToFloat(output.right);

        inputBlock_[frameIndex_].left = FloatToInt16(inL);
        inputBlock_[frameIndex_].right = FloatToInt16(inR);
        frameIndex_++;

        if (frameIndex_ == kBlockFrames) {
            engine_.ProcessBlock(inputBlock_, outputBlock_, controls, BlockEvents{pendingTrigger_});
            if (servicePolicy_ == ServicePolicy::AfterEachBlock) engine_.Service();
            pendingTrigger_ = false;
            frameIndex_ = 0;
        }
    }
}

void clouds_engine::BlockAdapter::ResetStreamState() noexcept {
    inputBlock_.fill(StereoFrame{});
    outputBlock_.fill(StereoFrame{});
    frameIndex_ = 0;
    pendingTrigger_ = false;
}
