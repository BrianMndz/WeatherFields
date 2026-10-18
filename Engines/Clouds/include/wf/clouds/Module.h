#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "clouds/dsp/frame.h"
#include "clouds/dsp/granular_processor.h"

// The engine is C++14 so the Bela board's Clang 3.9 can build it, while its
// headers must also compile in C++17 consumers. C++17 code gets the standard
// attribute; C++14 code on GCC and Clang (including Bela) gets the equivalent
// extension, so ignored status values still produce a warning there.
#if __cplusplus >= 201703L
#define WF_CLOUDS_NODISCARD [[nodiscard]]
#elif defined(__GNUC__)
#define WF_CLOUDS_NODISCARD __attribute__((warn_unused_result))
#else
#define WF_CLOUDS_NODISCARD
#endif

namespace wf {
namespace clouds {

    // Namespace-scope constexpr variables have internal linkage in C++14, so
    // each translation unit gets its own copy of these values.
    constexpr std::size_t kBlockFrames = 32;
    constexpr std::size_t kLargeMemoryBytes = 118784;
    constexpr std::size_t kSmallMemoryBytes = 65408;
    constexpr std::size_t kMemoryAlignment = 4;
    constexpr std::uint32_t kEngineSampleRateHz = 32000;

    struct StereoFrame {
        std::int16_t left = 0;
        std::int16_t right = 0;
    };

    using AudioBlock = std::array<StereoFrame, kBlockFrames>;

    struct MemoryRegion {
        void* data = nullptr;
        std::size_t sizeBytes = 0;
    };

    struct EngineMemory {
        MemoryRegion large;
        MemoryRegion small;
    };

    struct ControlSnapshot {
        float position = 0.5f;
        float size = 0.5f;
        float pitch = 0.0f;
        float density = 0.75f;
        float texture = 0.5f;
        float dryWet = 0.0f;
        float stereoSpread = 0.0f;
        float feedback = 0.0f;
        float reverb = 0.0f;

        // Persistent: they describe a state that holds until changed.
        bool freeze = false;
        bool gate = false;
    };

    // A trigger is a rising edge of the TRIG input. As on the module, where
    // an edge is only seen while the input is high, the block that consumes
    // a trigger also sees the gate, even if the host's gate is already low.
    struct BlockEvents {
        bool trigger = false;
    };

    enum class InitStatus {
        Ok,
        LargeRegionNull,
        LargeRegionIncorrectSize,
        SmallRegionNull,
        SmallRegionIncorrectSize,
        RegionsOverlap,
        RegionMisaligned,
        AlreadyInitialized,
        InvalidQuality
    };

    // In the module's order: the quality LEDs and its firmware's
    // set_quality() index both follow it.
    enum class Quality {
        Stereo16Bit32k,
        Mono16Bit32k,
        StereoMuLaw16k,
        MonoMuLaw16k,
    };

    enum class QualityStatus {
        Ok,
        NotInitialized,
        InvalidQuality
    };

    // In the module's order: its mode LEDs and the firmware's
    // PlaybackMode index both follow it.
    enum class PlaybackMode {
        Granular,
        Stretch,
        LoopingDelay,
        Spectral,
    };

    enum class PlaybackModeStatus {
        Ok,
        NotInitialized,
        InvalidPlaybackMode
    };

    class Module {
    public:
        Module() = default;

        WF_CLOUDS_NODISCARD InitStatus Initialize(EngineMemory engineMemory,
                                                      Quality quality,
                                                      std::uint32_t randomSeed) noexcept;

        void ProcessBlock(const AudioBlock& in, AudioBlock& out,
                          const ControlSnapshot& controls,
                          BlockEvents events) noexcept;

        // Prepares buffers after a change, such as a new quality or playback
        // mode, and performs the background work of the stretch and spectral
        // modes: a slice of the correlator search, or one spectral hop (a
        // 4096-point FFT pair every 1024 frames). On the module this runs in
        // the main loop; a host without one calls it on the audio thread
        // after every ProcessBlock(), never concurrently with it. In spectral
        // mode, falling more than about two hops behind glitches the output.
        void Service() noexcept;

        // Requests another recording quality. Until the next Service() call
        // the engine outputs silence; that call then erases the recording
        // buffer and releases freeze, as on the module. A host that sends
        // freeze in every ControlSnapshot should release it too.
        WF_CLOUDS_NODISCARD QualityStatus SetQuality(Quality quality) noexcept;
        WF_CLOUDS_NODISCARD Quality GetQuality() const noexcept;

        // Requests another playback mode. Until the next Service() call the
        // engine outputs silence. Switches among granular, stretch and
        // looping delay keep the recording; a switch to or from spectral
        // erases it and releases freeze, as a quality change does.
        WF_CLOUDS_NODISCARD PlaybackModeStatus SetPlaybackMode(PlaybackMode mode) noexcept;
        WF_CLOUDS_NODISCARD PlaybackMode GetPlaybackMode() const noexcept;

        WF_CLOUDS_NODISCARD bool IsInitialized() const noexcept;

        Module(const Module&) = delete;
        Module& operator=(const Module&) = delete;

        Module(Module&&) = delete;
        Module& operator=(Module&&) = delete;

    private:
        ::clouds::GranularProcessor processor_;
        bool initialized_ = false;
        Quality quality_ = Quality::Stereo16Bit32k;
        PlaybackMode playbackMode_ = PlaybackMode::Granular;

        std::array<::clouds::ShortFrame, kBlockFrames> inputFrames_{};
        std::array<::clouds::ShortFrame, kBlockFrames> outputFrames_{};
    };

}  // namespace clouds
}  // namespace wf
