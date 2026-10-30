#pragma once

#include <cstddef>
#include <cstdint>

#include "wf/Engine.h"

namespace wf {
namespace stratus {

class Engine final : public wf::Engine {
public:
    enum Param : ParamId { kPlay = 0, kRecord = 1, kClear = 2, kLoopLevel = 3, kThru = 4 };

    Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    const char* Name() const noexcept override { return "Stratus"; }
    const ParamTable& Params() const noexcept override;
    std::size_t MemoryBytes(float sampleRate, std::size_t maxFrames) const noexcept override;

    PrepareStatus Prepare(float sampleRate, std::size_t maxFrames,
                          MemoryBlock memory) noexcept override;

    void Handle(const ControlEvent& event) noexcept override;

    float ParamValue(ParamId id) const noexcept override;

    void Process(const float* inLeft, const float* inRight, float* outLeft, float* outRight,
                 std::size_t frames) noexcept override;

private:
    enum class State : std::uint8_t {
        kEmpty, kRecording,
        kPlaying, kOverdubbing, kStopped,
    };

    void OnRecord(bool isRecOn) noexcept;
    void OnPlay(bool isPlayOn) noexcept;
    void OnClear() noexcept;
    void FinishFirstTake(State nextState) noexcept;
    float AdvanceRamp(std::size_t& position, bool on) noexcept;
    // Panel readback only: Recording has Play on before a loop exists.
    bool PlayOn() const noexcept;
    bool RecordOn() const noexcept;

    float* left_ = nullptr;
    float* right_ = nullptr;
    std::size_t capacity_ = 0;   // frames each channel holds
    bool prepared_ = false;

    State state_ = State::kEmpty;
    std::size_t length_ = 0;
    std::size_t position_ = 0;

    float loopLevelDb_ = 0.0f;
    float loopGain_ = 1.0f;
    bool thru_ = true;
    // Gain position in a 5 ms ramp: integer steps give exact endpoints and
    // let a mid-ramp toggle reverse without restarting the fade.
    std::size_t fadeFrames_ = 1;
    std::size_t dryFadePosition_ = 1;
    std::size_t inputFadePosition_ = 0;
    float fadeStep_ = 1.0f;
};

}  // namespace stratus
}  // namespace wf
