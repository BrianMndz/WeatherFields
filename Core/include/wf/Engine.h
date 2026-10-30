#pragma once

#include <cstddef>
#include <cstdint>

#include "wf/Events.h"
#include "wf/Params.h"

namespace wf {

// Every arena a host hands out starts at this alignment, enough for NEON and
// every engine's buffers.
constexpr std::size_t kMemoryAlignment = 16;

constexpr std::size_t AlignMemory(std::size_t bytes) noexcept {
    return (bytes + kMemoryAlignment - 1) & ~(kMemoryAlignment - 1);
}

// Memory the host provides: SDRAM on Daisy, a static buffer or one allocated
// in setup() on Bela, the heap on desktop. The engine never allocates.
struct MemoryBlock {
    void* data;
    std::size_t bytes;
};

enum class PrepareStatus : std::uint8_t {
    Ok,
    MemoryTooSmall,
    MemoryMisaligned,
    UnsupportedSampleRate,
    UnsupportedBlockSize,
};

// Whether `memory` can hold `required` bytes at kMemoryAlignment.
inline PrepareStatus CheckMemory(MemoryBlock memory, std::size_t required) noexcept {
    if (required == 0) return PrepareStatus::Ok;
    if (memory.data == nullptr || memory.bytes < required) return PrepareStatus::MemoryTooSmall;
    if (reinterpret_cast<std::uintptr_t>(memory.data) % kMemoryAlignment != 0) {
        return PrepareStatus::MemoryMisaligned;
    }
    return PrepareStatus::Ok;
}

// The interface every engine implements, on every platform. Prepare() runs
// off the audio thread; ParamValue(), Handle() and Process() run on it and
// follow the real-time rules: no allocation, locks, I/O or system calls.
//
// Until a Prepare() returns Ok, and again after one that fails, the engine
// is silent: Process() writes zeros and Handle() ignores events. A host may
// therefore run its callback around an engine whose Prepare() failed.
class Engine {
public:
    virtual ~Engine() = default;

    // Static storage; shown in menus and panels.
    virtual const char* Name() const noexcept = 0;

    virtual const ParamTable& Params() const noexcept = 0;

    // The memory Prepare() needs for this rate and callback size. A multiple
    // of kMemoryAlignment; 0 when the engine needs none.
    virtual std::size_t MemoryBytes(float sampleRate, std::size_t maxFrames) const noexcept = 0;

    // Not real-time, and never concurrent with Handle() or Process(). Resets
    // the engine to its defaults; may be called again, for example after a
    // sample-rate change, with memory for the new MemoryBytes(). The engine
    // keeps using `memory` until the next Prepare() or its destruction.
    virtual PrepareStatus Prepare(float sampleRate, std::size_t maxFrames,
                                  MemoryBlock memory) noexcept = 0;

    // Real-time, before Process(). The rig has validated `event` against
    // Params() and constrained its value (ConstrainEvent() in Rig.h). Its
    // cost counts towards the callback with Process(): an engine documents
    // any expensive event (Clouds' quality and mode run the module's
    // Service(), about 0.2 ms on Bela).
    virtual void Handle(const ControlEvent& event) noexcept = 0;

    // Real-time, on the audio thread only. The value parameter `id` has now:
    // as the last event set it, or as the engine changed it itself (a Clouds
    // quality change releases freeze), constrained as by ConstrainValue();
    // a Readout, what the engine reports, within its spec's range.
    // Trigger and unknown IDs read 0; values are meaningful only after a
    // successful Prepare(). Other threads never call it: the host copies the
    // values out (to atomics for its GUI) after the callback's events.
    virtual float ParamValue(ParamId id) const noexcept = 0;

    // Real-time. `frames` is at most Prepare()'s maxFrames. Planar stereo;
    // each output may be the same buffer as its input (in-place, as Bela
    // processes), but buffers never partially overlap. Inputs are always
    // valid: a host with no input passes silence, never null.
    virtual void Process(const float* inLeft, const float* inRight,
                         float* outLeft, float* outRight, std::size_t frames) noexcept = 0;
};

}  // namespace wf
