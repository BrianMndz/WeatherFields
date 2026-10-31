#include "wf/stratocumulus/Rig.h"

namespace wf {
namespace stratocumulus {

const Engine* Rig::Slot(std::size_t slot) const noexcept {
    switch (slot) {
        case kCloudsSlot: return &clouds_;
        case kStratusSlot: return &stratus_;
        default: return nullptr;
    }
}

// Deaf until prepared: Rig::Handle() drops an event whose slot is null.
Engine* Rig::MutableSlot(std::size_t slot) noexcept {
    if (!prepared_) return nullptr;
    switch (slot) {
        case kCloudsSlot: return &clouds_;
        case kStratusSlot: return &stratus_;
        default: return nullptr;
    }
}

// Each engine's part rounded up to the alignment, so Stratus' part starts
// aligned too. Engines report 0 for a rate they refuse; Prepare() then
// fails in that engine.
std::size_t Rig::MemoryBytes(float sampleRate, std::size_t maxFrames) const noexcept {
    return AlignMemory(clouds_.MemoryBytes(sampleRate, maxFrames)) +
           AlignMemory(stratus_.MemoryBytes(sampleRate, maxFrames));
}

PrepareStatus Rig::Prepare(float sampleRate, std::size_t maxFrames, MemoryBlock memory) noexcept {
    prepared_ = false;

    // The whole block first: splitting a short one would hand Stratus a
    // part outside it.
    const PrepareStatus memoryStatus = CheckMemory(memory, MemoryBytes(sampleRate, maxFrames));
    if (memoryStatus != PrepareStatus::Ok) return memoryStatus;

    const std::size_t cloudsBytes = AlignMemory(clouds_.MemoryBytes(sampleRate, maxFrames));
    auto* const base = static_cast<unsigned char*>(memory.data);
    const PrepareStatus cloudsStatus = clouds_.Prepare(sampleRate, maxFrames, MemoryBlock{base, cloudsBytes});
    if (cloudsStatus != PrepareStatus::Ok) return cloudsStatus;
    const PrepareStatus stratusStatus =
        stratus_.Prepare(sampleRate, maxFrames, MemoryBlock{base + cloudsBytes, memory.bytes - cloudsBytes});
    if (stratusStatus != PrepareStatus::Ok) return stratusStatus;

    prepared_ = true;
    return PrepareStatus::Ok;
}

// Clouds reads the input before writing the output, then Stratus works in
// place on Clouds' output; both engines allow each output to be its input,
// so the host's buffers may be in place too.
void Rig::Process(const float* inLeft, const float* inRight,
                  float* outLeft, float* outRight, std::size_t frames) noexcept {
    if (!prepared_) {
        for (std::size_t i = 0; i < frames; ++i) {
            outLeft[i] = 0.0f;
            outRight[i] = 0.0f;
        }
        return;
    }
    clouds_.Process(inLeft, inRight, outLeft, outRight, frames);
    stratus_.Process(outLeft, outRight, outLeft, outRight, frames);
}

}  // namespace stratocumulus
}  // namespace wf
