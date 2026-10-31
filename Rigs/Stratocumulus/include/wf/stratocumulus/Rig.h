#pragma once

#include <cstddef>

#include "wf/Rig.h"
#include "wf/clouds/Engine.h"
#include "wf/stratus/Engine.h"

namespace wf {
namespace stratocumulus {

// Stratocumulus: Clouds into Stratus. The input goes through Clouds (slot
// 0), whose output Stratus records, overdubs and plays over (slot 1):
// resampling, so Clouds' textures, randomness included, end up in the loop.
// Stratus' Thru passes Clouds on; with it off, only the loop sounds.
//
// The chain runs in place through the output buffer: Clouds from the input
// into the output, then Stratus from the output into itself, with no
// scratch. Memory: Clouds' part then Stratus', each aligned, in the host's
// one block. Rates are Clouds': 32, 44.1 and 48 kHz.
//
// Silent and deaf until both engines prepare, as wf::Rig requires: after a
// failed Prepare(), Process() writes zeros and Handle() drops every event,
// even when an engine kept a previous preparation.
class Rig final : public wf::Rig {
public:
    enum Slot : std::size_t { kCloudsSlot = 0, kStratusSlot = 1 };

    Rig() = default;
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    // The engines, for checks that know the rig.
    clouds::Engine& Clouds() noexcept { return clouds_; }
    stratus::Engine& Stratus() noexcept { return stratus_; }

    const char* Name() const noexcept override { return "Stratocumulus"; }
    std::size_t SlotCount() const noexcept override { return 2; }
    const Engine* Slot(std::size_t slot) const noexcept override;

    std::size_t MemoryBytes(float sampleRate, std::size_t maxFrames) const noexcept override;
    PrepareStatus Prepare(float sampleRate, std::size_t maxFrames,
                          MemoryBlock memory) noexcept override;

    void Process(const float* inLeft, const float* inRight,
                 float* outLeft, float* outRight, std::size_t frames) noexcept override;

private:
    Engine* MutableSlot(std::size_t slot) noexcept override;

    clouds::Engine clouds_;
    stratus::Engine stratus_;
    bool prepared_ = false;
};

}  // namespace stratocumulus
}  // namespace wf
