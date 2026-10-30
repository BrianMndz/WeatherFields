#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "wf/ControlInput.h"
#include "wf/Rig.h"

namespace {

int failures = 0;

void Expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

constexpr std::size_t kCapacity = 8;
using Input = wf::ControlInput<kCapacity>;

// Records the parameter IDs it receives. With `refill` set, every event it
// handles pushes another into that input, as a producer racing the drain
// would.
class RecordingEngine final : public wf::Engine {
public:
    static constexpr std::size_t kMaxRecorded = 32;

    const char* Name() const noexcept override { return "Recorder"; }
    const wf::ParamTable& Params() const noexcept override { return kTable; }
    std::size_t MemoryBytes(float, std::size_t) const noexcept override { return 0; }

    wf::PrepareStatus Prepare(float, std::size_t, wf::MemoryBlock) noexcept override {
        count_ = 0;
        prepared_ = true;
        return wf::PrepareStatus::Ok;
    }

    void Handle(const wf::ControlEvent& event) noexcept override {
        if (!prepared_) return;
        if (count_ < kMaxRecorded) ids_[count_++] = event.id;
        if (refill != nullptr) refill->Push(event);
    }

    float ParamValue(wf::ParamId) const noexcept override { return 0.0f; }

    void Process(const float*, const float*, float* outLeft, float* outRight,
                 std::size_t frames) noexcept override {
        std::memset(outLeft, 0, frames * sizeof(float));
        std::memset(outRight, 0, frames * sizeof(float));
    }

    std::size_t Count() const noexcept { return count_; }
    wf::ParamId Id(std::size_t i) const noexcept { return ids_[i]; }

    Input* refill = nullptr;

private:
    static const wf::ParamSpec kSpecs[];
    static const wf::ParamTable kTable;

    wf::ParamId ids_[kMaxRecorded] = {};
    std::size_t count_ = 0;
    bool prepared_ = false;
};

// Ten Continuous parameters, IDs 0 to 9.
const wf::ParamSpec RecordingEngine::kSpecs[] = {
    {0, "P0", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.0f, "", nullptr},
    {1, "P1", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.0f, "", nullptr},
    {2, "P2", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.0f, "", nullptr},
    {3, "P3", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.0f, "", nullptr},
    {4, "P4", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.0f, "", nullptr},
    {5, "P5", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.0f, "", nullptr},
    {6, "P6", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.0f, "", nullptr},
    {7, "P7", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.0f, "", nullptr},
    {8, "P8", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.0f, "", nullptr},
    {9, "P9", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.0f, "", nullptr},
};
const wf::ParamTable RecordingEngine::kTable = wf::MakeParamTable(RecordingEngine::kSpecs);

wf::ControlEvent Param(wf::ParamId id) {
    return wf::ControlEvent{wf::ControlEvent::Type::Param, 0, id, 0.5f};
}

void Prepare(wf::Rig& rig) {
    Expect(rig.Prepare(48000.0f, 32, wf::MemoryBlock{nullptr, 0}) == wf::PrepareStatus::Ok,
           "the rig prepares");
}

void CheckOrderAndLimit() {
    wf::SoloRig<RecordingEngine> rig;
    Prepare(rig);
    Input input;

    for (wf::ParamId id = 0; id < 5; ++id) input.Push(Param(id));
    Expect(input.Drain(rig, 3) == 3, "a drain stops at maxEvents");
    Expect(input.Drain(rig, 3) == 2, "the next drain takes the rest");
    Expect(input.Drain(rig, 3) == 0, "an empty input drains nothing");

    const RecordingEngine& engine = rig.Get();
    bool inOrder = engine.Count() == 5;
    for (std::size_t i = 0; inOrder && i < 5; ++i) inOrder = engine.Id(i) == i;
    Expect(inOrder, "events reach the rig in the order pushed");
}

// A producer pushing while the callback drains must not keep it there.
void CheckRefillDoesNotStretch() {
    wf::SoloRig<RecordingEngine> rig;
    Prepare(rig);
    Input input;
    rig.Get().refill = &input;

    for (wf::ParamId id = 0; id < 4; ++id) input.Push(Param(id));
    Expect(input.Drain(rig, 100) == 4, "a drain takes only what was queued when it began");
    Expect(rig.Get().Count() == 4, "four events handled");
    rig.Get().refill = nullptr;
    Expect(input.Drain(rig, 100) == 4, "what arrived meanwhile waits for the next drain");
}

void CheckCounters() {
    wf::SoloRig<RecordingEngine> rig;
    Prepare(rig);
    Input input;

    for (std::size_t i = 0; i < kCapacity; ++i) {
        Expect(input.Push(Param(0)), "pushes up to the capacity succeed");
    }
    Expect(!input.Push(Param(0)) && !input.Push(Param(0)), "pushes into a full input fail");
    Expect(input.Overflows() == 2, "each refused push counts an overflow");
    Expect(input.Discard() == kCapacity, "Discard() drops everything queued");
    Expect(input.Drain(rig, 100) == 0 && rig.Get().Count() == 0, "nothing is left to drain");

    input.Push(Param(1));
    input.Push(Param(42));  // no such parameter
    input.Push(wf::ControlEvent{wf::ControlEvent::Type::Param, 3, 1, 0.5f});  // no such slot
    input.Push(Param(2));
    input.Push(Param(3));
    wf::ParamId taken[8] = {};
    std::size_t takenCount = 0;
    Expect(input.Drain(rig, 4, [&](const wf::ControlEvent& event) noexcept {
               if (takenCount < 8) taken[takenCount] = event.id;
               ++takenCount;
           }) == 4,
           "dropped events still leave the queue");
    Expect(takenCount == 4 && taken[0] == 1 && taken[1] == 42 && taken[2] == 1 && taken[3] == 2,
           "onTaken sees each event taken, dropped ones too, in order, and not the one left");
    Expect(input.Drain(rig, 100) == 1, "the event past maxEvents waits");
    Expect(input.Dropped() == 2, "events the rig drops are counted");
    Expect(rig.Get().Count() == 3 && rig.Get().Id(0) == 1 && rig.Get().Id(1) == 2 && rig.Get().Id(2) == 3,
           "the valid events around them arrive");
}

}  // namespace

int main() {
    CheckOrderAndLimit();
    CheckRefillDoesNotStretch();
    CheckCounters();

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
