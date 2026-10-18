#include "wf/clouds/SampleRateAdapter.h"

#include <cassert>

// C++14 needs this definition if the constant is ever bound to a reference
// (for example by std::min); C++17 makes static constexpr members inline.
constexpr std::size_t wf::clouds::SampleRateAdapter::kOutputQueueCapacity;

wf::clouds::SampleRateAdapter::SampleRateAdapter(BlockAdapter& blockAdapter) noexcept
    : blockAdapter_(blockAdapter) {
}

void wf::clouds::SampleRateAdapter::LinearResampler::Configure(
    std::uint32_t inputRate, std::uint32_t outputRate) noexcept {
    assert(inputRate != 0);
    assert(outputRate != 0);

    inputRate_ = inputRate;
    outputRate_ = outputRate;
    previousFrame_ = {};
    hasPrevious_ = false;
    currentInputIndex_ = 0;
    nextOutputTimeNumerator_ = 0;
}

std::size_t wf::clouds::SampleRateAdapter::LinearResampler::Push(
    FloatStereoFrame inputFrame,
    std::array<FloatStereoFrame, 2>& outputFrames) noexcept {
    if (!hasPrevious_) {
        previousFrame_ = inputFrame;
        hasPrevious_ = true;
        return 0;
    }

    ++currentInputIndex_;

    const std::uint64_t intervalStart =
        (currentInputIndex_ - 1) * static_cast<std::uint64_t>(outputRate_);
    const std::uint64_t intervalEnd =
        currentInputIndex_ * static_cast<std::uint64_t>(outputRate_);
    std::size_t produced = 0;

    while (nextOutputTimeNumerator_ <= intervalEnd) {
        assert(nextOutputTimeNumerator_ >= intervalStart);
        assert(produced < outputFrames.size());

        const float fraction = static_cast<float>(
            nextOutputTimeNumerator_ - intervalStart) /
            static_cast<float>(outputRate_);

        outputFrames[produced].left =
            previousFrame_.left + (inputFrame.left - previousFrame_.left) * fraction;
        outputFrames[produced].right =
            previousFrame_.right + (inputFrame.right - previousFrame_.right) * fraction;

        ++produced;
        nextOutputTimeNumerator_ += inputRate_;
    }

    previousFrame_ = inputFrame;
    return produced;
}

void wf::clouds::SampleRateAdapter::ClearOutputQueue() noexcept {
    outputQueue_ = {};
    outputReadIndex_ = 0;
    outputWriteIndex_ = 0;
    outputCount_ = 0;
}

void wf::clouds::SampleRateAdapter::EnqueueOutput(FloatStereoFrame frame) noexcept {
    assert(outputCount_ < kOutputQueueCapacity);
    if (outputCount_ >= kOutputQueueCapacity) return;

    outputQueue_[outputWriteIndex_] = frame;
    outputWriteIndex_ = (outputWriteIndex_ + 1) % kOutputQueueCapacity;
    ++outputCount_;
}

wf::clouds::SampleRateAdapter::FloatStereoFrame
wf::clouds::SampleRateAdapter::DequeueOutputOrSilence() noexcept {
    if (outputCount_ == 0) return {};

    const FloatStereoFrame frame = outputQueue_[outputReadIndex_];
    outputReadIndex_ = (outputReadIndex_ + 1) % kOutputQueueCapacity;
    --outputCount_;
    return frame;
}

wf::clouds::SampleRateStatus wf::clouds::SampleRateAdapter::Configure(
    std::uint32_t externalSampleRate) noexcept {
    configured_ = false;
    externalSampleRate_ = 0;
    blockAdapter_.ResetStreamState();

    ClearOutputQueue();
    pendingTrigger_ = false;

    if (externalSampleRate != kEngineSampleRateHz &&
        externalSampleRate != 44100u &&
        externalSampleRate != 48000u) {
        return SampleRateStatus::UnsupportedExternalSampleRate;
    }

    inputResampler_.Configure(externalSampleRate, kEngineSampleRateHz);
    outputResampler_.Configure(kEngineSampleRateHz, externalSampleRate);

    externalSampleRate_ = externalSampleRate;
    configured_ = true;
    return SampleRateStatus::Ok;
}

void wf::clouds::SampleRateAdapter::Process(
    const float* inputLeft, const float* inputRight,
    float* outputLeft, float* outputRight,
    std::size_t frameCount, const ControlSnapshot& controls,
    BlockEvents events) noexcept {
    if (frameCount == 0) return;

    assert(configured_);
    assert(inputLeft != nullptr);
    assert(inputRight != nullptr);
    assert(outputLeft != nullptr);
    assert(outputRight != nullptr);

    if (!configured_ || inputLeft == nullptr || inputRight == nullptr ||
        outputLeft == nullptr || outputRight == nullptr) {
        return;
    }

    if (externalSampleRate_ == kEngineSampleRateHz) {
        blockAdapter_.Process(inputLeft, inputRight, outputLeft, outputRight,
                              frameCount, controls, events);
        return;
    }

    pendingTrigger_ |= events.trigger;

    std::array<FloatStereoFrame, 2> engineInputFrames{};
    std::array<FloatStereoFrame, 2> externalOutputFrames{};

    for (std::size_t i = 0; i < frameCount; ++i) {
        const FloatStereoFrame inputFrame{inputLeft[i], inputRight[i]};

        const FloatStereoFrame outputFrame = DequeueOutputOrSilence();
        outputLeft[i] = outputFrame.left;
        outputRight[i] = outputFrame.right;

        const std::size_t engineInputCount =
            inputResampler_.Push(inputFrame, engineInputFrames);

        for (std::size_t engineInputIndex = 0;
             engineInputIndex < engineInputCount;
             ++engineInputIndex) {
            const FloatStereoFrame& engineInputFrame =
                engineInputFrames[engineInputIndex];
            FloatStereoFrame engineOutputFrame{};

            blockAdapter_.Process(
                &engineInputFrame.left, &engineInputFrame.right,
                &engineOutputFrame.left, &engineOutputFrame.right,
                1, controls, BlockEvents{pendingTrigger_});
            pendingTrigger_ = false;

            const std::size_t externalOutputCount =
                outputResampler_.Push(engineOutputFrame, externalOutputFrames);
            for (std::size_t externalOutputIndex = 0;
                 externalOutputIndex < externalOutputCount;
                 ++externalOutputIndex) {
                EnqueueOutput(externalOutputFrames[externalOutputIndex]);
            }
        }
    }
}

bool wf::clouds::SampleRateAdapter::IsConfigured() const noexcept {
    return configured_;
}

std::uint32_t wf::clouds::SampleRateAdapter::ExternalSampleRateHz() const noexcept {
    return externalSampleRate_;
}
