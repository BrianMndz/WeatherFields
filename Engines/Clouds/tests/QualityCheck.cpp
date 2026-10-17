#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "wf/clouds/Engine.h"

namespace {

using clouds_engine::AudioBlock;
using clouds_engine::BlockEvents;
using clouds_engine::ControlSnapshot;
using clouds_engine::Engine;
using clouds_engine::EngineMemory;
using clouds_engine::InitStatus;
using clouds_engine::MemoryRegion;
using clouds_engine::Quality;
using clouds_engine::QualityStatus;
using clouds_engine::kLargeMemoryBytes;
using clouds_engine::kMemoryAlignment;
using clouds_engine::kSmallMemoryBytes;

constexpr std::uint32_t kTestSeed = 0x23u;
constexpr int kBlocks = 64;

alignas(kMemoryAlignment) unsigned char largeArena[kLargeMemoryBytes];
alignas(kMemoryAlignment) unsigned char smallArena[kSmallMemoryBytes];

EngineMemory Memory() {
    return {
        MemoryRegion{largeArena, sizeof(largeArena)},
        MemoryRegion{smallArena, sizeof(smallArena)},
    };
}

const char* StatusName(QualityStatus status) {
    switch (status) {
        case QualityStatus::Ok: return "Ok";
        case QualityStatus::NotInitialized: return "NotInitialized";
        case QualityStatus::InvalidQuality: return "InvalidQuality";
    }
    return "Unknown";
}

bool Check(const char* scenario, bool condition) {
    if (!condition) std::cerr << scenario << ": failed\n";
    return condition;
}

bool CheckStatus(const char* scenario, QualityStatus actual, QualityStatus expected) {
    if (actual == expected) return true;
    std::cerr << scenario << ": expected " << StatusName(expected)
              << ", got " << StatusName(actual) << '\n';
    return false;
}

// Processes kBlocks blocks of a 1 kHz tone, fully dry, and returns the
// largest output sample magnitude.
int PeakOutput(Engine& engine) {
    ControlSnapshot controls;
    controls.dryWet = 0.0f;
    AudioBlock in{};
    AudioBlock out{};
    int peak = 0;
    for (int block = 0; block < kBlocks; ++block) {
        for (std::size_t i = 0; i < in.size(); ++i) {
            const auto n = static_cast<double>(block * in.size() + i);
            const auto sample = static_cast<std::int16_t>(8000.0 * std::sin(n * 2.0 * 3.14159265 / 32.0));
            in[i].left = sample;
            in[i].right = sample;
        }
        engine.ProcessBlock(in, out, controls, BlockEvents{});
        for (const auto& frame : out) {
            peak = std::max(peak, std::abs(static_cast<int>(frame.left)));
            peak = std::max(peak, std::abs(static_cast<int>(frame.right)));
        }
    }
    return peak;
}

}  // namespace

int main() {
    int failures = 0;

    {
        Engine engine;
        failures += !CheckStatus("Before initialization",
                                 engine.SetQuality(Quality::Mono16Bit32k),
                                 QualityStatus::NotInitialized);
    }

    Engine engine;
    if (engine.Initialize(Memory(), Quality::Stereo16Bit32k, kTestSeed) != InitStatus::Ok) {
        std::cerr << "Initialization failed\n";
        return 1;
    }
    failures += !Check("Initial quality", engine.GetQuality() == Quality::Stereo16Bit32k);
    failures += !Check("Audio after initialization", PeakOutput(engine) > 0);

    failures += !CheckStatus("Invalid quality",
                             engine.SetQuality(static_cast<Quality>(999)),
                             QualityStatus::InvalidQuality);
    failures += !Check("Invalid quality keeps the current one",
                       engine.GetQuality() == Quality::Stereo16Bit32k);
    failures += !Check("Invalid quality keeps audio", PeakOutput(engine) > 0);

    failures += !CheckStatus("Same quality", engine.SetQuality(Quality::Stereo16Bit32k),
                             QualityStatus::Ok);
    failures += !Check("Same quality needs no reset", PeakOutput(engine) > 0);

    // Every quality in turn: silent until Service(), audible after it.
    for (const auto quality : {Quality::Mono16Bit32k, Quality::StereoMuLaw16k,
                               Quality::MonoMuLaw16k, Quality::Stereo16Bit32k}) {
        failures += !CheckStatus("Change quality", engine.SetQuality(quality), QualityStatus::Ok);
        failures += !Check("GetQuality after change", engine.GetQuality() == quality);
        failures += !Check("Silent until Service()", PeakOutput(engine) == 0);
        engine.Service();
        failures += !Check("Audio after Service()", PeakOutput(engine) > 0);
    }

    return failures == 0 ? 0 : 1;
}
