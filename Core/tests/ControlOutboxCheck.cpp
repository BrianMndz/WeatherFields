#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "wf/ControlInput.h"
#include "wf/ControlOutbox.h"
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
using Outbox = wf::ControlOutbox<kCapacity>;

// Clouds' side effect in miniature: changing Mode releases Hold, as a Clouds
// quality change releases freeze. Records every event it handles.
class ModeEngine final : public wf::Engine {
public:
    enum : wf::ParamId { kLevel = 0, kMode = 1, kHold = 2, kTap = 3 };
    static constexpr std::size_t kMaxRecorded = 64;

    static const wf::ParamTable& Table() noexcept { return kTable; }

    const char* Name() const noexcept override { return "Mode"; }
    const wf::ParamTable& Params() const noexcept override { return kTable; }
    std::size_t MemoryBytes(float, std::size_t) const noexcept override { return 0; }

    wf::PrepareStatus Prepare(float, std::size_t, wf::MemoryBlock) noexcept override {
        level_ = 0.0f;
        mode_ = 0;
        hold_ = false;
        taps_ = 0;
        count_ = 0;
        return wf::PrepareStatus::Ok;
    }

    void Handle(const wf::ControlEvent& event) noexcept override {
        if (count_ < kMaxRecorded) events_[count_++] = event;
        if (event.type == wf::ControlEvent::Type::Trigger) {
            ++taps_;
            return;
        }
        switch (event.id) {
            case kLevel: level_ = event.value; break;
            case kMode: {
                const int mode = static_cast<int>(event.value);
                if (mode != mode_) hold_ = false;
                mode_ = mode;
                break;
            }
            case kHold: hold_ = event.value != 0.0f; break;
            default: break;
        }
    }

    float ParamValue(wf::ParamId id) const noexcept override {
        switch (id) {
            case kLevel: return level_;
            case kMode: return static_cast<float>(mode_);
            case kHold: return hold_ ? 1.0f : 0.0f;
            default: return 0.0f;
        }
    }

    void Process(const float*, const float*, float* outLeft, float* outRight,
                 std::size_t frames) noexcept override {
        std::memset(outLeft, 0, frames * sizeof(float));
        std::memset(outRight, 0, frames * sizeof(float));
    }

    int Taps() const noexcept { return taps_; }
    std::size_t Count() const noexcept { return count_; }
    const wf::ControlEvent& Event(std::size_t i) const noexcept { return events_[i]; }
    void ClearRecord() noexcept { count_ = 0; }

private:
    static const char* const kModeLabels[];
    static const wf::ParamSpec kSpecs[];
    static const wf::ParamTable kTable;

    float level_ = 0.0f;
    int mode_ = 0;
    bool hold_ = false;
    int taps_ = 0;
    wf::ControlEvent events_[kMaxRecorded] = {};
    std::size_t count_ = 0;
};

const char* const ModeEngine::kModeLabels[] = {"A", "B", "C", "D"};
const wf::ParamSpec ModeEngine::kSpecs[] = {
    {kLevel, "Level", wf::ParamKind::Continuous, 0.0f, 1.0f, 0.0f, "", nullptr},
    {kMode, "Mode", wf::ParamKind::Choice, 0.0f, 3.0f, 0.0f, "", kModeLabels},
    {kHold, "Hold", wf::ParamKind::Toggle, 0.0f, 1.0f, 0.0f, "", nullptr},
    {kTap, "Tap", wf::ParamKind::Trigger, 0.0f, 1.0f, 0.0f, "", nullptr},
};
const wf::ParamTable ModeEngine::kTable = wf::MakeParamTable(ModeEngine::kSpecs);

// Sends a Param or Trigger event the way a host's GUI does, with the
// parameter's kind from the engine's table; true if it replaced one waiting.
bool Send(Outbox& outbox, wf::ParamId id, float value) {
    const wf::ParamSpec* spec = ModeEngine::Table().Find(id);
    const bool trigger = spec->kind == wf::ParamKind::Trigger;
    return outbox.Send(wf::ControlEvent{trigger ? wf::ControlEvent::Type::Trigger : wf::ControlEvent::Type::Param, 0,
                                 id, value},
                spec->kind);
}

struct Fixture {
    Fixture() {
        static_cast<wf::Rig&>(rig).Prepare(48000.0f, 32, wf::MemoryBlock{nullptr, 0});
    }

    // Fills the input with events that change nothing: Mode set to its
    // current value, which Choice events never coalesce away.
    void Fill() {
        for (std::size_t i = 0; i < kCapacity; ++i) {
            Send(outbox, ModeEngine::kMode, rig.Get().ParamValue(ModeEngine::kMode));
        }
    }

    // The audio thread catching up: drains, lets the outbox refill the
    // input, and drains again until nothing waits.
    void CatchUp() {
        for (int i = 0; i < 16; ++i) {
            input.Drain(rig, kCapacity);
            outbox.Flush();
        }
        input.Drain(rig, kCapacity);
    }

    float Value(wf::ParamId id) const { return rig.Get().ParamValue(id); }

    wf::SoloRig<ModeEngine> rig;
    Input input;
    Outbox outbox{input};
};

void CheckImmediate() {
    Fixture f;
    Send(f.outbox, ModeEngine::kLevel, 0.5f);
    Expect(f.outbox.Empty(), "with room in the input nothing waits");
    f.input.Drain(f.rig, kCapacity);
    Expect(f.Value(ModeEngine::kLevel) == 0.5f, "the event arrives");
}

// The first review: Mode then Hold must not become Hold then Mode.
void CheckOrderKept() {
    Fixture f;
    f.Fill();
    Send(f.outbox, ModeEngine::kMode, 1.0f);
    Send(f.outbox, ModeEngine::kHold, 1.0f);
    Expect(!f.outbox.Empty(), "a full input makes events wait");
    f.CatchUp();
    Expect(f.Value(ModeEngine::kMode) == 1.0f && f.Value(ModeEngine::kHold) == 1.0f,
           "Mode then Hold arrive in that order: Hold stays on");
}

// The second review: Mode 1, Hold, Mode 0. Coalescing the Modes would leave
// Hold on; as sent, the last Mode change releases it.
void CheckChoicesNotCoalesced() {
    Fixture f;
    f.Fill();
    Send(f.outbox, ModeEngine::kMode, 1.0f);
    Send(f.outbox, ModeEngine::kHold, 1.0f);
    Expect(!Send(f.outbox, ModeEngine::kMode, 0.0f), "a Choice never replaces a waiting one");
    f.CatchUp();
    Expect(f.Value(ModeEngine::kMode) == 0.0f && f.Value(ModeEngine::kHold) == 0.0f,
           "every Choice change arrives: the last one releases Hold");
}

void CheckTriggersKept() {
    Fixture f;
    f.Fill();
    Send(f.outbox, ModeEngine::kTap, 1.0f);
    Send(f.outbox, ModeEngine::kTap, 1.0f);
    f.CatchUp();
    Expect(f.rig.Get().Taps() == 2, "both taps made while the input was full arrive");
}

void CheckContinuousCoalesced() {
    Fixture f;
    f.Fill();
    Expect(!Send(f.outbox, ModeEngine::kLevel, 0.1f), "the first Level replaces nothing");
    Send(f.outbox, ModeEngine::kMode, 1.0f);
    Expect(Send(f.outbox, ModeEngine::kLevel, 0.2f) && Send(f.outbox, ModeEngine::kLevel, 0.3f),
           "later Levels report replacing the waiting one");

    float latest = 0.0f;
    Expect(f.outbox.IsWaiting(0, ModeEngine::kLevel), "Level waits");
    Expect(f.outbox.LatestWaiting(0, ModeEngine::kLevel, latest) && latest == 0.3f,
           "the latest waiting Level is 0.3");
    Expect(!f.outbox.IsWaiting(0, ModeEngine::kHold) && !f.outbox.LatestWaiting(0, ModeEngine::kHold, latest),
           "Hold does not wait");

    f.input.Drain(f.rig, kCapacity);  // the fill
    f.rig.Get().ClearRecord();
    f.CatchUp();
    const ModeEngine& engine = f.rig.Get();
    Expect(engine.Count() == 2, "three Level values coalesce into one event");
    Expect(engine.Count() == 2 && engine.Event(0).id == ModeEngine::kMode && engine.Event(1).id == ModeEngine::kLevel &&
               engine.Event(1).value == 0.3f,
           "the coalesced Level moves behind the Mode sent before its latest value");
}

void CheckClear() {
    Fixture f;
    f.Fill();
    Send(f.outbox, ModeEngine::kHold, 1.0f);
    f.outbox.Clear();
    Expect(f.outbox.Empty(), "Clear() drops what waits");
    f.CatchUp();
    Expect(f.Value(ModeEngine::kHold) == 0.0f, "a cleared event never arrives");
}

}  // namespace

int main() {
    CheckImmediate();
    CheckOrderKept();
    CheckChoicesNotCoalesced();
    CheckTriggersKept();
    CheckContinuousCoalesced();
    CheckClear();

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
