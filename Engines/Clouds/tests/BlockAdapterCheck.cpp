#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <vector>

#include "wf/clouds/BlockAdapter.h"

namespace {

using wf::clouds::BlockAdapter;
using wf::clouds::Module;
using wf::clouds::EngineMemory;
using wf::clouds::InitStatus;
using wf::clouds::MemoryRegion;
using wf::clouds::PlaybackMode;
using wf::clouds::PlaybackModeStatus;
using wf::clouds::Quality;
using wf::clouds::kLargeMemoryBytes;
using wf::clouds::kMemoryAlignment;
using wf::clouds::kSmallMemoryBytes;

constexpr std::size_t kFrameCount = 160;
constexpr std::uint32_t kTestSeed = 0x23u;

alignas(kMemoryAlignment) unsigned char largeArena[kLargeMemoryBytes];
alignas(kMemoryAlignment) unsigned char smallArena[kSmallMemoryBytes];

struct PlanarBuffer {
    std::vector<float> left;
    std::vector<float> right;
};

PlanarBuffer MakeInput() {
    PlanarBuffer input{
        std::vector<float>(kFrameCount),
        std::vector<float>(kFrameCount),
    };

    for (std::size_t i = 0; i < kFrameCount; ++i) {
        input.left[i] = 0.1f + static_cast<float>((i * 17) % 101) / 202.0f;
        input.right[i] = -0.1f - static_cast<float>((i * 29) % 101) / 202.0f;
    }

    return input;
}

PlanarBuffer Render(const PlanarBuffer& input,
                    std::initializer_list<std::size_t> chunkSizes,
                    bool inPlace) {
    PlanarBuffer output = inPlace
        ? input
        : PlanarBuffer{
              std::vector<float>(input.left.size()),
              std::vector<float>(input.right.size()),
          };

    Module engine;
    const EngineMemory memory{
        MemoryRegion{largeArena, sizeof(largeArena)},
        MemoryRegion{smallArena, sizeof(smallArena)},
    };

    const InitStatus status =
        engine.Initialize(memory, Quality::Stereo16Bit32k, kTestSeed);
    if (status != InitStatus::Ok) {
        std::cerr << "Module initialization failed\n";
        return output;
    }

    BlockAdapter adapter{engine};
    const wf::clouds::ControlSnapshot controls{};
    std::size_t offset = 0;

    for (const std::size_t chunkSize : chunkSizes) {
        const float* inputLeft =
            inPlace ? output.left.data() + offset : input.left.data() + offset;
        const float* inputRight =
            inPlace ? output.right.data() + offset : input.right.data() + offset;

        adapter.Process(inputLeft, inputRight,
                        output.left.data() + offset, output.right.data() + offset,
                        chunkSize, controls, wf::clouds::BlockEvents{});
        offset += chunkSize;
    }

    if (offset != input.left.size()) {
        std::cerr << "Chunk sizes covered " << offset << " frames, expected "
                  << input.left.size() << '\n';
    }

    return output;
}

// A playback mode change silences the engine until Service(), so whether the
// adapter calls it shows in the output.
bool ResumesAfterModeChange(BlockAdapter::ServicePolicy policy) {
    Module engine;
    const EngineMemory memory{
        MemoryRegion{largeArena, sizeof(largeArena)},
        MemoryRegion{smallArena, sizeof(smallArena)},
    };
    if (engine.Initialize(memory, Quality::Stereo16Bit32k, kTestSeed) != InitStatus::Ok
        || engine.SetPlaybackMode(PlaybackMode::LoopingDelay) != PlaybackModeStatus::Ok) {
        std::cerr << "Module setup failed\n";
        return false;
    }

    BlockAdapter adapter{engine, policy};
    const PlanarBuffer input = MakeInput();
    PlanarBuffer output{
        std::vector<float>(kFrameCount),
        std::vector<float>(kFrameCount),
    };
    adapter.Process(input.left.data(), input.right.data(),
                    output.left.data(), output.right.data(),
                    kFrameCount, wf::clouds::ControlSnapshot{},
                    wf::clouds::BlockEvents{});

    for (std::size_t i = 0; i < kFrameCount; ++i) {
        if (output.left[i] != 0.0f || output.right[i] != 0.0f) return true;
    }
    return false;
}

// Renders spectral mode through 16-frame calls, half an engine block each,
// with a TRIG click at call `clickCall`: trigger and gate in that call only,
// as a host sends a press shorter than its buffer. Returns the left output.
std::vector<float> RenderSpectralClick(int clickCall) {
    constexpr std::size_t kCallFrames = 16;
    constexpr int kCalls = 1200;

    Module engine;
    const EngineMemory memory{
        MemoryRegion{largeArena, sizeof(largeArena)},
        MemoryRegion{smallArena, sizeof(smallArena)},
    };
    if (engine.Initialize(memory, Quality::Stereo16Bit32k, kTestSeed) != InitStatus::Ok
        || engine.SetPlaybackMode(PlaybackMode::Spectral) != PlaybackModeStatus::Ok) {
        std::cerr << "Module setup failed\n";
        return {};
    }

    BlockAdapter adapter{engine};
    std::vector<float> in(kCallFrames);
    std::vector<float> out(kCallFrames);
    std::vector<float> rendered;
    wf::clouds::ControlSnapshot controls;
    controls.dryWet = 1.0f;

    for (int call = 0; call < kCalls; ++call) {
        for (std::size_t i = 0; i < kCallFrames; ++i) {
            const auto n = static_cast<float>(static_cast<std::size_t>(call) * kCallFrames + i);
            in[i] = 0.3f * std::sin(n * 0.07f) + 0.2f * std::sin(n * 0.011f);
        }
        const bool click = call == clickCall;
        controls.gate = click;
        adapter.Process(in.data(), in.data(), out.data(), out.data(), kCallFrames,
                        controls, wf::clouds::BlockEvents{click});
        rendered.insert(rendered.end(), out.begin(), out.end());
    }
    return rendered;
}

}  // namespace

int main() {
    int failures = 0;
    const PlanarBuffer input = MakeInput();

    const PlanarBuffer singleChunk = Render(input, {160}, false);
    const PlanarBuffer variedChunks =
        Render(input, {10, 10, 12, 7, 41, 3, 64, 13}, false);
    const PlanarBuffer variedChunksInPlace =
        Render(input, {10, 10, 12, 7, 41, 3, 64, 13}, true);

    if (singleChunk.left != variedChunks.left) {
        std::cerr << "Left outputs differ between chunk layouts\n";
        ++failures;
    }

    if (singleChunk.right != variedChunks.right) {
        std::cerr << "Right outputs differ between chunk layouts\n";
        ++failures;
    }

    if (variedChunks.left != variedChunksInPlace.left) {
        std::cerr << "Left output differs between out-of-place and in-place processing\n";
        ++failures;
    }

    if (variedChunks.right != variedChunksInPlace.right) {
        std::cerr << "Right output differs between out-of-place and in-place processing\n";
        ++failures;
    }

    for (std::size_t i = 0; i < BlockAdapter::kLatencyFrames; ++i) {
        if (singleChunk.left[i] != 0.0f || singleChunk.right[i] != 0.0f) {
            std::cerr << "Expected silence at frame " << i << '\n';
            ++failures;
            break;
        }
    }

    bool foundNonzeroSample = false;
    for (std::size_t i = BlockAdapter::kLatencyFrames; i < kFrameCount; ++i) {
        if (singleChunk.left[i] != 0.0f || singleChunk.right[i] != 0.0f) {
            foundNonzeroSample = true;
            break;
        }
    }

    if (!foundNonzeroSample) {
        std::cerr << "Expected nonzero output after the initial latency\n";
        ++failures;
    }

    if (!ResumesAfterModeChange(BlockAdapter::ServicePolicy::AfterEachBlock)) {
        std::cerr << "AfterEachBlock: expected audio after a mode change\n";
        ++failures;
    }

    if (ResumesAfterModeChange(BlockAdapter::ServicePolicy::Manual)) {
        std::cerr << "Manual: expected silence until the host calls Service()\n";
        ++failures;
    }

    // A click shorter than an engine block must reach the block that consumes
    // its trigger as a gate. The clicks fall in even calls, the first half of
    // an engine block, so the block runs in the next call, whose controls no
    // longer carry the gate. Spectral mode reads the gate once per hop (1024
    // frames, 64 calls), so over 64 such positions (two hops) some must
    // change the output.
    const std::vector<float> unclicked = RenderSpectralClick(-1);
    int changed = 0;
    for (int offset = 0; offset < 64; ++offset) {
        if (RenderSpectralClick(700 + 2 * offset) != unclicked) ++changed;
    }
    if (changed == 0) {
        std::cerr << "Split callbacks: no click reached spectral mode as a gate\n";
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}
