#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include "wf/clouds/SampleRateAdapter.h"

namespace {

using wf::clouds::BlockAdapter;
using wf::clouds::Module;
using wf::clouds::EngineMemory;
using wf::clouds::InitStatus;
using wf::clouds::MemoryRegion;
using wf::clouds::Quality;
using wf::clouds::SampleRateAdapter;
using wf::clouds::SampleRateStatus;
using wf::clouds::kLargeMemoryBytes;
using wf::clouds::kMemoryAlignment;
using wf::clouds::kSmallMemoryBytes;

constexpr std::size_t kFrameCount = 2048;
constexpr std::uint32_t kTestSeed = 0x23u;

alignas(kMemoryAlignment) unsigned char largeArena[kLargeMemoryBytes];
alignas(kMemoryAlignment) unsigned char smallArena[kSmallMemoryBytes];

struct PlanarBuffer {
    std::vector<float> left;
    std::vector<float> right;
};

struct RenderResult {
    PlanarBuffer output;
    bool ok = false;
};

EngineMemory ValidMemory() {
    return {
        MemoryRegion{largeArena, sizeof(largeArena)},
        MemoryRegion{smallArena, sizeof(smallArena)},
    };
}

PlanarBuffer MakeInput(std::size_t frameCount) {
    PlanarBuffer input{
        std::vector<float>(frameCount),
        std::vector<float>(frameCount),
    };

    for (std::size_t i = 0; i < frameCount; ++i) {
        input.left[i] = 0.1f + static_cast<float>((i * 17) % 101) / 202.0f;
        input.right[i] = -0.1f - static_cast<float>((i * 29) % 101) / 202.0f;
    }

    return input;
}

std::vector<std::size_t> MakeIrregularChunks(std::size_t frameCount) {
    constexpr std::array<std::size_t, 8> pattern{16, 7, 31, 1, 48, 5, 64, 13};
    std::vector<std::size_t> chunks;
    std::size_t covered = 0;
    std::size_t patternIndex = 0;

    while (covered < frameCount) {
        const std::size_t chunk =
            pattern[patternIndex % pattern.size()] < frameCount - covered
                ? pattern[patternIndex % pattern.size()]
                : frameCount - covered;
        chunks.push_back(chunk);
        covered += chunk;
        ++patternIndex;
    }

    return chunks;
}

bool InitializeEngine(Module& engine) {
    if (engine.Initialize(ValidMemory(), Quality::Stereo16Bit32k, kTestSeed) ==
        InitStatus::Ok) {
        return true;
    }

    std::cerr << "Module initialization failed\n";
    return false;
}

RenderResult Render(const PlanarBuffer& input, std::uint32_t sampleRate,
                    const std::vector<std::size_t>& chunkSizes, bool inPlace) {
    RenderResult result{
        inPlace
            ? input
            : PlanarBuffer{
                  std::vector<float>(input.left.size()),
                  std::vector<float>(input.right.size()),
              },
        false,
    };

    Module engine;
    if (!InitializeEngine(engine)) return result;

    BlockAdapter blockAdapter{engine};
    SampleRateAdapter sampleRateAdapter{blockAdapter};
    if (sampleRateAdapter.Configure(sampleRate) != SampleRateStatus::Ok) {
        std::cerr << "Sample-rate configuration failed for " << sampleRate << " Hz\n";
        return result;
    }

    const wf::clouds::ControlSnapshot controls{};
    std::size_t offset = 0;

    for (const std::size_t chunkSize : chunkSizes) {
        if (chunkSize > input.left.size() - offset) {
            std::cerr << "Chunk layout exceeds the input length\n";
            return result;
        }

        const float* inputLeft = inPlace
            ? result.output.left.data() + offset
            : input.left.data() + offset;
        const float* inputRight = inPlace
            ? result.output.right.data() + offset
            : input.right.data() + offset;

        sampleRateAdapter.Process(
            inputLeft, inputRight,
            result.output.left.data() + offset,
            result.output.right.data() + offset,
            chunkSize, controls, wf::clouds::BlockEvents{});
        offset += chunkSize;
    }

    if (offset != input.left.size()) {
        std::cerr << "Chunk layout covered " << offset << " frames, expected "
                  << input.left.size() << '\n';
        return result;
    }

    result.ok = true;
    return result;
}

int CheckRate(std::uint32_t sampleRate, const PlanarBuffer& input,
              const std::vector<std::size_t>& irregularChunks) {
    int failures = 0;
    const RenderResult singleCall =
        Render(input, sampleRate, {input.left.size()}, false);
    const RenderResult irregular =
        Render(input, sampleRate, irregularChunks, false);
    const RenderResult irregularInPlace =
        Render(input, sampleRate, irregularChunks, true);

    if (!singleCall.ok || !irregular.ok || !irregularInPlace.ok) {
        std::cerr << "Render setup failed for " << sampleRate << " Hz\n";
        return 1;
    }

    if (singleCall.output.left != irregular.output.left ||
        singleCall.output.right != irregular.output.right) {
        std::cerr << "Output depends on callback partitioning at "
                  << sampleRate << " Hz\n";
        ++failures;
    }

    if (irregular.output.left != irregularInPlace.output.left ||
        irregular.output.right != irregularInPlace.output.right) {
        std::cerr << "In-place output differs at " << sampleRate << " Hz\n";
        ++failures;
    }

    for (std::size_t i = 0; i < BlockAdapter::kLatencyFrames; ++i) {
        if (singleCall.output.left[i] != 0.0f ||
            singleCall.output.right[i] != 0.0f) {
            std::cerr << "Expected startup silence at frame " << i
                      << " for " << sampleRate << " Hz\n";
            ++failures;
            break;
        }
    }

    bool foundNonzeroSample = false;
    for (std::size_t i = BlockAdapter::kLatencyFrames; i < input.left.size(); ++i) {
        if (singleCall.output.left[i] != 0.0f ||
            singleCall.output.right[i] != 0.0f) {
            foundNonzeroSample = true;
            break;
        }
    }

    if (!foundNonzeroSample) {
        std::cerr << "Expected active output at " << sampleRate << " Hz\n";
        ++failures;
    }

    return failures;
}

int CheckReconfiguration(const PlanarBuffer& input) {
    Module engine;
    if (!InitializeEngine(engine)) return 1;

    BlockAdapter blockAdapter{engine};
    SampleRateAdapter sampleRateAdapter{blockAdapter};
    int failures = 0;

    if (sampleRateAdapter.Configure(44100u) != SampleRateStatus::Ok ||
        !sampleRateAdapter.IsConfigured() ||
        sampleRateAdapter.ExternalSampleRateHz() != 44100u) {
        std::cerr << "Initial 44.1 kHz configuration failed\n";
        ++failures;
    }

    std::array<float, 257> discardedLeft{};
    std::array<float, 257> discardedRight{};
    sampleRateAdapter.Process(
        input.left.data(), input.right.data(),
        discardedLeft.data(), discardedRight.data(), discardedLeft.size(),
        wf::clouds::ControlSnapshot{}, wf::clouds::BlockEvents{true});

    if (sampleRateAdapter.Configure(12345u) !=
            SampleRateStatus::UnsupportedExternalSampleRate ||
        sampleRateAdapter.IsConfigured() ||
        sampleRateAdapter.ExternalSampleRateHz() != 0u) {
        std::cerr << "Unsupported reconfiguration did not fail closed\n";
        ++failures;
    }

    if (sampleRateAdapter.Configure(48000u) != SampleRateStatus::Ok ||
        !sampleRateAdapter.IsConfigured() ||
        sampleRateAdapter.ExternalSampleRateHz() != 48000u) {
        std::cerr << "Valid retry at 48 kHz failed\n";
        ++failures;
    }

    std::array<float, BlockAdapter::kLatencyFrames> outputLeft{};
    std::array<float, BlockAdapter::kLatencyFrames> outputRight{};
    sampleRateAdapter.Process(
        input.left.data(), input.right.data(), outputLeft.data(), outputRight.data(),
        outputLeft.size(), wf::clouds::ControlSnapshot{},
        wf::clouds::BlockEvents{});

    for (std::size_t i = 0; i < outputLeft.size(); ++i) {
        if (outputLeft[i] != 0.0f || outputRight[i] != 0.0f) {
            std::cerr << "Reconfiguration retained stale stream output\n";
            ++failures;
            break;
        }
    }

    return failures;
}

}  // namespace

int main() {
    const PlanarBuffer input = MakeInput(kFrameCount);
    const std::vector<std::size_t> irregularChunks =
        MakeIrregularChunks(kFrameCount);

    int failures = 0;
    failures += CheckRate(44100u, input, irregularChunks);
    failures += CheckRate(48000u, input, irregularChunks);
    failures += CheckReconfiguration(input);
    return failures == 0 ? 0 : 1;
}
