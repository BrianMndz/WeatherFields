#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

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

bool Step(Rig& rig) {
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
    return silent;
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

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;

    explicit Stereo(std::size_t frames, float value = 0)
        : left(frames, value), right(frames, value) {}
};

Stereo Pattern(std::size_t frames, std::size_t offset = 0) {
    Stereo audio(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const std::size_t index = offset + i;
        audio.left[i] = static_cast<float>(static_cast<int>(index % 251) - 125) / 256.0f;
        audio.right[i] = static_cast<float>(static_cast<int>((3 * index) % 241) - 120) / 256.0f;
    }
    return audio;
}

Stereo Slice(const Stereo& audio, std::size_t offset, std::size_t frames) {
    Stereo part(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        part.left[i] = audio.left[offset + i];
        part.right[i] = audio.right[offset + i];
    }
    return part;
}

Stereo Render(Rig& rig, const Stereo& input, bool inPlace = false) {
    Stereo output = inPlace ? input : Stereo(input.left.size(), 123.0f);
    rig.Process(inPlace ? output.left.data() : input.left.data(),
                inPlace ? output.right.data() : input.right.data(), output.left.data(),
                output.right.data(), input.left.size());
    return output;
}

void ExpectAudio(const Stereo& actual, const Stereo& expected, const char* description,
                 float tolerance = 0) {
    bool equal = actual.left.size() == expected.left.size();
    for (std::size_t i = 0; equal && i < expected.left.size(); ++i) {
        equal = std::abs(actual.left[i] - expected.left[i]) <= tolerance &&
                std::abs(actual.right[i] - expected.right[i]) <= tolerance;
        if (!equal) std::cerr << "at frame " << i << ": ";
    }
    Expect(equal, description);
}

Stereo Repeat(const Stereo& loop, std::size_t frames, float gain = 1) {
    Stereo expected(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        expected.left[i] = loop.left[i % loop.left.size()] * gain;
        expected.right[i] = loop.right[i % loop.right.size()] * gain;
    }
    return expected;
}

void Reset(Rig& rig) {
    Clear(rig);
    Send(rig, Engine::kLoopLevel, 0);
    Send(rig, Engine::kThru, 1);
}

void Restart(Rig& rig) {
    Send(rig, Engine::kPlay, 0);
    Send(rig, Engine::kPlay, 1);
}

void CheckPlayback(Rig& rig) {
    Reset(rig);
    const Stereo take = Pattern(13);
    const Stereo live = Pattern(31, 73);
    ExpectAudio(Render(rig, live), live, "Empty passes input with Thru on");
    Send(rig, Engine::kThru, 0);
    ExpectAudio(Render(rig, live), Stereo(31), "Empty mutes input with Thru off");

    // Record raw input even with both listening paths muted. Duplicate Record
    // and Play-on events must not rewind or play the unfinished first take.
    Send(rig, Engine::kLoopLevel, -60);
    Send(rig, Engine::kRecord, 1);
    ExpectAudio(Render(rig, Slice(take, 0, 5)), Stereo(5), "first take with Thru off is silent");
    Send(rig, Engine::kRecord, 1);
    Send(rig, Engine::kPlay, 1);
    ExpectAudio(Render(rig, Slice(take, 5, 8)), Stereo(8),
                "first take continues after duplicate events");
    Send(rig, Engine::kRecord, 0);
    Send(rig, Engine::kLoopLevel, 0);
    ExpectAudio(Render(rig, live), Repeat(take, 31), "loop plays raw recorded samples and wraps");

    Send(rig, Engine::kPlay, 0);
    ExpectAudio(Render(rig, live), Stereo(31), "Stopped with Thru off is silent");
    Send(rig, Engine::kThru, 1);
    ExpectAudio(Render(rig, live), live, "Stopped passes live input with Thru on");
    Send(rig, Engine::kThru, 0);
    Send(rig, Engine::kPlay, 1);
    ExpectAudio(Render(rig, live), Repeat(take, 31), "Play from Stopped restarts at frame zero");

    Restart(rig);
    Send(rig, Engine::kLoopLevel, -6.02f);
    ExpectAudio(Render(rig, live), Repeat(take, 31, 0.5f), "-6.02 dB is approximately half gain",
                2e-5f);
    Restart(rig);
    Send(rig, Engine::kLoopLevel, -60);
    ExpectAudio(Render(rig, live), Stereo(31), "bottom of Loop Level is exactly silent");
    Restart(rig);
    Send(rig, Engine::kLoopLevel, 0);
    Send(rig, Engine::kThru, 1);
    Stereo mix = Repeat(take, 31);
    for (std::size_t i = 0; i < 31; ++i) {
        mix.left[i] += live.left[i];
        mix.right[i] += live.right[i];
    }
    ExpectAudio(Render(rig, live), mix, "Playing mixes Thru input and the loop");

    Reset(rig);
    const Stereo shorter = Pattern(3, 41);
    Send(rig, Engine::kRecord, 1);
    ExpectAudio(Render(rig, shorter), shorter, "Recording monitors input with Thru on");
    Send(rig, Engine::kPlay, 0);
    Send(rig, Engine::kThru, 0);
    Send(rig, Engine::kPlay, 1);
    ExpectAudio(Render(rig, live), Repeat(shorter, 31),
                "new shorter take never reads stale tail after Clear");
}

void CheckOverdub(Rig& rig) {
    Reset(rig);
    const Stereo take = Pattern(13);
    Send(rig, Engine::kRecord, 1);
    Render(rig, take);
    Send(rig, Engine::kRecord, 0);
    Send(rig, Engine::kThru, 0);
    Send(rig, Engine::kLoopLevel, -60);
    Send(rig, Engine::kRecord, 1);
    const Stereo overdub = Pattern(13, 31);
    ExpectAudio(Render(rig, overdub), Stereo(13), "muted overdub still records input");
    Send(rig, Engine::kRecord, 0);
    Send(rig, Engine::kLoopLevel, 0);
    Stereo sum = take;
    for (std::size_t i = 0; i < 13; ++i) {
        sum.left[i] += overdub.left[i];
        sum.right[i] += overdub.right[i];
    }
    ExpectAudio(Render(rig, Stereo(13)), sum, "overdub adds unscaled input before Thru");

    // With Thru on, output is live input plus the old loop. The next lap
    // hears the additions made on the previous lap.
    const Stereo next = Pattern(29, 79);
    Stereo expected(29);
    for (std::size_t i = 0; i < 29; ++i) {
        const std::size_t j = i % 13;
        expected.left[i] = next.left[i] + sum.left[j];
        expected.right[i] = next.right[i] + sum.right[j];
        sum.left[j] += next.left[i];
        sum.right[j] += next.right[i];
    }
    Send(rig, Engine::kThru, 1);
    Send(rig, Engine::kRecord, 1);
    ExpectAudio(Render(rig, next), expected,
                "overdub reads old loop before adding input, across wraps");
    Send(rig, Engine::kPlay, 0);
    ExpectState(rig, State::Stopped, "stopping overdub releases Record");
    Send(rig, Engine::kThru, 0);
    Send(rig, Engine::kPlay, 1);
    ExpectAudio(Render(rig, Stereo(31)), Repeat(sum, 31),
                "overdub sums persist and restart from zero");
}

void CheckInPlace() {
    std::unique_ptr<unsigned char[]> separateMemory;
    std::unique_ptr<unsigned char[]> inPlaceMemory;
    Rig separate;
    Rig inPlace;
    const std::size_t bytes = separate.MemoryBytes(kSampleRate, kFrames);
    separateMemory.reset(new unsigned char[bytes]);
    inPlaceMemory.reset(new unsigned char[bytes]);
    Expect(separate.Prepare(kSampleRate, kFrames, {separateMemory.get(), bytes}) ==
               wf::PrepareStatus::Ok,
           "separate-buffer rig prepares");
    Expect(inPlace.Prepare(kSampleRate, kFrames, {inPlaceMemory.get(), bytes}) ==
               wf::PrepareStatus::Ok,
           "in-place rig prepares");
    const auto send = [&](wf::ParamId id, float value) {
        Send(separate, id, value);
        Send(inPlace, id, value);
    };
    const auto compare = [&](std::size_t frames, std::size_t offset) {
        const Stereo input = Pattern(frames, offset);
        const Stereo a = Render(separate, input);
        const Stereo b = Render(inPlace, input, true);
        Expect(std::memcmp(a.left.data(), b.left.data(), frames * sizeof(float)) == 0 &&
                   std::memcmp(a.right.data(), b.right.data(), frames * sizeof(float)) == 0,
               "in-place stereo output is byte-identical to separate buffers");
    };
    compare(32, 0);  // Empty
    send(Engine::kRecord, 1);
    compare(5, 1);
    send(Engine::kThru, 0);
    compare(8, 6);  // Recording
    send(Engine::kRecord, 0);
    compare(31, 44);  // Playing, with wraps
    send(Engine::kPlay, 0);
    send(Engine::kThru, 1);
    compare(32, 71);  // Stopped
    send(Engine::kPlay, 1);
    send(Engine::kRecord, 1);
    send(Engine::kLoopLevel, -6.02f);
    compare(29, 101);  // Overdubbing, with wraps and both listening paths on
    send(Engine::kRecord, 0);
    send(Engine::kThru, 0);
    compare(31, 139);
    send(Engine::kLoopLevel, -60);
    compare(32, 171);
}

void CheckMaximumLength() {
    constexpr float rate = 1000;
    constexpr std::size_t capacity = 30000;  // 30 s at 1 kHz
    constexpr std::size_t blockFrames = 64;
    std::unique_ptr<unsigned char[]> memory;
    Rig rig;
    const std::size_t bytes = rig.MemoryBytes(rate, blockFrames);
    memory.reset(new unsigned char[bytes]);
    Expect(rig.Prepare(rate, blockFrames, {memory.get(), bytes}) == wf::PrepareStatus::Ok,
           "1 kHz prepares for the capacity boundary check");
    const Stereo take = Pattern(capacity);
    Send(rig, Engine::kThru, 0);
    Send(rig, Engine::kRecord, 1);
    const std::size_t prefix = capacity - 3;
    for (std::size_t offset = 0; offset < prefix;) {
        const std::size_t remaining = prefix - offset;
        const std::size_t count = remaining < blockFrames ? remaining : blockFrames;
        ExpectAudio(Render(rig, Slice(take, offset, count)), Stereo(count),
                    "first take stays silent until full with Thru off");
        offset += count;
    }
    ExpectState(rig, State::Recording, "still recording three frames before capacity");
    Stereo expected(blockFrames);
    for (std::size_t i = 3; i < blockFrames; ++i) {
        expected.left[i] = take.left[i - 3];
        expected.right[i] = take.right[i - 3];
    }
    ExpectAudio(Render(rig, Pattern(blockFrames, prefix)), expected,
                "full take starts playing frame zero within the same callback");
    ExpectState(rig, State::Playing, "capacity ends the first take and releases Record");
    ExpectAudio(Render(rig, Stereo(blockFrames)), Slice(take, blockFrames - 3, blockFrames),
                "next callback continues playback from its advanced position");
    Restart(rig);
    // Check the whole take, including its last three frames and the wrap.
    for (std::size_t offset = 0; offset < capacity + blockFrames; offset += blockFrames) {
        Stereo wrapped(blockFrames);
        for (std::size_t i = 0; i < blockFrames; ++i) {
            wrapped.left[i] = take.left[(offset + i) % capacity];
            wrapped.right[i] = take.right[(offset + i) % capacity];
        }
        ExpectAudio(Render(rig, Stereo(blockFrames)), wrapped,
                    "full-capacity loop plays and wraps exactly");
    }
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
    Expect(Step(rig), "unprepared engine outputs silence");
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
    CheckPlayback(rig);
    CheckOverdub(rig);
    CheckInPlace();
    CheckMaximumLength();
    CheckEvents(rig);

    Expect(rig.Prepare(kSampleRate, kFrames, {nullptr, 0}) == wf::PrepareStatus::MemoryTooSmall,
           "failed Prepare rejects a missing heap block");
    Send(rig, Engine::kRecord, 0);
    Clear(rig);
    Expect(Step(rig), "engine outputs silence after failed Prepare");
    ExpectState(rig, State::Recording, "events after failed Prepare are ignored");

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
