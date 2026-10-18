#include "wf/clouds/Module.h"

#include <cassert>

#include "stmlib/utils/random.h"

namespace {
    struct QualitySettings {
        int numChannels;
        bool lowFidelity;
    };

    // Returns false for a value outside the enumeration.
    bool ToSettings(wf::clouds::Quality quality, QualitySettings& settings) noexcept {
        using wf::clouds::Quality;
        switch (quality) {
            case Quality::Stereo16Bit32k: settings = {2, false}; return true;
            case Quality::Mono16Bit32k: settings = {1, false}; return true;
            case Quality::StereoMuLaw16k: settings = {2, true}; return true;
            case Quality::MonoMuLaw16k: settings = {1, true}; return true;
        }
        return false;
    }

    // Returns false for a value outside the enumeration.
    bool ToProcessorMode(wf::clouds::PlaybackMode mode,
                         ::clouds::PlaybackMode& processorMode) noexcept {
        using wf::clouds::PlaybackMode;
        switch (mode) {
            case PlaybackMode::Granular: processorMode = ::clouds::PLAYBACK_MODE_GRANULAR; return true;
            case PlaybackMode::Stretch: processorMode = ::clouds::PLAYBACK_MODE_STRETCH; return true;
            case PlaybackMode::LoopingDelay: processorMode = ::clouds::PLAYBACK_MODE_LOOPING_DELAY; return true;
            case PlaybackMode::Spectral: processorMode = ::clouds::PLAYBACK_MODE_SPECTRAL; return true;
        }
        return false;
    }
}

wf::clouds::InitStatus wf::clouds::Module::Initialize(
    EngineMemory engineMemory, Quality quality, std::uint32_t randomSeed) noexcept {
    // Perform the status check in order
    if (initialized_) return InitStatus::AlreadyInitialized;

    if (engineMemory.large.data == nullptr) return InitStatus::LargeRegionNull;
    if (engineMemory.large.sizeBytes != kLargeMemoryBytes) return InitStatus::LargeRegionIncorrectSize;

    if (engineMemory.small.data == nullptr) return InitStatus::SmallRegionNull;
    if (engineMemory.small.sizeBytes != kSmallMemoryBytes) return InitStatus::SmallRegionIncorrectSize;

    // Convert each non-null pointer
    const auto largeAddress = reinterpret_cast<std::uintptr_t>(engineMemory.large.data);
    const auto smallAddress = reinterpret_cast<std::uintptr_t>(engineMemory.small.data);

    if (largeAddress % kMemoryAlignment != 0 || smallAddress % kMemoryAlignment != 0)
        return InitStatus::RegionMisaligned;

    // calculate each end address
    const auto largeEnd = largeAddress + engineMemory.large.sizeBytes;
    const auto smallEnd = smallAddress + engineMemory.small.sizeBytes;

    if (largeAddress < smallEnd && smallAddress < largeEnd)
        return InitStatus::RegionsOverlap;

    QualitySettings settings{};
    if (!ToSettings(quality, settings)) return InitStatus::InvalidQuality;

    stmlib::Random::Seed(randomSeed);

    // call processor init
    processor_.Init(engineMemory.large.data, engineMemory.large.sizeBytes,
                    engineMemory.small.data, engineMemory.small.sizeBytes);

    processor_.set_playback_mode(::clouds::PLAYBACK_MODE_GRANULAR);
    processor_.set_num_channels(settings.numChannels);
    processor_.set_low_fidelity(settings.lowFidelity);
    quality_ = quality;
    playbackMode_ = PlaybackMode::Granular;

    // call first prepare
    processor_.Prepare();

    // set init to true
    initialized_ = true;

    return InitStatus::Ok;
}

bool wf::clouds::Module::IsInitialized() const noexcept {
    return initialized_;
}

void wf::clouds::Module::Service() noexcept {
    if (!initialized_) {
        assert(initialized_ && "Module::Service called before initialization");
        return;
    }

    processor_.Prepare();
}

wf::clouds::QualityStatus wf::clouds::Module::SetQuality(Quality quality) noexcept {
    if (!initialized_) return QualityStatus::NotInitialized;

    QualitySettings settings{};
    if (!ToSettings(quality, settings)) return QualityStatus::InvalidQuality;

    // Each setter marks the buffers for reallocation only if its value
    // changes, so requesting the current quality erases nothing.
    processor_.set_num_channels(settings.numChannels);
    processor_.set_low_fidelity(settings.lowFidelity);
    quality_ = quality;
    return QualityStatus::Ok;
}

wf::clouds::Quality wf::clouds::Module::GetQuality() const noexcept {
    return quality_;
}

wf::clouds::PlaybackModeStatus wf::clouds::Module::SetPlaybackMode(PlaybackMode mode) noexcept {
    if (!initialized_) return PlaybackModeStatus::NotInitialized;

    ::clouds::PlaybackMode processorMode{};
    if (!ToProcessorMode(mode, processorMode)) return PlaybackModeStatus::InvalidPlaybackMode;

    // Prepare() compares the mode with the one it last prepared, so
    // requesting the current mode changes nothing.
    processor_.set_playback_mode(processorMode);
    playbackMode_ = mode;
    return PlaybackModeStatus::Ok;
}

wf::clouds::PlaybackMode wf::clouds::Module::GetPlaybackMode() const noexcept {
    return playbackMode_;
}

void wf::clouds::Module::ProcessBlock(const AudioBlock& in,
                                         AudioBlock& out,
                                         const ControlSnapshot& controls,
                                         BlockEvents events) noexcept {
    if (!initialized_) {
        out.fill(StereoFrame{});
        assert(initialized_ && "Module::ProcessBlock called before initialization");
        return;
    }

    auto* parameters = processor_.mutable_parameters();
    parameters->position = controls.position;
    parameters->size = controls.size;
    parameters->pitch = controls.pitch;
    parameters->density = controls.density;
    parameters->texture = controls.texture;
    parameters->dry_wet = controls.dryWet;
    parameters->stereo_spread = controls.stereoSpread;
    parameters->feedback = controls.feedback;
    parameters->reverb = controls.reverb;
    parameters->freeze = controls.freeze;
    parameters->gate = controls.gate || events.trigger;
    parameters->trigger = events.trigger;

    for (std::size_t i = 0; i < kBlockFrames; ++i) {
        inputFrames_[i].l = static_cast<short>(in[i].left);
        inputFrames_[i].r = static_cast<short>(in[i].right);
    }

    processor_.Process(inputFrames_.data(), outputFrames_.data(), kBlockFrames);

    parameters->trigger = false;

    for (std::size_t i = 0; i < kBlockFrames; ++i) {
        out[i].left = static_cast<std::int16_t>(outputFrames_[i].l);
        out[i].right = static_cast<std::int16_t>(outputFrames_[i].r);
    }
}
