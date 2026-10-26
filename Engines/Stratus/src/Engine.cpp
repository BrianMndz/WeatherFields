#include "wf/stratus/Engine.h"

#include <cmath>

#include "wf/Dsp.h"

using wf::stratus::Engine;

namespace {

constexpr float kMaxSeconds = 30.0f;  // 60 once tested
constexpr std::size_t kChannels = 2;
// Above any real host, and low enough that the sizes cannot overflow a 32-bit
// std::size_t (Bela): 192 kHz needs 46 MB.
constexpr float kMaxSampleRate = 192000.0f;

// IsFinite rather than comparisons alone: under -ffast-math (Bela) a NaN
// may pass them.
bool SupportedRate(float sampleRate) {
    return wf::IsFinite(sampleRate) && sampleRate > 0.0f && sampleRate <= kMaxSampleRate;
}

// 0 for an unsupported rate, so MemoryBytes() is safe before Prepare()
// refuses it.
std::size_t FramesFor(float sampleRate) {
    if (!SupportedRate(sampleRate)) return 0;
    return static_cast<std::size_t>(std::ceil(kMaxSeconds * sampleRate));
}

std::size_t ChannelBytes(float sampleRate) {
    return wf::AlignMemory(FramesFor(sampleRate) * sizeof(float));
}

}  // namespace

std::size_t Engine::MemoryBytes(float sampleRate, std::size_t) const noexcept {
    return kChannels * ChannelBytes(sampleRate);
}

wf::PrepareStatus Engine::Prepare(float sampleRate, std::size_t, MemoryBlock memory) noexcept {
    // Silent until this succeeds: a failure below must not leave Process()
    // running on pointers into the previous block.
    prepared_ = false;

    // The rate first: MemoryBytes() converts 30 s of it to a frame count.
    if (!SupportedRate(sampleRate)) return PrepareStatus::UnsupportedSampleRate;
    const PrepareStatus memoryStatus = CheckMemory(memory, MemoryBytes(sampleRate, 0));
    if (memoryStatus != PrepareStatus::Ok) return memoryStatus;

    // Left channel, then right, each aligned. The buffer is not cleared: the
    // first take overwrites what it plays, and nothing reads past length_.
    auto* bytes = static_cast<unsigned char*>(memory.data);
    left_ = reinterpret_cast<float*>(bytes);
    right_ = reinterpret_cast<float*>(bytes + ChannelBytes(sampleRate));
    capacity_ = FramesFor(sampleRate);

    state_ = State::kEmpty;
    length_ = 0;
    position_ = 0;

    loopLevelDb_ = 0.0f;
    loopGain_ = 1.0f;
    thru_ = true;

    prepared_ = true;
    return PrepareStatus::Ok;
}
