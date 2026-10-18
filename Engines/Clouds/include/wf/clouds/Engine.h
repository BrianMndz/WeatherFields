#pragma once

#include <cstddef>
#include <cstdint>

#include "wf/Engine.h"
#include "wf/clouds/BlockAdapter.h"
#include "wf/clouds/Module.h"
#include "wf/clouds/SampleRateAdapter.h"

namespace wf {
namespace clouds {

// Clouds as a wf::Engine: the Module behind the block and sample-rate
// adapters, so it runs at 32, 44.1 or 48 kHz with any callback size, plus
// the module's IN GAIN knob. Controls follow CloudsEngine's JUCE panel:
// its ranges and defaults (dry/wet 0.5 rather than the Module's dry 0),
// pitch ±24 st (the Module accepts ±48), IN GAIN -18..+6 dB ramped across
// each callback before the engine.
//
// Only one Engine may run at a time: Clouds' random generator
// (stmlib::Random) is process-global, and Prepare() reseeds it.
class Engine final : public wf::Engine {
public:
    // Stable forever (see wf/Params.h); the order of Params() is the panel's.
    enum Param : ParamId {
        kPosition = 0,
        kSize = 1,
        kPitch = 2,
        kDensity = 3,
        kTexture = 4,
        kDryWet = 5,
        kStereoSpread = 6,
        kFeedback = 7,
        kReverb = 8,
        kInputGain = 9,
        kFreeze = 10,
        kTrigger = 11,  // a rising edge of the TRIG input
        kGate = 12,     // the TRIG input's level
        kQuality = 13,  // Quality's order
        kPlaybackMode = 14,  // PlaybackMode's order
    };

    // The seed of CloudsEngine's render references.
    static constexpr std::uint32_t kRandomSeed = 0x23u;

    // The two Module arenas, carved from one block: large first.
    static constexpr std::size_t kMemoryBytes =
        AlignMemory(kLargeMemoryBytes) + AlignMemory(kSmallMemoryBytes);

    Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    const char* Name() const noexcept override { return "Clouds"; }
    const ParamTable& Params() const noexcept override;
    std::size_t MemoryBytes(float sampleRate, std::size_t maxFrames) const noexcept override;

    // Accepts 32, 44.1 and 48 kHz and any maxFrames above 0. Starts in
    // granular mode at stereo 16-bit with every parameter at its default.
    PrepareStatus Prepare(float sampleRate, std::size_t maxFrames,
                          MemoryBlock memory) noexcept override;

    // A quality change, or a mode change to or from spectral, erases the
    // recording and releases freeze, as on the module; it takes effect at
    // once, in event order, with the Module's Service() on this thread.
    // Freeze turned on by a later event stays on.
    void Handle(const ControlEvent& event) noexcept override;

    // Adds the adapters' latency: 32 engine frames, plus up to a few frames
    // of resampling at 44.1 and 48 kHz.
    void Process(const float* inLeft, const float* inRight,
                 float* outLeft, float* outRight, std::size_t frames) noexcept override;

    // The state the next Process() uses, for hosts showing it (freeze, for
    // example, can be released by a quality change) and for checks.
    const ControlSnapshot& Controls() const noexcept { return controls_; }
    float InputGainDecibels() const noexcept { return inputGainDecibels_; }
    Quality GetQuality() const noexcept { return module_.GetQuality(); }
    PlaybackMode GetPlaybackMode() const noexcept { return module_.GetPlaybackMode(); }
    bool IsPrepared() const noexcept { return prepared_; }

private:
    void SetParam(ParamId id, float value) noexcept;

    Module module_;
    BlockAdapter blockAdapter_{module_};
    SampleRateAdapter sampleRateAdapter_{blockAdapter_};

    ControlSnapshot controls_{};
    float inputGainDecibels_ = 0.0f;
    float targetInputGain_ = 1.0f;
    float appliedInputGain_ = 1.0f;  // where the last callback's ramp ended
    bool pendingTrigger_ = false;
    bool prepared_ = false;
};

}  // namespace clouds
}  // namespace wf
