#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "wf/clouds/BlockAdapter.h"

namespace clouds_engine {

    enum class SampleRateStatus {
        Ok,
        UnsupportedExternalSampleRate,
    };

    class SampleRateAdapter {
    public:
        explicit SampleRateAdapter(BlockAdapter& blockAdapter) noexcept;

        CLOUDS_ENGINE_NODISCARD SampleRateStatus Configure(std::uint32_t externalSampleRate) noexcept;

        void Process(const float* inputLeft, const float* inputRight,
                     float* outputLeft, float* outputRight,
                     std::size_t frameCount, const ControlSnapshot& controls,
                     BlockEvents events) noexcept;

        CLOUDS_ENGINE_NODISCARD bool IsConfigured() const noexcept;

        CLOUDS_ENGINE_NODISCARD std::uint32_t ExternalSampleRateHz() const noexcept;

        SampleRateAdapter(const SampleRateAdapter&) = delete;
        SampleRateAdapter& operator=(const SampleRateAdapter&) = delete;

        SampleRateAdapter(SampleRateAdapter&&) = delete;
        SampleRateAdapter& operator=(SampleRateAdapter&&) = delete;

    private:
        BlockAdapter& blockAdapter_;
        std::uint32_t externalSampleRate_ = 0;
        bool configured_ = false;

        struct FloatStereoFrame {
            float left = 0.0f;
            float right = 0.0f;
        };

        class LinearResampler {
        public:
            void Configure(std::uint32_t inputRate, std::uint32_t outputRate) noexcept;

            CLOUDS_ENGINE_NODISCARD std::size_t Push(FloatStereoFrame inputFrame,
                                std::array<FloatStereoFrame, 2>& outputFrames) noexcept;

        private:
            FloatStereoFrame previousFrame_{};
            bool hasPrevious_ = false;
            std::uint32_t inputRate_ = 0;
            std::uint32_t outputRate_ = 0;
            std::uint64_t currentInputIndex_ = 0;
            std::uint64_t nextOutputTimeNumerator_ = 0;
        };

        LinearResampler inputResampler_;
        LinearResampler outputResampler_;

        void ClearOutputQueue() noexcept;
        void EnqueueOutput(FloatStereoFrame frame) noexcept;

        CLOUDS_ENGINE_NODISCARD FloatStereoFrame DequeueOutputOrSilence() noexcept;

        static constexpr std::size_t kOutputQueueCapacity = 4;

        std::array<FloatStereoFrame, kOutputQueueCapacity> outputQueue_{};
        std::size_t outputReadIndex_ = 0;
        std::size_t outputWriteIndex_ = 0;
        std::size_t outputCount_ = 0;

        bool pendingTrigger_ = false;
    };

}  // namespace clouds_engine



