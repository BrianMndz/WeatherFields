#pragma once

#include <cstddef>

#include "wf/clouds/Engine.h"

namespace clouds_engine {

    class BlockAdapter {
    public:
        static constexpr std::size_t kLatencyFrames = kBlockFrames;

        // Who calls Engine::Service(). AfterEachBlock calls it after every
        // engine block, on the thread that calls Process(), as the stretch
        // and spectral modes need. Manual leaves it to the host, for targets
        // whose audio deadline cannot absorb a spectral hop.
        enum class ServicePolicy {
            AfterEachBlock,
            Manual,
        };

        explicit BlockAdapter(Engine& engine,
                              ServicePolicy servicePolicy = ServicePolicy::AfterEachBlock) noexcept;

        void Process(const float* inputLeft, const float* inputRight,
                     float* outputLeft, float* outputRight,
                     std::size_t frameCount, const ControlSnapshot& controls,
                     BlockEvents events) noexcept;

        void ResetStreamState() noexcept;

        BlockAdapter(const BlockAdapter&) = delete;
        BlockAdapter& operator=(const BlockAdapter&) = delete;

        BlockAdapter(BlockAdapter&&) = delete;
        BlockAdapter& operator=(BlockAdapter&&) = delete;

    private:
        Engine& engine_;
        ServicePolicy servicePolicy_;

        AudioBlock inputBlock_{};
        AudioBlock outputBlock_{};

        std::size_t frameIndex_ = 0;
        bool pendingTrigger_ = false;
    };

} // namespace clouds_engine
