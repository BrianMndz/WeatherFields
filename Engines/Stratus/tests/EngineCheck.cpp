#include <iostream>
#include <limits>
#include <memory>

#include "wf/Rig.h"
#include "wf/stratus/Engine.h"

namespace {

using wf::stratus::Engine;
using Rig = wf::SoloRig<Engine>;
using Type = wf::ControlEvent::Type;
constexpr float kSampleRate = 48000.0f;
constexpr std::size_t kFrames = 32;

int failures = 0;

void Expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void Send(Rig& rig, wf::ParamId id, float value) {
    Expect(rig.Handle({Type::Param, 0, id, value}), "parameter event accepted by rig");
}

void Clear(Rig& rig) {
    Expect(rig.Handle({Type::Trigger, 0, Engine::kClear, 0}), "Clear trigger accepted by rig");
}

void Step(Rig& rig) {
    float left[kFrames];
    float right[kFrames];
    for (std::size_t i = 0; i < kFrames; ++i) {
        left[i] = 0.5f;
        right[i] = -0.25f;
    }
    rig.Process(left, right, left, right, kFrames);
    bool silent = true;
    for (std::size_t i = 0; i < kFrames; ++i) {
        silent = silent && left[i] == 0.0f && right[i] == 0.0f;
    }
    Expect(silent, "Process stub outputs silence on both channels");
}

enum class State { Empty, Recording, Playing, Overdubbing, Stopped };

void ExpectState(const Rig& rig, State state, const char* description) {
    const float play = state == State::Empty || state == State::Stopped ? 0.0f : 1.0f;
    const float record = state == State::Recording || state == State::Overdubbing ? 1.0f : 0.0f;
    Expect(rig.Get().ParamValue(Engine::kPlay) == play &&
               rig.Get().ParamValue(Engine::kRecord) == record,
           description);
}

void EnterState(Rig& rig, State state) {
    Clear(rig);
    if (state == State::Empty) return;
    Send(rig, Engine::kRecord, 1);
    Step(rig);
    if (state == State::Recording) return;
    Send(rig, Engine::kRecord, 0);
    if (state == State::Overdubbing) Send(rig, Engine::kRecord, 1);
    if (state == State::Stopped) Send(rig, Engine::kPlay, 0);
}

void CheckTransitions(Rig& rig) {
    struct Row {
        State from;
        State recordOn;
        State recordOff;
        State playOn;
        State playOff;
    };
    const Row rows[] = {
        {State::Empty, State::Recording, State::Empty, State::Empty, State::Empty},
        {State::Recording, State::Recording, State::Playing, State::Recording, State::Stopped},
        {State::Playing, State::Overdubbing, State::Playing, State::Playing, State::Stopped},
        {State::Overdubbing, State::Overdubbing, State::Playing, State::Overdubbing,
         State::Stopped},
        {State::Stopped, State::Overdubbing, State::Stopped, State::Playing, State::Stopped},
    };
    for (const Row& row : rows) {
        for (wf::ParamId id : {Engine::kRecord, Engine::kPlay}) {
            for (bool on : {false, true}) {
                EnterState(rig, row.from);
                const State expected = id == Engine::kRecord ? (on ? row.recordOn : row.recordOff)
                                                             : (on ? row.playOn : row.playOff);
                Send(rig, id, on ? 1.0f : 0.0f);
                ExpectState(rig, expected, "transition matches the Record/Play table");
                Send(rig, id, on ? 1.0f : 0.0f);
                ExpectState(rig, expected, "duplicate event preserves readback");

                // Follow-up events distinguish Empty from Stopped.
                if (expected == State::Recording || expected == State::Overdubbing) {
                    if (row.from == State::Empty) Step(rig);
                    Send(rig, Engine::kRecord, 0);
                    ExpectState(rig, State::Playing, "ending recording leaves a playable loop");
                } else {
                    Send(rig, Engine::kPlay, 1);
                    ExpectState(rig, expected == State::Empty ? State::Empty : State::Playing,
                                "Play refuses Empty and restarts an existing loop");
                }
            }
        }
        EnterState(rig, row.from);
        Send(rig, Engine::kLoopLevel, -12);
        Send(rig, Engine::kThru, 0);
        Clear(rig);
        Clear(rig);
        ExpectState(rig, State::Empty, "Clear from every state, including repeated Clear");
        Send(rig, Engine::kPlay, 1);
        ExpectState(rig, State::Empty, "Clear leaves no loop to restart");
        Expect(rig.Get().ParamValue(Engine::kLoopLevel) == -12 &&
                   rig.Get().ParamValue(Engine::kThru) == 0,
               "Clear preserves Loop Level and Thru");
    }
}

void CheckZeroFrameTakes(Rig& rig) {
    for (wf::ParamId end : {Engine::kRecord, Engine::kPlay}) {
        Clear(rig);
        Send(rig, Engine::kRecord, 1);
        Send(rig, Engine::kRecord, 1);
        Send(rig, Engine::kPlay, 1);
        float sample = 1;
        rig.Process(&sample, &sample, &sample, &sample, 0);
        Send(rig, end, 0);
        Send(rig, Engine::kPlay, 1);
        ExpectState(rig, State::Empty, "ending a zero-frame take leaves Empty");
        Send(rig, Engine::kRecord, 1);
        Step(rig);
        Send(rig, Engine::kRecord, 0);
        ExpectState(rig, State::Playing, "a new take works after discarding zero frames");
    }
}

void CheckEvents(Rig& rig) {
    Clear(rig);
    Send(rig, Engine::kThru, 0.49f);
    Expect(rig.Get().ParamValue(Engine::kThru) == 0, "rig constrains Toggle below 0.5 to off");
    Send(rig, Engine::kThru, 0.5f);
    Expect(rig.Get().ParamValue(Engine::kThru) == 1, "rig constrains Toggle at 0.5 to on");
    Send(rig, Engine::kLoopLevel, -12);
    Expect(rig.Get().ParamValue(Engine::kLoopLevel) == -12, "Loop Level reads back dB");
    const wf::ParamSpec& level = *rig.Get().Params().Find(Engine::kLoopLevel);
    Send(rig, Engine::kLoopLevel, level.min - 10);
    Expect(rig.Get().ParamValue(Engine::kLoopLevel) == level.min, "Loop Level clamps at bottom");
    Send(rig, Engine::kLoopLevel, level.max + 10);
    Expect(rig.Get().ParamValue(Engine::kLoopLevel) == level.max, "Loop Level clamps at top");
    Expect(
        !rig.Handle({Type::Param, 0, Engine::kLoopLevel, std::numeric_limits<float>::quiet_NaN()}),
        "rig drops NaN");
    Expect(!rig.Handle({Type::Param, 0, Engine::kClear, 1}), "Clear requires Trigger type");
    Expect(!rig.Handle({Type::Trigger, 0, Engine::kPlay, 1}), "Play requires Param type");
    Expect(!rig.Handle({Type::Param, 0, 999, 1}), "unknown parameter is rejected");
    Expect(rig.Get().ParamValue(999) == 0 && rig.Get().ParamValue(Engine::kClear) == 0,
           "unknown and Trigger parameters read zero");
    EnterState(rig, State::Recording);
    Expect(rig.Handle({Type::NoteOn, 0, Engine::kPlay, 1}), "valid NoteOn passes rig");
    Expect(rig.Handle({Type::NoteOff, 0, Engine::kRecord, 0}), "valid NoteOff passes rig");
    ExpectState(rig, State::Recording, "notes do not change loop state");
}

}  // namespace

int main() {
    std::unique_ptr<unsigned char[]> memory;
    Rig rig;
    const Engine& engine = rig.Get();
    Expect(wf::IsValidParamTable(engine.Params()), "Stratus parameter table is valid");

    const wf::ParamTable& table = engine.Params();
    Expect(table.count == 5, "Stratus exposes five parameters");
    for (std::size_t i = 0; i < table.count; ++i) {
        Expect(table.specs[i].id == i, "parameters are in ID order");
    }

    const wf::ParamSpec* thru = table.Find(wf::stratus::Engine::kThru);
    Expect(thru != nullptr && thru->def == 1.0f, "Thru defaults to on");
    const wf::ParamSpec* record = table.Find(wf::stratus::Engine::kRecord);
    Expect(record != nullptr && record->def == 0.0f, "Record defaults to off");

    Send(rig, Engine::kRecord, 1);
    Send(rig, Engine::kLoopLevel, -12);
    Send(rig, Engine::kThru, 0);
    Step(rig);
    ExpectState(rig, State::Empty, "events before Prepare are ignored");
    Expect(engine.ParamValue(Engine::kLoopLevel) == 0 && engine.ParamValue(Engine::kThru) == 1,
           "events before Prepare leave mix defaults unchanged");

    const std::size_t bytes = rig.MemoryBytes(kSampleRate, kFrames);
    memory.reset(new unsigned char[bytes]);
    const wf::MemoryBlock block{memory.get(), bytes};
    if (rig.Prepare(kSampleRate, kFrames, block) != wf::PrepareStatus::Ok) {
        std::cerr << "FAIL: rig prepares with its requested heap block\n";
        return 1;
    }
    for (std::size_t i = 0; i < table.count; ++i) {
        const wf::ParamSpec& spec = table.specs[i];
        Expect(engine.ParamValue(spec.id) == spec.def, "each parameter reads its default");
    }
    CheckTransitions(rig);
    CheckZeroFrameTakes(rig);
    CheckEvents(rig);

    Expect(rig.Prepare(kSampleRate, kFrames, {nullptr, 0}) == wf::PrepareStatus::MemoryTooSmall,
           "failed Prepare rejects a missing heap block");
    Send(rig, Engine::kRecord, 0);
    Clear(rig);
    Step(rig);
    ExpectState(rig, State::Recording, "events after failed Prepare are ignored");

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
