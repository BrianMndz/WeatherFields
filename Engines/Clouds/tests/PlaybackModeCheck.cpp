#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "wf/clouds/Module.h"

namespace {

using wf::clouds::AudioBlock;
using wf::clouds::BlockEvents;
using wf::clouds::ControlSnapshot;
using wf::clouds::Module;
using wf::clouds::EngineMemory;
using wf::clouds::InitStatus;
using wf::clouds::MemoryRegion;
using wf::clouds::PlaybackMode;
using wf::clouds::PlaybackModeStatus;
using wf::clouds::Quality;
using wf::clouds::QualityStatus;
using wf::clouds::kLargeMemoryBytes;
using wf::clouds::kMemoryAlignment;
using wf::clouds::kSmallMemoryBytes;

constexpr std::uint32_t kTestSeed = 0x23u;

// Spectral output starts after a 4096-sample analysis window, 128 blocks.
constexpr int kBlocks = 256;

// The test tone peaks at 8000; every mode's wet output reaches about 3000.
// A broken mode can still leak a few LSBs through its effects.
constexpr int kAudible = 1000;

alignas(kMemoryAlignment) unsigned char largeArena[kLargeMemoryBytes];
alignas(kMemoryAlignment) unsigned char smallArena[kSmallMemoryBytes];

EngineMemory Memory() {
    return {
        MemoryRegion{largeArena, sizeof(largeArena)},
        MemoryRegion{smallArena, sizeof(smallArena)},
    };
}

const char* StatusName(PlaybackModeStatus status) {
    switch (status) {
        case PlaybackModeStatus::Ok: return "Ok";
        case PlaybackModeStatus::NotInitialized: return "NotInitialized";
        case PlaybackModeStatus::InvalidPlaybackMode: return "InvalidPlaybackMode";
    }
    return "Unknown";
}

const char* ModeName(PlaybackMode mode) {
    switch (mode) {
        case PlaybackMode::Granular: return "Granular";
        case PlaybackMode::Stretch: return "Stretch";
        case PlaybackMode::LoopingDelay: return "LoopingDelay";
        case PlaybackMode::Spectral: return "Spectral";
    }
    return "Unknown";
}

bool Check(const char* scenario, bool condition) {
    if (!condition) std::cerr << scenario << ": failed\n";
    return condition;
}

bool CheckStatus(const char* scenario, PlaybackModeStatus actual, PlaybackModeStatus expected) {
    if (actual == expected) return true;
    std::cerr << scenario << ": expected " << StatusName(expected)
              << ", got " << StatusName(actual) << '\n';
    return false;
}

struct Render {
    bool tone = true;      // a 1 kHz tone, otherwise silence
    bool freeze = false;
    bool service = true;   // Service() after every block, as hosts do
};

// Processes kBlocks fully wet blocks and returns the largest output sample
// magnitude. POSITION and SIZE read the newest audio: at their defaults,
// grains trail the write head by half a buffer plus their own length, so
// granular mode stays silent for up to 2 s after an erasing switch.
int PeakOutput(Module& engine, Render render) {
    ControlSnapshot controls;
    controls.dryWet = 1.0f;
    controls.position = 0.0f;
    controls.size = 0.0f;
    controls.freeze = render.freeze;
    AudioBlock in{};
    AudioBlock out{};
    int peak = 0;
    for (int block = 0; block < kBlocks; ++block) {
        for (std::size_t i = 0; i < in.size(); ++i) {
            const auto n = static_cast<double>(block * in.size() + i);
            const auto sample = render.tone
                ? static_cast<std::int16_t>(8000.0 * std::sin(n * 2.0 * 3.14159265 / 32.0))
                : std::int16_t{0};
            in[i].left = sample;
            in[i].right = sample;
        }
        engine.ProcessBlock(in, out, controls, BlockEvents{});
        if (render.service) engine.Service();
        for (const auto& frame : out) {
            peak = std::max(peak, std::abs(static_cast<int>(frame.left)));
            peak = std::max(peak, std::abs(static_cast<int>(frame.right)));
        }
    }
    return peak;
}

int PeakWithoutService(Module& engine) {
    Render render;
    render.service = false;
    return PeakOutput(engine, render);
}

bool Switch(Module& engine, PlaybackMode mode) {
    const bool ok = engine.SetPlaybackMode(mode) == PlaybackModeStatus::Ok;
    engine.Service();
    return ok;
}

// Records the tone in granular mode, switches to `via` and then to `to`, and
// returns the peak of the frozen recording played with silent input.
int FrozenPeakAfter(Module& engine, PlaybackMode via, PlaybackMode to) {
    bool ok = Switch(engine, PlaybackMode::Granular);
    PeakOutput(engine, Render{});
    ok = Switch(engine, via) && ok;
    ok = Switch(engine, to) && ok;
    if (!ok) std::cerr << "Switching modes failed\n";

    Render frozen;
    frozen.tone = false;
    frozen.freeze = true;
    return PeakOutput(engine, frozen);
}

}  // namespace

int main() {
    int failures = 0;

    {
        Module engine;
        failures += !CheckStatus("Before initialization",
                                 engine.SetPlaybackMode(PlaybackMode::Stretch),
                                 PlaybackModeStatus::NotInitialized);
    }

    Module engine;
    if (engine.Initialize(Memory(), Quality::Stereo16Bit32k, kTestSeed) != InitStatus::Ok) {
        std::cerr << "Initialization failed\n";
        return 1;
    }
    failures += !Check("Initial mode", engine.GetPlaybackMode() == PlaybackMode::Granular);
    failures += !Check("Audio after initialization", PeakOutput(engine, Render{}) >= kAudible);

    failures += !CheckStatus("Invalid mode",
                             engine.SetPlaybackMode(static_cast<PlaybackMode>(999)),
                             PlaybackModeStatus::InvalidPlaybackMode);
    failures += !Check("Invalid mode keeps the current one",
                       engine.GetPlaybackMode() == PlaybackMode::Granular);
    failures += !Check("Invalid mode keeps audio", PeakWithoutService(engine) >= kAudible);

    failures += !CheckStatus("Same mode", engine.SetPlaybackMode(PlaybackMode::Granular),
                             PlaybackModeStatus::Ok);
    failures += !Check("Same mode needs no reset", PeakWithoutService(engine) >= kAudible);

    // Every quality and mode combination: silent until Service(), audible
    // after it.
    for (const auto quality : {Quality::Stereo16Bit32k, Quality::Mono16Bit32k,
                               Quality::StereoMuLaw16k, Quality::MonoMuLaw16k}) {
        if (engine.SetQuality(quality) != QualityStatus::Ok) {
            std::cerr << "SetQuality failed\n";
            ++failures;
        }
        for (const auto mode : {PlaybackMode::Stretch, PlaybackMode::LoopingDelay,
                                PlaybackMode::Spectral, PlaybackMode::Granular}) {
            failures += !CheckStatus(ModeName(mode), engine.SetPlaybackMode(mode),
                                     PlaybackModeStatus::Ok);
            if (engine.GetPlaybackMode() != mode || engine.GetQuality() != quality) {
                std::cerr << ModeName(mode) << ": getters disagree\n";
                ++failures;
            }
            if (PeakWithoutService(engine) != 0) {
                std::cerr << ModeName(mode) << ": audio before Service()\n";
                ++failures;
            }
            engine.Service();
            if (PeakOutput(engine, Render{}) < kAudible) {
                std::cerr << ModeName(mode) << ": silent after Service()\n";
                ++failures;
            }
        }
    }
    if (engine.SetQuality(Quality::Stereo16Bit32k) != QualityStatus::Ok) ++failures;
    engine.Service();

    // Benign switches keep the recording; a pass through spectral erases it.
    failures += !Check("Granular to looping delay keeps the recording",
                       FrozenPeakAfter(engine, PlaybackMode::LoopingDelay,
                                       PlaybackMode::LoopingDelay) >= kAudible);
    failures += !Check("Through stretch keeps the recording",
                       FrozenPeakAfter(engine, PlaybackMode::Stretch,
                                       PlaybackMode::Granular) >= kAudible);
    failures += !Check("Through spectral erases the recording",
                       FrozenPeakAfter(engine, PlaybackMode::Spectral,
                                       PlaybackMode::Granular) == 0);

    return failures == 0 ? 0 : 1;
}
