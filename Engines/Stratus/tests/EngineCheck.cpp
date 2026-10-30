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

std::size_t FadeFramesFor(float sampleRate) {
    const auto frames = static_cast<std::size_t>(std::ceil(sampleRate * 0.005));
    // Backstop, as in Prepare(): supported positive rates already ceil to >= 1.
    return frames == 0 ? 1 : frames;
}

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

Stereo RenderInBlocks(Rig& rig, const Stereo& input) {
    Stereo output(input.left.size());
    for (std::size_t offset = 0; offset < input.left.size();) {
        const std::size_t remaining = input.left.size() - offset;
        const std::size_t blockSize = offset == 0 ? 2 : kFrames;
        const std::size_t count = remaining < blockSize ? remaining : blockSize;
        const Stereo block = Render(rig, Slice(input, offset, count));
        for (std::size_t i = 0; i < count; ++i) {
            output.left[offset + i] = block.left[i];
            output.right[offset + i] = block.right[i];
        }
        offset += count;
    }
    return output;
}

void Wait(Rig& rig, std::size_t frames) {
    for (std::size_t remaining = frames; remaining != 0;) {
        const std::size_t count = remaining < kFrames ? remaining : kFrames;
        Render(rig, Stereo(count));
        remaining -= count;
    }
}

// Steady-state checks wait for Thru without recording extra input. Call
// before starting a take, and restart playback afterwards if position matters.
void SetThru(Rig& rig, bool on, std::size_t fadeFrames = FadeFramesFor(kSampleRate)) {
    Send(rig, Engine::kThru, on ? 1.0f : 0.0f);
    Wait(rig, fadeFrames);
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

Stereo LoopEnvelope(const Stereo& loop, std::size_t start, const std::vector<float>& gains) {
    Stereo expected(gains.size());
    for (std::size_t i = 0; i < gains.size(); ++i) {
        const std::size_t pos = (start + i) % loop.left.size();
        expected.left[i] = loop.left[pos] * gains[i];
        expected.right[i] = loop.right[pos] * gains[i];
    }
    return expected;
}

void Reset(Rig& rig) {
    Clear(rig);
    Send(rig, Engine::kLoopLevel, 0);
    SetThru(rig, true);
}

void Restart(Rig& rig, std::size_t length, float rate = kSampleRate) {
    Send(rig, Engine::kPlay, 0);
    const std::size_t fadeFrames = FadeFramesFor(rate);
    Wait(rig, fadeFrames);
    Send(rig, Engine::kPlay, 1);
    // Let the fade-in settle for whole laps, so the next read starts at zero.
    Wait(rig, ((fadeFrames + length - 1) / length) * length);
}

std::size_t SeamFramesFor(std::size_t length, float rate) {
    const std::size_t fade = FadeFramesFor(rate);
    return length > 1 ? (length - 1 < fade ? length - 1 : fade) : 0;
}

// The seam blends a loop's last frames into the input that preceded its
// Record press. Checks about other things feed the take's own last frames
// first, so the seam is transparent; nothing else may be moving.
void StartTake(Rig& rig, const Stereo& take, float rate = kSampleRate) {
    const std::size_t length = take.left.size();
    const std::size_t seam = SeamFramesFor(length, rate);
    RenderInBlocks(rig, Slice(take, length - seam, seam));
    Send(rig, Engine::kRecord, 1);
}

// The loop as heard: frame 0 as recorded, the last before.size() frames
// blending linearly into `before`, the input just before the press.
Stereo SeamedLoop(const Stereo& take, const Stereo& before) {
    Stereo expected = take;
    const std::size_t seam = before.left.size();
    const std::size_t start = take.left.size() - seam;
    for (std::size_t i = 0; i < seam; ++i) {
        const float mix = static_cast<float>(i + 1) / seam;
        float& left = expected.left[start + i];
        float& right = expected.right[start + i];
        left = i + 1 == seam ? before.left[i] : left + (before.left[i] - left) * mix;
        right = i + 1 == seam ? before.right[i] : right + (before.right[i] - right) * mix;
    }
    return expected;
}

void CheckThruRamp() {
    struct Rate { float sampleRate; std::size_t fadeFrames; };
    const Rate rates[] = {
        {100, 1}, {1000, 5}, {8200, 41}, {44100, 221}, {48000, 240}, {192000, 960}};
    for (const Rate& rate : rates) {
        Rig rig;
        const std::size_t bytes = rig.MemoryBytes(rate.sampleRate, kFrames);
        std::unique_ptr<unsigned char[]> memory(new unsigned char[bytes]);
        const wf::MemoryBlock block{memory.get(), bytes};
        Expect(rig.Prepare(rate.sampleRate, kFrames, block) == wf::PrepareStatus::Ok,
               "Thru ramp rig prepares");
        ExpectAudio(Render(rig, Stereo(1, 1)), Stereo(1, 1), "Thru starts at full gain");
        for (bool on : {false, true}) {
            Send(rig, Engine::kThru, on ? 1 : 0);
            Expect(rig.Get().ParamValue(Engine::kThru) == (on ? 1 : 0),
                   "Thru reads back its target immediately");
            Render(rig, Stereo(0, 1));
            float previous = on ? 0 : 1;
            for (std::size_t offset = 0; offset < rate.fadeFrames;) {
                // Split even the five-frame fade across callbacks.
                const std::size_t blockSize = offset == 0 ? 2 : kFrames;
                const std::size_t remaining = rate.fadeFrames - offset;
                const std::size_t count = remaining < blockSize ? remaining : blockSize;
                const Stereo actual = Render(rig, Stereo(count, 1));
                Stereo expected(count);
                for (std::size_t i = 0; i < count; ++i) {
                    const float progress = static_cast<float>(offset + i + 1) /
                                           static_cast<float>(rate.fadeFrames);
                    expected.left[i] = expected.right[i] = on ? progress : 1 - progress;
                    Expect(std::abs(actual.left[i] - previous) <=
                               1.0f / static_cast<float>(rate.fadeFrames) + 1e-6f,
                           "Thru neighbour difference is bounded, including callback edges");
                    previous = actual.left[i];
                }
                ExpectAudio(actual, expected, "Thru follows the linear five-ms envelope", 1e-6f);
                offset += count;
            }
            Expect(previous == (on ? 1 : 0), "Thru reaches the exact endpoint on time");
            ExpectAudio(Render(rig, Stereo(3, 1)), Stereo(3, on ? 1 : 0),
                        "Thru holds the exact endpoint");
        }
        if (rate.sampleRate == 1000) {
            Send(rig, Engine::kThru, 0);
            Render(rig, Stereo(2, 1));
            Send(rig, Engine::kThru, 0);
            ExpectAudio(Render(rig, Stereo(1, 1)), Stereo(1, 0.4f),
                        "duplicate Thru event does not restart a fade", 1e-6f);
            Send(rig, Engine::kThru, 1);
            ExpectAudio(Render(rig, Stereo(1, 1)), Stereo(1, 0.6f),
                        "mid-fade reversal continues from the current gain", 1e-6f);
            Clear(rig);
            ExpectAudio(Render(rig, Stereo(1, 1)), Stereo(1, 0.8f),
                        "Clear preserves the running dry ramp", 1e-6f);
            ExpectAudio(Render(rig, Stereo(1, 1)), Stereo(1, 1),
                        "reversed fade reaches full gain on time");
        }
        SetThru(rig, false, rate.fadeFrames);
        Expect(rig.Prepare(rate.sampleRate, kFrames, block) == wf::PrepareStatus::Ok,
               "Thru ramp rig re-prepares");
        Expect(rig.Get().ParamValue(Engine::kThru) == 1, "Prepare restores Thru on");
        ExpectAudio(Render(rig, Stereo(1, 1)), Stereo(1, 1),
                    "Prepare restores full dry gain immediately");
    }
}

void CheckInputRamp() {
    struct Rate { float sampleRate; std::size_t fadeFrames; };
    const Rate rates[] = {
        {100, 1}, {1000, 5}, {8200, 41}, {44100, 221}, {48000, 240}, {192000, 960}};
    for (const Rate& rate : rates) {
        Rig rig;
        const std::size_t bytes = rig.MemoryBytes(rate.sampleRate, kFrames);
        std::unique_ptr<unsigned char[]> memory(new unsigned char[bytes]);
        Expect(rig.Prepare(rate.sampleRate, kFrames, {memory.get(), bytes}) == wf::PrepareStatus::Ok,
               "input ramp rig prepares");
        SetThru(rig, false, rate.fadeFrames);
        const std::size_t length = 2 * rate.fadeFrames + 3;
        Stereo take(length, 0.25f);
        for (float& sample : take.right) sample = -0.25f;
        StartTake(rig, take, rate.sampleRate);
        RenderInBlocks(rig, take);
        Send(rig, Engine::kRecord, 0);
        ExpectAudio(RenderInBlocks(rig, Stereo(length)), take,
                    "first take stays full-level from its first frame to its last");

        // Both listening paths muted: only the buffer readback can see the punch.
        Send(rig, Engine::kLoopLevel, -60);
        Send(rig, Engine::kRecord, 1);
        ExpectState(rig, State::Overdubbing, "punch-in reads Record on immediately");
        Render(rig, Stereo(0));
        Stereo input(rate.fadeFrames + 1, 1);
        for (float& sample : input.right) sample = -0.5f;
        ExpectAudio(RenderInBlocks(rig, input), Stereo(input.left.size()),
                    "muted punch-in still writes input");
        Send(rig, Engine::kRecord, 0);
        ExpectState(rig, State::Playing, "punch-out reads Record off before writing has faded");
        Render(rig, Stereo(0));
        input = Stereo(rate.fadeFrames + 2, 1);
        for (float& sample : input.right) sample = -0.5f;
        ExpectAudio(RenderInBlocks(rig, input), Stereo(input.left.size()),
                    "muted punch-out still writes the fading input");

        Stereo expected = take;
        for (std::size_t i = 0; i < rate.fadeFrames; ++i) {
            const float gain = static_cast<float>(i + 1) / rate.fadeFrames;
            expected.left[i] += gain;
            expected.right[i] -= 0.5f * gain;
            const std::size_t outIndex = rate.fadeFrames + 1 + i;
            expected.left[outIndex] += 1 - gain;
            expected.right[outIndex] -= 0.5f * (1 - gain);
        }
        expected.left[rate.fadeFrames] += 1;
        expected.right[rate.fadeFrames] -= 0.5f;
        Send(rig, Engine::kLoopLevel, 0);
        const Stereo actual = RenderInBlocks(rig, Stereo(length));
        ExpectAudio(actual, expected, "buffer contains both five-ms punch ramps", 1e-6f);
        float previous = take.left[0];
        for (std::size_t i = 0; i < length; ++i) {
            Expect(std::abs(actual.left[i] - previous) <= 1.0f / rate.fadeFrames + 1e-6f,
                   "written punch envelope has no step above one fade increment");
            previous = actual.left[i];
        }
        Expect(actual.left[rate.fadeFrames - 1] == 1.25f &&
                   actual.left[2 * rate.fadeFrames] == 0.25f && actual.left.back() == 0.25f,
               "written punch reaches exact full and zero gain on time");
        ExpectAudio(RenderInBlocks(rig, Stereo(length)), actual,
                    "buffer is unchanged after punch-out has finished");
    }
}

void CheckInputRampEdges() {
    Rig rig;
    const std::size_t bytes = rig.MemoryBytes(1000, kFrames);
    std::unique_ptr<unsigned char[]> memory(new unsigned char[bytes]);
    Expect(rig.Prepare(1000, kFrames, {memory.get(), bytes}) == wf::PrepareStatus::Ok,
           "input ramp edge-case rig prepares");
    SetThru(rig, false, 5);
    const auto startLoop = [&](std::size_t length) {
        Clear(rig);
        Wait(rig, 5);
        Send(rig, Engine::kRecord, 1);
        Render(rig, Stereo(length));
        Send(rig, Engine::kRecord, 0);
    };
    const auto expectLoop = [&](const std::vector<float>& values, const char* description) {
        Stereo expected(values.size());
        expected.left = expected.right = values;
        Restart(rig, values.size(), 1000);
        ExpectAudio(Render(rig, Stereo(values.size())), expected, description, 1e-6f);
    };

    startLoop(7);
    Send(rig, Engine::kRecord, 1);
    Render(rig, Stereo(5, 1));
    Send(rig, Engine::kRecord, 0);
    Render(rig, Stereo(5, 1));
    expectLoop({0.6f, 0.6f, 0.6f, 0.8f, 1, 0.8f, 0.6f},
               "punch-out continues writing across the loop wrap");

    startLoop(17);
    Send(rig, Engine::kRecord, 1);
    Render(rig, Stereo(1, 1));
    Send(rig, Engine::kRecord, 1);  // Duplicate during punch-in.
    Render(rig, Stereo(1, 1));
    Send(rig, Engine::kRecord, 0);
    Render(rig, Stereo(0));
    Render(rig, Stereo(1, 1));
    Send(rig, Engine::kRecord, 1);  // Reverse the punch-out.
    Render(rig, Stereo(1, 1));
    Send(rig, Engine::kRecord, 1);
    Render(rig, Stereo(2, 1));
    Send(rig, Engine::kRecord, 0);
    Render(rig, Stereo(1, 1));
    Send(rig, Engine::kRecord, 0);  // Duplicate during punch-out.
    Render(rig, Stereo(4, 1));
    Send(rig, Engine::kRecord, 1);
    Render(rig, Stereo(1, 1));
    Send(rig, Engine::kRecord, 0);
    Render(rig, Stereo(1, 1));
    expectLoop({0.2f, 0.4f, 0.2f, 0.4f, 0.6f, 0.8f, 0.6f, 0.4f, 0.2f, 0, 0, 0.2f,
                0, 0, 0, 0, 0},
               "punch reversals, duplicates and zero-frame calls preserve gain continuity");

    startLoop(17);
    Send(rig, Engine::kPlay, 0);
    Wait(rig, 5);
    Send(rig, Engine::kRecord, 1);
    Render(rig, Stereo(5, 1));
    Send(rig, Engine::kPlay, 0);
    Send(rig, Engine::kPlay, 1);
    Render(rig, Stereo(17, 1));
    expectLoop({0.2f, 0.4f, 0.6f, 0.8f, 1, 0.8f, 0.6f, 0.4f, 0.2f, 0, 0, 0, 0, 0, 0, 0, 0},
               "Play off keeps the punch fading while its tail playhead moves");

    Send(rig, Engine::kRecord, 1);
    Render(rig, Stereo(5, 1));
    Clear(rig);
    const Stereo newTake = Pattern(3);
    Send(rig, Engine::kRecord, 1);
    Render(rig, newTake);
    Send(rig, Engine::kRecord, 0);
    Wait(rig, 2);  // Finish the old tail without advancing the new loop.
    Render(rig, Stereo(3, 1));
    // Its seam blends into the two frames of 1 rendered before Record.
    ExpectAudio(Render(rig, Stereo(3)), SeamedLoop(newTake, Stereo(2, 1)),
                "Clear cancels pending writes and the next first take stays unscaled");
}

void CheckInputRampReprepare() {
    constexpr float rate = 1000;
    Rig rig;
    const std::size_t bytes = rig.MemoryBytes(rate, kFrames);
    std::unique_ptr<unsigned char[]> memory(new unsigned char[bytes]);
    const wf::MemoryBlock block{memory.get(), bytes};
    Expect(rig.Prepare(rate, kFrames, block) == wf::PrepareStatus::Ok,
           "input ramp re-Prepare rig prepares");
    Send(rig, Engine::kRecord, 1);
    Render(rig, Stereo(8));
    Send(rig, Engine::kRecord, 0);
    Send(rig, Engine::kRecord, 1);
    Render(rig, Stereo(4, 1));  // Leave the old punch at 0.8 gain.

    Expect(rig.Prepare(rate, kFrames, block) == wf::PrepareStatus::Ok,
           "Prepare succeeds during an input punch");
    // A host can replay Record immediately after a device restart. Do not
    // Clear or settle Thru here: either would hide a stale input ramp.
    Send(rig, Engine::kRecord, 1);
    Render(rig, Stereo(2));  // Shorter than the five-frame fade.
    Send(rig, Engine::kRecord, 0);
    Render(rig, Stereo(4, 1));
    Restart(rig, 2, rate);
    ExpectAudio(Render(rig, Stereo(2)), Stereo(2),
                "Prepare resets the old input punch before an immediate short take");
}

void CheckLoopRamp() {
    struct Rate { float sampleRate; std::size_t fadeFrames; };
    const Rate rates[] = {
        {100, 1}, {1000, 5}, {8200, 41}, {44100, 221}, {48000, 240}, {192000, 960}};
    for (const Rate& rate : rates) {
        Rig rig;
        const std::size_t bytes = rig.MemoryBytes(rate.sampleRate, kFrames);
        std::unique_ptr<unsigned char[]> memory(new unsigned char[bytes]);
        Expect(rig.Prepare(rate.sampleRate, kFrames, {memory.get(), bytes}) == wf::PrepareStatus::Ok,
               "loop ramp rig prepares");
        SetThru(rig, false, rate.fadeFrames);
        Stereo take(2 * rate.fadeFrames + 7, 1);
        for (float& sample : take.right) sample = -0.5f;
        StartTake(rig, take, rate.sampleRate);
        RenderInBlocks(rig, take);
        Send(rig, Engine::kRecord, 0);
        ExpectAudio(Render(rig, Stereo(2)), Repeat(take, 2),
                    "ending a first take starts at full loop gain");
        for (bool on : {false, true, false}) {
            Send(rig, Engine::kPlay, on ? 1 : 0);
            Send(rig, Engine::kPlay, on ? 1 : 0);
            ExpectState(rig, on ? State::Playing : State::Stopped,
                        "Play readback changes immediately, duplicate is a no-op");
            Render(rig, Stereo(0));
            std::vector<float> gains(rate.fadeFrames + 2, on ? 1 : 0);
            for (std::size_t i = 0; i < rate.fadeFrames; ++i) {
                const float progress = static_cast<float>(i + 1) / rate.fadeFrames;
                gains[i] = on ? progress : 1 - progress;
            }
            const Stereo actual = RenderInBlocks(rig, Stereo(gains.size()));
            ExpectAudio(actual, LoopEnvelope(take, 0, gains),
                        "loop start/stop follows a five-ms envelope across callbacks", 1e-6f);
            float previous = on ? 0 : 1;
            for (std::size_t i = 0; i < gains.size(); ++i) {
                Expect(std::abs(actual.left[i] - previous) <= 1.0f / rate.fadeFrames + 1e-6f,
                       "loop gain neighbour difference is bounded");
                previous = actual.left[i];
            }
            Expect(actual.left[rate.fadeFrames - 1] == (on ? 1 : 0) &&
                       actual.right[rate.fadeFrames - 1] == (on ? -0.5f : 0),
                   "loop ramp reaches exact endpoints on time in both channels");
        }
        Restart(rig, take.left.size(), rate.sampleRate);
        Clear(rig);
        Clear(rig);
        ExpectState(rig, State::Empty, "Clear reads Empty before the tail finishes");
        Send(rig, Engine::kPlay, 1);
        ExpectState(rig, State::Empty, "Clear refuses Play while its old tail sounds");
        std::vector<float> gains(rate.fadeFrames + 2, 0);
        for (std::size_t i = 0; i < rate.fadeFrames; ++i) {
            gains[i] = 1 - static_cast<float>(i + 1) / rate.fadeFrames;
        }
        ExpectAudio(RenderInBlocks(rig, Stereo(gains.size())), LoopEnvelope(take, 0, gains),
                    "Clear fades out the old loop then stays silent", 1e-6f);
        Send(rig, Engine::kPlay, 1);
        ExpectState(rig, State::Empty, "Clear remains Empty after its tail finishes");
    }
}

void CheckLoopTailCases() {
    Rig rig;
    const std::size_t bytes = rig.MemoryBytes(1000, kFrames);
    std::unique_ptr<unsigned char[]> memory(new unsigned char[bytes]);
    Expect(rig.Prepare(1000, kFrames, {memory.get(), bytes}) == wf::PrepareStatus::Ok,
           "loop tail rig prepares");
    SetThru(rig, false, 5);
    const Stereo old = Pattern(7, 41);
    const auto startLoop = [&](const Stereo& take, std::size_t position) {
        Clear(rig);
        Wait(rig, 5);
        StartTake(rig, take, 1000);
        RenderInBlocks(rig, take);
        Send(rig, Engine::kRecord, 0);
        Wait(rig, position);
    };

    const Stereo unity(7, 1);
    startLoop(unity, 0);
    Send(rig, Engine::kLoopLevel, -6.02f);
    const float levelGain = std::pow(10.0f, -6.02f / 20.0f);
    ExpectAudio(Render(rig, Stereo(1)), Stereo(1, levelGain),
                "loop plays at the selected level before Stop", 1e-6f);
    Send(rig, Engine::kPlay, 0);
    ExpectAudio(Render(rig, Stereo(2)), LoopEnvelope(unity, 0, {0.8f * levelGain, 0.6f * levelGain}),
                "Stop tail keeps Loop Level instead of jumping to full gain", 1e-6f);
    Send(rig, Engine::kLoopLevel, -60);
    ExpectAudio(Render(rig, Stereo(3)), Stereo(3),
                "Loop Level changes also apply to a running tail");
    Send(rig, Engine::kLoopLevel, 0);

    // Row 1: Stopped drains the moving tail, then restarts at the downbeat.
    startLoop(old, 3);
    Send(rig, Engine::kPlay, 0);
    ExpectAudio(Render(rig, Stereo(2)), LoopEnvelope(old, 3, {0.8f, 0.6f}),
                "Stop keeps the old playhead moving", 1e-6f);
    Send(rig, Engine::kPlay, 0);
    ExpectAudio(Render(rig, Stereo(4)), LoopEnvelope(old, 5, {0.4f, 0.2f, 0, 0}),
                "duplicate Stop does not restart the tail fade, including a wrap", 1e-6f);
    Send(rig, Engine::kPlay, 1);
    ExpectAudio(Render(rig, Stereo(5)), LoopEnvelope(old, 0, {0.2f, 0.4f, 0.6f, 0.8f, 1}),
                "Play after a completed Stop fades in from frame zero", 1e-6f);

    // Row 2: Playing waits for the tail's zero, then fades in at frame zero.
    startLoop(old, 3);
    Send(rig, Engine::kPlay, 0);
    Render(rig, Stereo(2));
    Send(rig, Engine::kPlay, 1);
    Send(rig, Engine::kPlay, 1);
    ExpectState(rig, State::Playing, "pending Play reads on during a fade-out");
    Render(rig, Stereo(0));
    ExpectAudio(Render(rig, Stereo(3)), LoopEnvelope(old, 5, {0.4f, 0.2f, 0}),
                "Play mid-fade completes the old tail before restarting", 1e-6f);
    ExpectAudio(Render(rig, Stereo(5)), LoopEnvelope(old, 0, {0.2f, 0.4f, 0.6f, 0.8f, 1}),
                "pending Play resets only at zero and then fades in", 1e-6f);

    // A partial fade-in reverses at its current gain, rather than full gain.
    Send(rig, Engine::kPlay, 0);
    Wait(rig, 5);
    Send(rig, Engine::kPlay, 1);
    Render(rig, Stereo(2));
    Send(rig, Engine::kPlay, 0);
    ExpectAudio(Render(rig, Stereo(3)), LoopEnvelope(old, 2, {0.2f, 0, 0}),
                "Stop during fade-in drains only the current gain", 1e-6f);

    // Row 3: repeated Clear does not restart the fade or permit Play.
    startLoop(old, 2);
    Clear(rig);
    Render(rig, Stereo(2));
    Clear(rig);
    ExpectAudio(Render(rig, Stereo(3)), LoopEnvelope(old, 4, {0.4f, 0.2f, 0}),
                "Clear tail retains its own length and moving playhead", 1e-6f);
    Send(rig, Engine::kPlay, 1);
    ExpectState(rig, State::Empty, "Clear has no loop when its tail ends");

    // Row 4: a new take records now; the old tail owns its own playhead.
    startLoop(old, 5);
    Clear(rig);
    Send(rig, Engine::kRecord, 1);
    const Stereo take = Pattern(9, 193);
    ExpectAudio(Render(rig, Slice(take, 0, 2)), LoopEnvelope(old, 5, {0.8f, 0.6f}),
                "Clear then Record starts the new take while the old tail sounds", 1e-6f);
    ExpectAudio(Render(rig, Slice(take, 2, 3)), LoopEnvelope(old, 0, {0.4f, 0.2f, 0}),
                "old tail survives wrap into samples overwritten by the new take", 1e-6f);
    ExpectState(rig, State::Recording, "old tail completion does not change the new take state");
    ExpectAudio(Render(rig, Slice(take, 5, 4)), Stereo(4),
                "new take continues silently after the old tail ends");
    Send(rig, Engine::kRecord, 0);
    // Recorded at once after Clear: its pre-roll is the silence before.
    ExpectAudio(Render(rig, Stereo(9)), SeamedLoop(take, Stereo(5)),
                "old tail completion preserves the new take position, length and content", 1e-6f);

    // Even a one-frame take can finish before a two-frame old loop fades.
    const Stereo tinyOld = Pattern(2, 11);
    startLoop(tinyOld, 0);
    Clear(rig);
    Send(rig, Engine::kRecord, 1);
    const Stereo tinyNew = Pattern(1, 199);
    ExpectAudio(Render(rig, tinyNew), LoopEnvelope(tinyOld, 0, {0.8f}),
                "tiny new take begins immediately during Clear", 1e-6f);
    Send(rig, Engine::kRecord, 0);
    ExpectAudio(Render(rig, Stereo(4, 1)), LoopEnvelope(tinyOld, 1, {0.6f, 0.4f, 0.2f, 0}),
                "finished new take waits while the original old tail wraps repeatedly", 1e-6f);
    ExpectAudio(Render(rig, Stereo(4)), Repeat(tinyNew, 4),
                "new take starts at full gain after the old tail, without being erased");

    startLoop(tinyOld, 0);
    Clear(rig);
    Send(rig, Engine::kRecord, 1);
    Render(rig, tinyNew);
    // Finish Playing: the unheard take has full loop gain, so only
    // BeginTail's active-tail guard can prevent replacing the original tail.
    Send(rig, Engine::kRecord, 0);
    ExpectState(rig, State::Playing, "new take finishes Playing while its predecessor fades");
    Clear(rig);
    Clear(rig);
    Send(rig, Engine::kRecord, 1);
    const Stereo replacement = Pattern(2, 211);
    ExpectAudio(Render(rig, replacement), LoopEnvelope(tinyOld, 1, {0.6f, 0.4f}),
                "repeated Clear and a replacement take retain the original cached tail", 1e-6f);
    Send(rig, Engine::kPlay, 0);
    ExpectAudio(Render(rig, Stereo(2)), LoopEnvelope(tinyOld, 1, {0.2f, 0}),
                "new take can finish Stopped without cutting or replacing the old tail", 1e-6f);
    Send(rig, Engine::kPlay, 1);
    ExpectAudio(Render(rig, Stereo(5)),
                LoopEnvelope(SeamedLoop(replacement, tinyNew), 0, {0.2f, 0.4f, 0.6f, 0.8f, 1}),
                "tail completion preserves a replacement take that already stopped", 1e-6f);

    // Record requested during Stop waits with Play, then punches in at zero.
    startLoop(old, 0);
    Send(rig, Engine::kPlay, 0);
    Render(rig, Stereo(2));
    Send(rig, Engine::kRecord, 1);
    ExpectState(rig, State::Overdubbing, "pending overdub reads Record on immediately");
    ExpectAudio(Render(rig, Stereo(3, 1)), LoopEnvelope(old, 2, {0.4f, 0.2f, 0}),
                "pending overdub does not reverse or write into a draining Stop tail", 1e-6f);
    Render(rig, Stereo(5, 1));
    Send(rig, Engine::kRecord, 0);
    Wait(rig, 5);
    Stereo overdubbed = old;
    for (std::size_t i = 0; i < 5; ++i) {
        overdubbed.left[i] += static_cast<float>(i + 1) / 5;
        overdubbed.right[i] += static_cast<float>(i + 1) / 5;
    }
    Restart(rig, old.left.size(), 1000);
    ExpectAudio(Render(rig, Stereo(7)), overdubbed,
                "pending overdub begins its input ramp on the restarted loop", 1e-6f);

    // Re-Prepare must drop an active tail and its scratch metadata.
    Send(rig, Engine::kPlay, 0);
    Render(rig, Stereo(1));
    Expect(rig.Prepare(1000, kFrames, {memory.get(), bytes}) == wf::PrepareStatus::Ok,
           "Prepare succeeds during a loop tail");
    ExpectAudio(Render(rig, Stereo(6)), Stereo(6), "Prepare removes the old tail immediately");
}

void CheckPlayback(Rig& rig) {
    Reset(rig);
    const Stereo take = Pattern(13);
    const Stereo live = Pattern(31, 73);
    ExpectAudio(Render(rig, live), live, "Empty passes input with Thru on");
    SetThru(rig, false);
    ExpectAudio(Render(rig, live), Stereo(31), "Empty mutes input with Thru off");

    // Record raw input even with both listening paths muted. Duplicate Record
    // and Play-on events must not rewind or play the unfinished first take.
    Send(rig, Engine::kLoopLevel, -60);
    StartTake(rig, take);
    ExpectAudio(Render(rig, Slice(take, 0, 5)), Stereo(5), "first take with Thru off is silent");
    Send(rig, Engine::kRecord, 1);
    Send(rig, Engine::kPlay, 1);
    ExpectAudio(Render(rig, Slice(take, 5, 8)), Stereo(8),
                "first take continues after duplicate events");
    Send(rig, Engine::kRecord, 0);
    Send(rig, Engine::kLoopLevel, 0);
    ExpectAudio(Render(rig, live), Repeat(take, 31), "loop plays raw recorded samples and wraps");

    Send(rig, Engine::kPlay, 0);
    Wait(rig, FadeFramesFor(kSampleRate));
    ExpectAudio(Render(rig, live), Stereo(31), "Stopped with Thru off is silent");
    SetThru(rig, true);
    ExpectAudio(Render(rig, live), live, "Stopped passes live input with Thru on");
    SetThru(rig, false);
    Restart(rig, take.left.size());
    ExpectAudio(Render(rig, live), Repeat(take, 31), "Play from Stopped restarts at frame zero");

    Restart(rig, take.left.size());
    Send(rig, Engine::kLoopLevel, -6.02f);
    ExpectAudio(Render(rig, live), Repeat(take, 31, 0.5f), "-6.02 dB is approximately half gain",
                2e-5f);
    Restart(rig, take.left.size());
    Send(rig, Engine::kLoopLevel, -60);
    ExpectAudio(Render(rig, live), Stereo(31), "bottom of Loop Level is exactly silent");
    SetThru(rig, true);
    Restart(rig, take.left.size());
    Send(rig, Engine::kLoopLevel, 0);
    Stereo mix = Repeat(take, 31);
    for (std::size_t i = 0; i < 31; ++i) {
        mix.left[i] += live.left[i];
        mix.right[i] += live.right[i];
    }
    ExpectAudio(Render(rig, live), mix, "Playing mixes Thru input and the loop");

    Reset(rig);
    const Stereo shorter = Pattern(3, 41);
    StartTake(rig, shorter);
    ExpectAudio(Render(rig, shorter), shorter, "Recording monitors input with Thru on");
    Send(rig, Engine::kPlay, 0);
    SetThru(rig, false);
    Restart(rig, shorter.left.size());
    ExpectAudio(Render(rig, live), Repeat(shorter, 31),
                "new shorter take never reads stale tail after Clear");
}

void CheckOverdub(Rig& rig) {
    Reset(rig);
    const Stereo take = Pattern(13);
    // Steady-state overdub checks punch in over silence for whole laps, so
    // the gain settles while preserving both the buffer and the loop phase.
    const auto settleInput = [&]() {
        const std::size_t length = take.left.size();
        const std::size_t frames = ((FadeFramesFor(kSampleRate) + length - 1) / length) * length;
        for (std::size_t remaining = frames; remaining != 0;) {
            const std::size_t count = remaining < kFrames ? remaining : kFrames;
            Render(rig, Stereo(count));
            remaining -= count;
        }
    };
    StartTake(rig, take);
    Render(rig, take);
    Send(rig, Engine::kRecord, 0);
    SetThru(rig, false);
    Restart(rig, take.left.size());
    Send(rig, Engine::kLoopLevel, -60);
    Send(rig, Engine::kRecord, 1);
    settleInput();
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
    SetThru(rig, true);
    Restart(rig, take.left.size());
    Send(rig, Engine::kRecord, 1);
    settleInput();
    ExpectAudio(Render(rig, next), expected,
                "overdub reads old loop before adding input, across wraps");
    Send(rig, Engine::kPlay, 0);
    ExpectState(rig, State::Stopped, "stopping overdub releases Record");
    SetThru(rig, false);
    Restart(rig, take.left.size());
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
    send(Engine::kLoopLevel, 0);
    Clear(separate);
    Clear(inPlace);
    send(Engine::kRecord, 1);
    compare(2, 203);  // First take overwrites a prefix while the tail wraps.
    send(Engine::kRecord, 0);
    compare(32, 205);  // The finished take waits for that same tail.
    Clear(separate);
    Clear(inPlace);
    send(Engine::kRecord, 1);
    for (std::size_t i = 0; i < 4; ++i) compare(32, 237 + i * 32);
    send(Engine::kRecord, 0);
    compare(32, 365);  // New playback after the original tail has finished.
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
    const Stereo seamed = SeamedLoop(take, Stereo(FadeFramesFor(rate)));
    SetThru(rig, false, FadeFramesFor(rate));
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
    Restart(rig, capacity, rate);
    // Check the whole take, including its last three frames and the wrap.
    for (std::size_t offset = 0; offset < capacity + blockFrames; offset += blockFrames) {
        Stereo wrapped(blockFrames);
        for (std::size_t i = 0; i < blockFrames; ++i) {
            wrapped.left[i] = seamed.left[(offset + i) % capacity];
            wrapped.right[i] = seamed.right[(offset + i) % capacity];
        }
        // The seam is computed; under -ffast-math it may round differently.
        ExpectAudio(Render(rig, Stereo(blockFrames)), wrapped,
                    "full-capacity loop plays and wraps exactly", 1e-6f);
    }
}

void CheckSeam() {
    struct Rate { float sampleRate; std::size_t fadeFrames; };
    const Rate rates[] = {
        {100, 1}, {1000, 5}, {8200, 41}, {44100, 221}, {48000, 240}, {192000, 960}};
    for (const Rate& rate : rates) {
        Rig rig;
        const std::size_t bytes = rig.MemoryBytes(rate.sampleRate, kFrames);
        std::unique_ptr<unsigned char[]> memory(new unsigned char[bytes]);
        Expect(rig.Prepare(rate.sampleRate, kFrames, {memory.get(), bytes}) == wf::PrepareStatus::Ok,
               "seam rig prepares");
        SetThru(rig, false, rate.fadeFrames);
        // Opposite signs on the two sides of the seam, a downbeat that differs
        // from both, and on the left a pair whose naive blend misses the
        // pre-roll at the last frame (0.3f + (-1 - 0.3f) != -1 in float).
        const std::size_t length = 2 * rate.fadeFrames + 7;
        const std::size_t seam = SeamFramesFor(length, rate.sampleRate);
        Stereo before(rate.fadeFrames, -1);
        for (float& sample : before.right) sample = -0.5f;
        Stereo take(length, 0.3f);
        for (float& sample : take.right) sample = 0.5f;
        take.left[0] = 0.75f;
        take.right[0] = -0.25f;
        RenderInBlocks(rig, before);
        Send(rig, Engine::kRecord, 1);
        RenderInBlocks(rig, take);
        Send(rig, Engine::kRecord, 0);
        const Stereo expected =
            SeamedLoop(take, Slice(before, rate.fadeFrames - seam, seam));
        const Stereo laps = RenderInBlocks(rig, Stereo(4 * length));
        ExpectAudio(laps, Repeat(expected, 4 * length),
                    "the loop's end blends into its pre-roll on every lap", 1e-6f);
        for (std::size_t lap = 0; lap < 4; ++lap) {
            const std::size_t first = lap * length;
            Expect(laps.left[first] == 0.75f && laps.right[first] == -0.25f,
                   "the downbeat plays as recorded at full gain on every lap");
            Expect(laps.left[first + length - 1] == -1 && laps.right[first + length - 1] == -0.5f,
                   "the last frame is the downbeat's recorded predecessor");
            float previous = laps.left[first + length - seam - 1];
            for (std::size_t i = length - seam; i < length; ++i) {
                Expect(std::abs(laps.left[first + i] - previous) <= 1.3f / seam + 1e-6f,
                       "the seam moves by at most one crossfade step per frame");
                previous = laps.left[first + i];
            }
        }
    }
}

void CheckSeamEdges() {
    Rig rig;
    const std::size_t bytes = rig.MemoryBytes(1000, kFrames);
    std::unique_ptr<unsigned char[]> memory(new unsigned char[bytes]);
    const wf::MemoryBlock block{memory.get(), bytes};
    const auto prepare = [&]() {
        Expect(rig.Prepare(1000, kFrames, block) == wf::PrepareStatus::Ok, "seam edge rig prepares");
        SetThru(rig, false, 5);
    };
    const auto record = [&](const Stereo& take) {
        Send(rig, Engine::kRecord, 1);
        Render(rig, take);
        Send(rig, Engine::kRecord, 0);
    };

    // A one-frame click on the downbeat survives every lap.
    prepare();
    Stereo click(20);
    click.left[0] = click.right[0] = 1;
    record(click);
    const Stereo laps = Render(rig, Stereo(60));
    Expect(laps.left[0] == 1 && laps.left[20] == 1 && laps.left[40] == 1 && laps.right[40] == 1,
           "a click on the downbeat is heard at full on every lap");

    // Short loops: no seam at one frame, and frame 0 stays outside it.
    prepare();
    Render(rig, Stereo(5, 0.5f));
    record(Stereo(1, 0.3f));
    ExpectAudio(Render(rig, Stereo(4)), Stereo(4, 0.3f), "a one-frame loop has no seam");
    Clear(rig);
    Wait(rig, 5);
    Render(rig, Stereo(5, 0.5f));
    Stereo two(2);
    two.left = two.right = {0.3f, 0.7f};
    record(two);
    Stereo twoExpected(2);
    twoExpected.left = twoExpected.right = {0.3f, 0.5f};
    ExpectAudio(Render(rig, Stereo(4)), Repeat(twoExpected, 4),
                "a two-frame loop keeps its downbeat; its last frame is the pre-roll");

    // Stop from Overdubbing inside the seam: the fading punch reaches both
    // sides of the blend, so it reads back at its written gain.
    prepare();
    record(Stereo(10));
    Send(rig, Engine::kRecord, 1);
    Render(rig, Stereo(10));  // The input gain settles at full, back at frame 0.
    Render(rig, Stereo(6));
    Send(rig, Engine::kPlay, 0);
    Render(rig, Stereo(5, 1));
    Restart(rig, 10, 1000);
    std::vector<float> punch = {0, 0, 0, 0, 0, 0, 0.8f, 0.6f, 0.4f, 0.2f};
    Stereo punchExpected(10);
    punchExpected.left = punchExpected.right = punch;
    ExpectAudio(Render(rig, Stereo(10)), punchExpected,
                "a Stop tail's punch inside the seam keeps its gain", 1e-6f);

    // Clear then Record: the new take's pre-roll goes to the other slot, so
    // the old tail still blends into its own.
    prepare();
    const Stereo old = Pattern(7, 41);
    Render(rig, Stereo(5, 0.5f));
    record(old);
    Render(rig, Stereo(5, -0.25f));  // Becomes the new take's pre-roll.
    Clear(rig);
    Send(rig, Engine::kRecord, 1);
    const Stereo next = Pattern(9, 193);
    ExpectAudio(Render(rig, next),
                LoopEnvelope(SeamedLoop(old, Stereo(5, 0.5f)), 5, {0.8f, 0.6f, 0.4f, 0.2f, 0, 0, 0, 0, 0}),
                "Clear's tail keeps its own pre-roll while a new take records", 1e-6f);
    Send(rig, Engine::kRecord, 0);
    ExpectAudio(Render(rig, Stereo(18)), Repeat(SeamedLoop(next, Stereo(5, -0.25f)), 18),
                "the new take blends into the input before its own press", 1e-6f);

    // Prepare() clears the ring: a take right after it blends into silence.
    Render(rig, Stereo(5, 0.9f));
    prepare();
    Stereo three(3);
    three.left = three.right = {0.3f, 0.6f, 0.9f};
    record(three);
    ExpectAudio(Render(rig, Stereo(3)), SeamedLoop(three, Stereo(2)),
                "no input from before Prepare() reaches a seam", 1e-6f);
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
    CheckThruRamp();
    CheckInputRamp();
    CheckInputRampEdges();
    CheckInputRampReprepare();
    CheckLoopRamp();
    CheckLoopTailCases();
    CheckPlayback(rig);
    CheckOverdub(rig);
    CheckInPlace();
    CheckMaximumLength();
    CheckSeam();
    CheckSeamEdges();
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
