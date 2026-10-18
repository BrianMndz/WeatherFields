#include "wf/clouds/Engine.h"

#include <cmath>
#include <cstring>
#include <new>

namespace {

using wf::ParamKind;
using wf::ParamSpec;
using wf::clouds::Engine;

const ParamSpec kSpecs[] = {
    {Engine::kPosition, "Position", ParamKind::Continuous, 0.0f, 1.0f, 0.5f, ""},
    {Engine::kSize, "Size", ParamKind::Continuous, 0.0f, 1.0f, 0.5f, ""},
    {Engine::kPitch, "Pitch", ParamKind::Continuous, -24.0f, 24.0f, 0.0f, "st"},
    {Engine::kDensity, "Density", ParamKind::Continuous, 0.0f, 1.0f, 0.75f, ""},
    {Engine::kTexture, "Texture", ParamKind::Continuous, 0.0f, 1.0f, 0.5f, ""},
    {Engine::kDryWet, "Dry/Wet", ParamKind::Continuous, 0.0f, 1.0f, 0.5f, ""},
    {Engine::kStereoSpread, "Spread", ParamKind::Continuous, 0.0f, 1.0f, 0.0f, ""},
    {Engine::kFeedback, "Feedback", ParamKind::Continuous, 0.0f, 1.0f, 0.0f, ""},
    {Engine::kReverb, "Reverb", ParamKind::Continuous, 0.0f, 1.0f, 0.0f, ""},
    {Engine::kInputGain, "In Gain", ParamKind::Continuous, -18.0f, 6.0f, 0.0f, "dB"},
    {Engine::kFreeze, "Freeze", ParamKind::Toggle, 0.0f, 1.0f, 0.0f, ""},
    {Engine::kTrigger, "Trig", ParamKind::Trigger, 0.0f, 1.0f, 0.0f, ""},
    {Engine::kGate, "Gate", ParamKind::Toggle, 0.0f, 1.0f, 0.0f, ""},
    {Engine::kQuality, "Quality", ParamKind::Choice, 0.0f, 3.0f, 0.0f, ""},
    {Engine::kPlaybackMode, "Mode", ParamKind::Choice, 0.0f, 3.0f, 0.0f, ""},
};

const wf::ParamTable kTable = wf::MakeParamTable(kSpecs);

}  // namespace

// C++14 needs these definitions if the constants are ever bound to a
// reference; C++17 makes static constexpr members inline.
constexpr std::uint32_t wf::clouds::Engine::kRandomSeed;
constexpr std::size_t wf::clouds::Engine::kMemoryBytes;

const wf::ParamTable& wf::clouds::Engine::Params() const noexcept {
    return kTable;
}

std::size_t wf::clouds::Engine::MemoryBytes(float, std::size_t) const noexcept {
    return kMemoryBytes;
}

wf::PrepareStatus wf::clouds::Engine::Prepare(float sampleRate, std::size_t maxFrames,
                                              MemoryBlock memory) noexcept {
    prepared_ = false;

    const PrepareStatus memoryStatus = CheckMemory(memory, kMemoryBytes);
    if (memoryStatus != PrepareStatus::Ok) return memoryStatus;
    if (maxFrames == 0) return PrepareStatus::UnsupportedBlockSize;
    if (!IsFinite(sampleRate) || sampleRate <= 0.0f) return PrepareStatus::UnsupportedSampleRate;
    const auto rate = static_cast<std::uint32_t>(std::lround(sampleRate));
    if (rate != kEngineSampleRateHz && rate != 44100u && rate != 48000u) {
        return PrepareStatus::UnsupportedSampleRate;
    }

    // A Module initializes once, so a second Prepare() starts a fresh one in
    // the same place; the adapters keep referring to it.
    if (module_.IsInitialized()) {
        module_.~Module();
        new (&module_) Module();
    }

    unsigned char* const base = static_cast<unsigned char*>(memory.data);
    const EngineMemory arenas{
        MemoryRegion{base, kLargeMemoryBytes},
        MemoryRegion{base + AlignMemory(kLargeMemoryBytes), kSmallMemoryBytes},
    };
    // Every condition Initialize() checks is met by the memory checked above,
    // so only a fault in that check could make it fail.
    if (module_.Initialize(arenas, Quality::Stereo16Bit32k, kRandomSeed) != InitStatus::Ok) {
        return PrepareStatus::MemoryMisaligned;
    }
    if (sampleRateAdapter_.Configure(rate) != SampleRateStatus::Ok) {
        return PrepareStatus::UnsupportedSampleRate;
    }

    // The defaults go through the same path as events, so the table and the
    // state cannot disagree. Quality and mode are already at theirs.
    controls_ = ControlSnapshot{};
    for (const ParamSpec& spec : kSpecs) {
        if (spec.kind != ParamKind::Trigger) SetParam(spec.id, spec.def);
    }
    appliedInputGain_ = targetInputGain_;
    pendingTrigger_ = false;
    prepared_ = true;
    return PrepareStatus::Ok;
}

void wf::clouds::Engine::Handle(const ControlEvent& event) noexcept {
    if (!prepared_) return;
    switch (event.type) {
        case ControlEvent::Type::Param:
            SetParam(event.id, event.value);
            break;
        case ControlEvent::Type::Trigger:
            // Held until an engine block consumes it, however the callbacks
            // split the blocks.
            if (event.id == kTrigger) pendingTrigger_ = true;
            break;
        case ControlEvent::Type::NoteOn:
        case ControlEvent::Type::NoteOff:
            break;
    }
}

void wf::clouds::Engine::SetParam(ParamId id, float value) noexcept {
    switch (id) {
        case kPosition: controls_.position = value; break;
        case kSize: controls_.size = value; break;
        case kPitch: controls_.pitch = value; break;
        case kDensity: controls_.density = value; break;
        case kTexture: controls_.texture = value; break;
        case kDryWet: controls_.dryWet = value; break;
        case kStereoSpread: controls_.stereoSpread = value; break;
        case kFeedback: controls_.feedback = value; break;
        case kReverb: controls_.reverb = value; break;
        case kInputGain:
            inputGainDecibels_ = value;
            targetInputGain_ = std::pow(10.0f, value / 20.0f);
            break;
        case kFreeze: controls_.freeze = value != 0.0f; break;
        case kGate: controls_.gate = value != 0.0f; break;
        case kQuality: {
            const auto quality = static_cast<Quality>(static_cast<int>(value));
            if (quality == module_.GetQuality()) break;
            if (module_.SetQuality(quality) != QualityStatus::Ok) break;
            // The Module releases freeze with the erased recording; so does
            // the snapshot, or the next block would freeze it again.
            controls_.freeze = false;
            module_.Service();
            break;
        }
        case kPlaybackMode: {
            const auto mode = static_cast<PlaybackMode>(static_cast<int>(value));
            const PlaybackMode current = module_.GetPlaybackMode();
            if (mode == current) break;
            if (module_.SetPlaybackMode(mode) != PlaybackModeStatus::Ok) break;
            if (mode == PlaybackMode::Spectral || current == PlaybackMode::Spectral) {
                controls_.freeze = false;
            }
            module_.Service();
            break;
        }
        default:
            break;
    }
}

void wf::clouds::Engine::Process(const float* inLeft, const float* inRight,
                                 float* outLeft, float* outRight,
                                 std::size_t frames) noexcept {
    if (frames == 0) return;
    if (!prepared_) {
        std::memset(outLeft, 0, frames * sizeof(float));
        std::memset(outRight, 0, frames * sizeof(float));
        return;
    }

    // IN GAIN into the output buffers, which the adapters then process in
    // place, as on Bela.
    ApplyGainRamp(inLeft, outLeft, frames, appliedInputGain_, targetInputGain_);
    ApplyGainRamp(inRight, outRight, frames, appliedInputGain_, targetInputGain_);
    appliedInputGain_ = targetInputGain_;

    sampleRateAdapter_.Process(outLeft, outRight, outLeft, outRight, frames,
                               controls_, BlockEvents{pendingTrigger_});
    pendingTrigger_ = false;
}
