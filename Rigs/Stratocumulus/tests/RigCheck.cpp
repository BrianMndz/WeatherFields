#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "wf/stratocumulus/Rig.h"

namespace {

using wf::stratocumulus::Rig;
using Clouds = wf::clouds::Engine;
using Stratus = wf::stratus::Engine;
using Type = wf::ControlEvent::Type;

constexpr float kRate = 48000.0f;
constexpr std::size_t kMaxFrames = 64;

int failures = 0;

void Expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

// Heap memory with an aligned block of `bytes` inside it, optionally moved
// off the alignment by `offset`.
class Arena {
public:
    explicit Arena(std::size_t bytes) : storage_(new unsigned char[bytes + 2 * wf::kMemoryAlignment]), bytes_(bytes) {}

    wf::MemoryBlock Block(std::size_t offset = 0) const {
        const auto address = reinterpret_cast<std::uintptr_t>(storage_.get());
        const std::size_t align = (wf::kMemoryAlignment - address % wf::kMemoryAlignment) % wf::kMemoryAlignment;
        return wf::MemoryBlock{storage_.get() + align + offset, bytes_};
    }

private:
    std::unique_ptr<unsigned char[]> storage_;
    std::size_t bytes_;
};

wf::ControlEvent Param(std::size_t slot, wf::ParamId id, float value) {
    return wf::ControlEvent{Type::Param, static_cast<std::uint8_t>(slot), id, value};
}

void CheckDescription() {
    Rig rig;
    const wf::Rig& base = rig;
    Expect(std::strcmp(base.Name(), "Stratocumulus") == 0, "the rig is Stratocumulus");
    Expect(base.SlotCount() == 2 && base.Slot(Rig::kCloudsSlot) == &rig.Clouds() &&
               base.Slot(Rig::kStratusSlot) == &rig.Stratus() && base.Slot(2) == nullptr,
           "Clouds in slot 0, Stratus in slot 1, nothing after");
}

void CheckMemory() {
    Rig rig;
    wf::Rig& base = rig;
    const std::size_t bytes = base.MemoryBytes(kRate, kMaxFrames);
    Expect(bytes == wf::AlignMemory(rig.Clouds().MemoryBytes(kRate, kMaxFrames)) +
                        wf::AlignMemory(rig.Stratus().MemoryBytes(kRate, kMaxFrames)),
           "the rig's memory is both engines' aligned parts");
    Expect(bytes % wf::kMemoryAlignment == 0, "the rig's memory is aligned");

    Arena arena(bytes);
    wf::MemoryBlock shortBlock = arena.Block();
    shortBlock.bytes = bytes - 1;
    Expect(base.Prepare(kRate, kMaxFrames, shortBlock) == wf::PrepareStatus::MemoryTooSmall,
           "a block one byte short is refused before splitting");
    wf::MemoryBlock tiny = arena.Block();
    tiny.bytes = 16;
    Expect(base.Prepare(kRate, kMaxFrames, tiny) == wf::PrepareStatus::MemoryTooSmall,
           "a block smaller than Clouds' part is refused, never split");
    Expect(base.Prepare(kRate, kMaxFrames, arena.Block(4)) == wf::PrepareStatus::MemoryMisaligned,
           "a misaligned block is refused");
    Expect(base.Prepare(kRate, kMaxFrames, arena.Block()) == wf::PrepareStatus::Ok,
           "exactly enough aligned memory prepares");
}

// After a failed Prepare(), silent and deaf, even though Stratus (and, for
// a rate only Clouds refuses, nothing else) kept an earlier preparation.
void CheckFailedPrepare() {
    Rig rig;
    wf::Rig& base = rig;
    Arena arena(base.MemoryBytes(96000.0f, kMaxFrames));
    Expect(base.Prepare(kRate, kMaxFrames, arena.Block()) == wf::PrepareStatus::Ok, "48 kHz prepares");
    Expect(base.Handle(Param(Rig::kCloudsSlot, Clouds::kDryWet, 0.0f)) &&
               base.Handle(Param(Rig::kStratusSlot, Stratus::kRecord, 1)),
           "a prepared rig takes events");
    // A loop of Clouds' dry output, playing: Stratus keeps its preparation
    // through the failure below and would go on playing it.
    float left[kMaxFrames], right[kMaxFrames];
    for (int call = 0; call < 100; ++call) {
        for (std::size_t i = 0; i < kMaxFrames; ++i) left[i] = right[i] = 0.5f;
        base.Process(left, right, left, right, kMaxFrames);
    }
    base.Handle(Param(Rig::kStratusSlot, Stratus::kRecord, 0));
    base.Handle(Param(Rig::kStratusSlot, Stratus::kThru, 0));
    for (int call = 0; call < 10; ++call) base.Process(left, right, left, right, kMaxFrames);
    Expect(std::fabs(left[0]) > 0.1f, "the loop plays before the failure");

    Expect(base.Prepare(96000.0f, kMaxFrames, arena.Block()) == wf::PrepareStatus::UnsupportedSampleRate,
           "96 kHz is refused: Clouds' rates are the rig's");
    Expect(!base.Handle(Param(Rig::kStratusSlot, Stratus::kRecord, 0)) &&
               !base.Handle(Param(Rig::kCloudsSlot, Clouds::kFreeze, 1)),
           "after a failed Prepare() the rig drops events for every slot");
    for (std::size_t i = 0; i < kMaxFrames; ++i) left[i] = right[i] = 0.5f;
    base.Process(left, right, left, right, kMaxFrames);
    bool silent = true;
    for (std::size_t i = 0; i < kMaxFrames; ++i) silent = silent && left[i] == 0.0f && right[i] == 0.0f;
    Expect(silent, "after a failed Prepare() the rig is silent");

    Expect(base.Prepare(kRate, kMaxFrames, arena.Block()) == wf::PrepareStatus::Ok, "48 kHz prepares again");
    Expect(base.Handle(Param(Rig::kStratusSlot, Stratus::kRecord, 1)), "and takes events again");
}

void CheckRouting() {
    Rig rig;
    wf::Rig& base = rig;
    Arena arena(base.MemoryBytes(kRate, kMaxFrames));
    Expect(base.Prepare(kRate, kMaxFrames, arena.Block()) == wf::PrepareStatus::Ok, "the rig prepares");
    Expect(base.Handle(Param(Rig::kCloudsSlot, Clouds::kFreeze, 1)) &&
               rig.Clouds().ParamValue(Clouds::kFreeze) == 1.0f && rig.Stratus().ParamValue(Stratus::kRecord) == 0.0f,
           "slot 0 reaches Clouds only");
    Expect(base.Handle(Param(Rig::kStratusSlot, Stratus::kLoopLevel, -12)) &&
               rig.Stratus().ParamValue(Stratus::kLoopLevel) == -12.0f,
           "slot 1 reaches Stratus");
    Expect(!base.Handle(Param(2, Stratus::kRecord, 1)), "there is no slot 2");
    Expect(!base.Handle(Param(Rig::kStratusSlot, Stratus::kPosition, 1)), "Stratus' readouts stay read-only");
    Expect(!base.Handle(Param(Rig::kCloudsSlot, Clouds::kPitch, std::nanf(""))), "values are still constrained");
}

// One timeline of events at callback starts: Clouds wetter, a loop of Clouds'
// output recorded, Clouds frozen, an overdub, Loop Level and Thru changed.
struct Timeline {
    std::size_t frame;
    std::size_t slot;
    wf::ParamId id;
    float value;
};

const Timeline kEvents[] = {
    {0, Rig::kCloudsSlot, Clouds::kDryWet, 0.6f},
    {0, Rig::kCloudsSlot, Clouds::kPosition, 0.3f},
    {9600, Rig::kStratusSlot, Stratus::kRecord, 1},
    {33600, Rig::kStratusSlot, Stratus::kRecord, 0},
    {48000, Rig::kCloudsSlot, Clouds::kFreeze, 1},
    {57600, Rig::kStratusSlot, Stratus::kRecord, 1},
    {67200, Rig::kStratusSlot, Stratus::kRecord, 0},
    {72000, Rig::kStratusSlot, Stratus::kLoopLevel, -6},
    {76800, Rig::kStratusSlot, Stratus::kThru, 0},
};
constexpr std::size_t kTotal = 96000;  // 2 s
constexpr std::size_t kLoudFrom = 76800;  // Thru off: only the loop sounds

float Input(std::size_t frame, int channel) {
    const double t = static_cast<double>(frame) / kRate;
    return 0.4f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * (channel == 0 ? 220.0 : 331.0) * t));
}

// Calls `process(in, out, count)` in callbacks of uneven sizes, sending each
// event through `send(slot, id, value)` at the callback where it falls, and
// returns the interleaved output.
template <typename Send, typename Process>
std::vector<float> Render(Send send, Process process) {
    const std::size_t sizes[] = {64, 37, 1, 64, 50, 13};
    std::vector<float> output(2 * kTotal);
    float inLeft[kMaxFrames], inRight[kMaxFrames], outLeft[kMaxFrames], outRight[kMaxFrames];
    std::size_t next = 0;
    for (std::size_t done = 0, call = 0; done < kTotal; ++call) {
        std::size_t count = sizes[call % 6];
        if (count > kTotal - done) count = kTotal - done;
        // Events land at the first callback starting at or after their frame.
        while (next < sizeof(kEvents) / sizeof(kEvents[0]) && kEvents[next].frame <= done) {
            send(kEvents[next].slot, kEvents[next].id, kEvents[next].value);
            ++next;
        }
        for (std::size_t i = 0; i < count; ++i) {
            inLeft[i] = Input(done + i, 0);
            inRight[i] = Input(done + i, 1);
        }
        process(inLeft, inRight, outLeft, outRight, count);
        for (std::size_t i = 0; i < count; ++i) {
            output[2 * (done + i)] = outLeft[i];
            output[2 * (done + i) + 1] = outRight[i];
        }
        done += count;
    }
    return output;
}

std::vector<float> RenderRig(bool inPlace) {
    Rig rig;
    wf::Rig& base = rig;
    Arena arena(base.MemoryBytes(kRate, kMaxFrames));
    if (base.Prepare(kRate, kMaxFrames, arena.Block()) != wf::PrepareStatus::Ok) return {};
    return Render(
        [&base](std::size_t slot, wf::ParamId id, float value) { base.Handle(Param(slot, id, value)); },
        [&base, inPlace](float* inL, float* inR, float* outL, float* outR, std::size_t count) {
            if (inPlace) {
                base.Process(inL, inR, inL, inR, count);
                std::memcpy(outL, inL, count * sizeof(float));
                std::memcpy(outR, inR, count * sizeof(float));
            } else {
                base.Process(inL, inR, outL, outR, count);
            }
        });
}

// The two engines by hand, each in its own solo rig with its own memory and
// separate buffers between them; `cloudsOnly` returns Clouds' output.
std::vector<float> RenderByHand(bool cloudsOnly) {
    wf::SoloRig<Clouds> clouds;
    wf::SoloRig<Stratus> stratus;
    Arena cloudsArena(clouds.MemoryBytes(kRate, kMaxFrames));
    Arena stratusArena(stratus.MemoryBytes(kRate, kMaxFrames));
    if (clouds.Prepare(kRate, kMaxFrames, cloudsArena.Block()) != wf::PrepareStatus::Ok ||
        stratus.Prepare(kRate, kMaxFrames, stratusArena.Block()) != wf::PrepareStatus::Ok) {
        return {};
    }
    return Render(
        [&](std::size_t slot, wf::ParamId id, float value) {
            wf::Rig& target = slot == Rig::kCloudsSlot ? static_cast<wf::Rig&>(clouds) : stratus;
            target.Handle(Param(0, id, value));
        },
        [&](float* inL, float* inR, float* outL, float* outR, std::size_t count) {
            float midLeft[kMaxFrames], midRight[kMaxFrames];
            clouds.Process(inL, inR, midLeft, midRight, count);
            if (cloudsOnly) {
                std::memcpy(outL, midLeft, count * sizeof(float));
                std::memcpy(outR, midRight, count * sizeof(float));
            } else {
                stratus.Process(midLeft, midRight, outL, outR, count);
            }
        });
}

bool Same(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && !a.empty() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// The chain is Clouds then Stratus, nothing more: bit for bit the engines
// run by hand, in place or not. Each Prepare() seeds stmlib::Random, and
// each render runs alone, so all draw the same numbers.
void CheckChain() {
    const std::vector<float> separate = RenderRig(false);
    const std::vector<float> inPlace = RenderRig(true);
    const std::vector<float> byHand = RenderByHand(false);
    const std::vector<float> cloudsOnly = RenderByHand(true);
    Expect(Same(separate, byHand), "the rig gives Clouds then Stratus run by hand, bit for bit");
    Expect(Same(inPlace, byHand), "in place, the rig gives the same, bit for bit");

    // The loop, not Clouds, sounds once Thru is off.
    float loopPeak = 0.0f;
    float cloudsDifference = 0.0f;
    for (std::size_t i = 2 * kLoudFrom; i < byHand.size() && i < cloudsOnly.size(); ++i) {
        loopPeak = std::max(loopPeak, std::fabs(byHand[i]));
        cloudsDifference = std::max(cloudsDifference, std::fabs(byHand[i] - cloudsOnly[i]));
    }
    Expect(loopPeak > 0.01f, "with Thru off, the loop of Clouds' output sounds");
    Expect(cloudsDifference > 0.01f, "Stratus changes the output: it is in the chain");
}

}  // namespace

int main() {
    CheckDescription();
    CheckMemory();
    CheckFailedPrepare();
    CheckRouting();
    CheckChain();

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
