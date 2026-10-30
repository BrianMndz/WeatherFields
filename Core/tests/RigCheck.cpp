#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "wf/Rig.h"

namespace {

int failures = 0;

void Expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

float FromBits(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The smallest engine that exercises every part of the interface: a gain in
// dB, a mute toggle, a choice, a trigger, a readout of the linear gain,
// notes, and memory it writes to.
class GainEngine final : public wf::Engine {
public:
    enum : wf::ParamId { kGain = 0, kMute = 1, kCurve = 2, kReset = 3, kLinear = 4 };

    static constexpr std::size_t kMemoryPerFrame = 8;

    const char* Name() const noexcept override { return "Gain"; }

    const wf::ParamTable& Params() const noexcept override { return kTable; }

    std::size_t MemoryBytes(float, std::size_t maxFrames) const noexcept override {
        return wf::AlignMemory(maxFrames * kMemoryPerFrame);
    }

    wf::PrepareStatus Prepare(float sampleRate, std::size_t maxFrames,
                              wf::MemoryBlock memory) noexcept override {
        prepared_ = false;
        if (!(sampleRate > 0.0f)) return wf::PrepareStatus::UnsupportedSampleRate;
        if (maxFrames == 0) return wf::PrepareStatus::UnsupportedBlockSize;
        const wf::PrepareStatus status = wf::CheckMemory(memory, MemoryBytes(sampleRate, maxFrames));
        if (status != wf::PrepareStatus::Ok) return status;
        // Writing all of it shows the host's block is really this large.
        std::memset(memory.data, 0, memory.bytes);
        gainDb_ = 0.0f;
        linearGain_ = 1.0f;
        mute_ = false;
        curve_ = 0.0f;
        resets_ = 0;
        lastNote_ = -1;
        lastVelocity_ = -1.0f;
        prepared_ = true;
        return wf::PrepareStatus::Ok;
    }

    void Handle(const wf::ControlEvent& event) noexcept override {
        ++handled_;
        if (!prepared_) return;
        switch (event.type) {
            case wf::ControlEvent::Type::Param:
                if (event.id == kGain) {
                    gainDb_ = event.value;
                    linearGain_ = std::pow(10.0f, gainDb_ / 20.0f);
                } else if (event.id == kMute) {
                    mute_ = event.value != 0.0f;
                } else if (event.id == kCurve) {
                    curve_ = event.value;
                }
                break;
            case wf::ControlEvent::Type::Trigger:
                if (event.id == kReset) ++resets_;
                break;
            case wf::ControlEvent::Type::NoteOn:
            case wf::ControlEvent::Type::NoteOff:
                lastNote_ = event.id;
                lastVelocity_ = event.value;
                break;
        }
    }

    float ParamValue(wf::ParamId id) const noexcept override {
        switch (id) {
            case kGain: return gainDb_;
            case kMute: return mute_ ? 1.0f : 0.0f;
            case kCurve: return curve_;
            case kLinear: return linearGain_;
            default: return 0.0f;
        }
    }

    void Process(const float* inLeft, const float* inRight, float* outLeft, float* outRight,
                 std::size_t frames) noexcept override {
        const float gain = !prepared_ || mute_ ? 0.0f : linearGain_;
        for (std::size_t i = 0; i < frames; ++i) {
            outLeft[i] = inLeft[i] * gain;
            outRight[i] = inRight[i] * gain;
        }
    }

    float gainDb_ = 0.0f;
    float linearGain_ = 1.0f;
    bool mute_ = false;
    float curve_ = 0.0f;
    int resets_ = 0;
    int lastNote_ = -1;
    float lastVelocity_ = -1.0f;
    int handled_ = 0;  // calls, prepared or not
    bool prepared_ = false;

private:
    static const wf::ParamSpec kSpecs[];
    static const wf::ParamTable kTable;
};

const char* const kCurveLabels[] = {"Linear", "Square", "Cube"};

const wf::ParamSpec GainEngine::kSpecs[] = {
    {kGain, "Gain", wf::ParamKind::Continuous, -60.0f, 12.0f, 0.0f, "dB", nullptr},
    {kMute, "Mute", wf::ParamKind::Toggle, 0.0f, 1.0f, 0.0f, "", nullptr},
    {kCurve, "Curve", wf::ParamKind::Choice, 0.0f, 2.0f, 0.0f, "", kCurveLabels},
    {kReset, "Reset", wf::ParamKind::Trigger, 0.0f, 1.0f, 0.0f, "", nullptr},
    {kLinear, "Linear", wf::ParamKind::Readout, 0.0f, 4.0f, 1.0f, "", nullptr},
};
const wf::ParamTable GainEngine::kTable = wf::MakeParamTable(GainEngine::kSpecs);

using Type = wf::ControlEvent::Type;

wf::ControlEvent Event(Type type, std::uint8_t target, wf::ParamId id, float value) {
    return wf::ControlEvent{type, target, id, value};
}

constexpr std::size_t kMaxFrames = 64;
alignas(wf::kMemoryAlignment) unsigned char arena[1024];

void CheckSlotsAndMemory() {
    wf::SoloRig<GainEngine> rig;
    wf::Rig& base = rig;

    Expect(wf::IsValidParamTable(rig.Get().Params()), "the gain engine's table is valid");
    Expect(std::strcmp(base.Name(), "Gain") == 0, "a solo rig takes its engine's name");
    Expect(base.SlotCount() == 1, "a solo rig has one slot");
    Expect(base.Slot(0) == &rig.Get(), "slot 0 is the engine");
    Expect(base.Slot(1) == nullptr, "there is no slot 1");
    const wf::Rig& constBase = rig;
    Expect(std::strcmp(constBase.Slot(0)->Name(), "Gain") == 0,
           "a const rig gives read access to its engines");

    const std::size_t bytes = base.MemoryBytes(48000.0f, kMaxFrames);
    Expect(bytes == kMaxFrames * GainEngine::kMemoryPerFrame, "the rig reports its engine's memory");
    Expect(bytes % wf::kMemoryAlignment == 0, "memory sizes are aligned");
    Expect(wf::AlignMemory(1) == wf::kMemoryAlignment && wf::AlignMemory(16) == 16 &&
               wf::AlignMemory(0) == 0,
           "AlignMemory rounds up to the alignment");

    Expect(base.Prepare(48000.0f, kMaxFrames, wf::MemoryBlock{nullptr, 0}) ==
               wf::PrepareStatus::MemoryTooSmall,
           "no memory is too small");
    Expect(base.Prepare(48000.0f, kMaxFrames, wf::MemoryBlock{arena, bytes - 1}) ==
               wf::PrepareStatus::MemoryTooSmall,
           "one byte short is too small");
    Expect(base.Prepare(48000.0f, kMaxFrames, wf::MemoryBlock{arena + 4, bytes}) ==
               wf::PrepareStatus::MemoryMisaligned,
           "a misaligned block is refused");
    Expect(base.Prepare(48000.0f, 0, wf::MemoryBlock{arena, sizeof(arena)}) ==
               wf::PrepareStatus::UnsupportedBlockSize,
           "the engine's own refusals come through the rig");
    Expect(base.Prepare(48000.0f, kMaxFrames, wf::MemoryBlock{arena, bytes}) == wf::PrepareStatus::Ok,
           "exactly enough aligned memory prepares");
}

// Every event a rig accepts or drops, and what reaches the engine.
void CheckHandle() {
    wf::SoloRig<GainEngine> rig;
    wf::Rig& base = rig;
    GainEngine& engine = rig.Get();
    Expect(base.Prepare(48000.0f, kMaxFrames, wf::MemoryBlock{arena, sizeof(arena)}) ==
               wf::PrepareStatus::Ok,
           "the rig prepares");

    Expect(base.Handle(Event(Type::Param, 0, GainEngine::kGain, -6.0f)) && engine.gainDb_ == -6.0f,
           "a valid value reaches the engine unchanged");
    Expect(base.Handle(Event(Type::Param, 0, GainEngine::kGain, 30.0f)) && engine.gainDb_ == 12.0f,
           "a value above the range arrives clamped");
    Expect(base.Handle(Event(Type::Param, 0, GainEngine::kCurve, 1.6f)) && engine.curve_ == 2.0f,
           "a Choice arrives as an index");
    Expect(base.Handle(Event(Type::Param, 0, GainEngine::kMute, 0.7f)) && engine.mute_,
           "a Toggle arrives as on or off");
    Expect(base.Handle(Event(Type::Trigger, 0, GainEngine::kReset, 0.0f)) && engine.resets_ == 1,
           "a trigger reaches the engine");
    Expect(base.Handle(Event(Type::NoteOn, 0, 60, 1.5f)) && engine.lastNote_ == 60 &&
               engine.lastVelocity_ == 1.0f,
           "a note arrives with its velocity clamped to 0..1");

    const int handled = engine.handled_;
    const struct {
        wf::ControlEvent event;
        const char* description;
    } dropped[] = {
        {Event(Type::Param, 1, GainEngine::kGain, 0.0f), "an event for a missing slot"},
        {Event(Type::Param, 0, 9, 0.0f), "an unknown parameter"},
        {Event(Type::Param, 0, GainEngine::kGain, FromBits(0x7fc00000u)), "a NaN value"},
        {Event(Type::Param, 0, GainEngine::kGain, FromBits(0xff800000u)), "an infinite value"},
        {Event(Type::Param, 0, GainEngine::kReset, 1.0f), "a Param event for a Trigger parameter"},
        {Event(Type::Trigger, 0, GainEngine::kGain, 1.0f), "a Trigger event for a Continuous parameter"},
        {Event(Type::Param, 0, GainEngine::kLinear, 1.0f), "a Param event for a Readout"},
        {Event(Type::Trigger, 0, GainEngine::kLinear, 1.0f), "a Trigger event for a Readout"},
        {Event(Type::NoteOn, 0, 128, 1.0f), "a note number above 127"},
        {Event(Type::NoteOff, 0, 60, FromBits(0x7fc00000u)), "a NaN velocity"},
    };
    for (const auto& d : dropped) {
        if (base.Handle(d.event)) {
            std::cerr << "FAIL: the rig accepted " << d.description << '\n';
            ++failures;
        }
    }
    Expect(engine.handled_ == handled, "dropped events never reach the engine");
    Expect(engine.gainDb_ == 12.0f, "dropped events leave the parameters alone");
}

// What a host's panel reads back after the callback's events.
void CheckParamValue() {
    wf::SoloRig<GainEngine> rig;
    wf::Rig& base = rig;
    Expect(base.Prepare(48000.0f, kMaxFrames, wf::MemoryBlock{arena, sizeof(arena)}) ==
               wf::PrepareStatus::Ok,
           "the rig prepares");
    const wf::Engine& engine = *base.Slot(0);

    Expect(engine.ParamValue(GainEngine::kGain) == 0.0f && engine.ParamValue(GainEngine::kMute) == 0.0f &&
               engine.ParamValue(GainEngine::kCurve) == 0.0f,
           "values start at the defaults");
    base.Handle(Event(Type::Param, 0, GainEngine::kGain, 30.0f));
    base.Handle(Event(Type::Param, 0, GainEngine::kCurve, 1.6f));
    base.Handle(Event(Type::Param, 0, GainEngine::kMute, 0.7f));
    Expect(engine.ParamValue(GainEngine::kGain) == 12.0f, "a value reads back as constrained");
    Expect(engine.ParamValue(GainEngine::kCurve) == 2.0f &&
               std::strcmp(engine.Params().Find(GainEngine::kCurve)->labels[2], "Cube") == 0,
           "a Choice reads back as an index into its labels");
    Expect(engine.ParamValue(GainEngine::kMute) == 1.0f, "a Toggle reads back as 0 or 1");
    Expect(std::abs(engine.ParamValue(GainEngine::kLinear) - 3.981f) < 0.001f,
           "a Readout reads back what the engine reports");
    Expect(engine.ParamValue(GainEngine::kReset) == 0.0f && engine.ParamValue(9) == 0.0f,
           "triggers and unknown IDs read 0");
}

// A failed Prepare() leaves the engine silent and deaf, so a host can keep
// its callback running around it.
void CheckLifecycle() {
    wf::SoloRig<GainEngine> rig;
    wf::Rig& base = rig;
    GainEngine& engine = rig.Get();

    float left[kMaxFrames], right[kMaxFrames];
    const auto fillAndProcess = [&] {
        for (std::size_t i = 0; i < kMaxFrames; ++i) left[i] = right[i] = 0.5f;
        base.Process(left, right, left, right, kMaxFrames);
        bool silent = true;
        for (std::size_t i = 0; i < kMaxFrames; ++i) {
            silent = silent && left[i] == 0.0f && right[i] == 0.0f;
        }
        return silent;
    };

    Expect(fillAndProcess(), "an engine never prepared outputs silence");
    Expect(base.Prepare(48000.0f, kMaxFrames, wf::MemoryBlock{arena, sizeof(arena)}) ==
               wf::PrepareStatus::Ok,
           "the rig prepares");
    Expect(!fillAndProcess(), "a prepared engine passes audio");
    Expect(base.Prepare(48000.0f, kMaxFrames, wf::MemoryBlock{nullptr, 0}) ==
               wf::PrepareStatus::MemoryTooSmall,
           "a second Prepare() fails");
    Expect(fillAndProcess(), "after a failed Prepare() the engine outputs silence");
    base.Handle(Event(Type::Param, 0, GainEngine::kGain, -6.0f));
    Expect(engine.gainDb_ == 0.0f, "after a failed Prepare() the engine ignores events");
}

void CheckProcess() {
    wf::SoloRig<GainEngine> rig;
    wf::Rig& base = rig;
    Expect(base.Prepare(48000.0f, kMaxFrames, wf::MemoryBlock{arena, sizeof(arena)}) ==
               wf::PrepareStatus::Ok,
           "the rig prepares");
    Expect(base.Handle(Event(Type::Param, 0, GainEngine::kGain, -6.0f)), "gain set");

    float inLeft[kMaxFrames], inRight[kMaxFrames];
    for (std::size_t i = 0; i < kMaxFrames; ++i) {
        inLeft[i] = static_cast<float>(i) / kMaxFrames;
        inRight[i] = -inLeft[i];
    }
    float outLeft[kMaxFrames], outRight[kMaxFrames];
    base.Process(inLeft, inRight, outLeft, outRight, kMaxFrames);

    const float gain = std::pow(10.0f, -6.0f / 20.0f);
    bool applied = true;
    for (std::size_t i = 0; i < kMaxFrames; ++i) {
        applied = applied && outLeft[i] == inLeft[i] * gain && outRight[i] == inRight[i] * gain;
    }
    Expect(applied, "Process applies the engine to every frame");

    // In place, as Bela processes: the output buffers are the inputs.
    float left[kMaxFrames], right[kMaxFrames];
    std::memcpy(left, inLeft, sizeof(left));
    std::memcpy(right, inRight, sizeof(right));
    base.Process(left, right, left, right, kMaxFrames);
    Expect(std::memcmp(left, outLeft, sizeof(left)) == 0 &&
               std::memcmp(right, outRight, sizeof(right)) == 0,
           "processing in place gives the same output");

    Expect(base.Handle(Event(Type::Param, 0, GainEngine::kMute, 1.0f)), "mute set");
    base.Process(inLeft, inRight, outLeft, outRight, kMaxFrames);
    bool silent = true;
    for (std::size_t i = 0; i < kMaxFrames; ++i) {
        silent = silent && outLeft[i] == 0.0f && outRight[i] == 0.0f;
    }
    Expect(silent, "an event takes effect from the next Process()");
}

}  // namespace

int main() {
    CheckSlotsAndMemory();
    CheckHandle();
    CheckParamValue();
    CheckLifecycle();
    CheckProcess();

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
