#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "wf/Rig.h"
#include "wf/clouds/Engine.h"

namespace {

using wf::clouds::Engine;
using wf::clouds::Module;
using wf::clouds::PlaybackMode;
using wf::clouds::Quality;
using wf::clouds::StereoFrame;
using Type = wf::ControlEvent::Type;

int failures = 0;

void Expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

// Storage outside the stack: the Module is about 9 KiB and the arenas 180 KiB.
alignas(wf::kMemoryAlignment) unsigned char arena[Engine::kMemoryBytes];
alignas(wf::clouds::kMemoryAlignment) unsigned char largeArena[wf::clouds::kLargeMemoryBytes];
alignas(wf::clouds::kMemoryAlignment) unsigned char smallArena[wf::clouds::kSmallMemoryBytes];
wf::SoloRig<Engine> rig;

const wf::MemoryBlock kArena{arena, sizeof(arena)};

bool Send(Type type, wf::ParamId id, float value) {
    return static_cast<wf::Rig&>(rig).Handle(wf::ControlEvent{type, 0, id, value});
}

bool SetParam(wf::ParamId id, float value) {
    return Send(Type::Param, id, value);
}

void CheckDescription() {
    Engine& engine = rig.Get();
    Expect(wf::IsValidParamTable(engine.Params()), "the parameter table is valid");
    Expect(engine.Params().count == 15, "fifteen parameters");
    Expect(std::strcmp(rig.Name(), "Clouds") == 0, "the rig is named after the engine");
    Expect(engine.MemoryBytes(48000.0f, 512) == 118784 + 65408,
           "memory is the two Module arenas, already aligned");
}

void CheckPrepare() {
    wf::Rig& base = rig;
    Expect(base.Prepare(32000.0f, 32, wf::MemoryBlock{arena, sizeof(arena) - 1}) ==
               wf::PrepareStatus::MemoryTooSmall,
           "one byte short is too small");
    Expect(base.Prepare(32000.0f, 32, wf::MemoryBlock{arena + 4, sizeof(arena) - 4}) ==
               wf::PrepareStatus::MemoryTooSmall,
           "the block must hold all of it");
    Expect(base.Prepare(22050.0f, 32, kArena) == wf::PrepareStatus::UnsupportedSampleRate,
           "22.05 kHz is not supported");
    Expect(base.Prepare(32000.0f, 0, kArena) == wf::PrepareStatus::UnsupportedBlockSize,
           "a zero block size is refused");
    Expect(!rig.Get().IsPrepared(), "a refused Prepare() leaves the engine unprepared");

    float left[4] = {1, 1, 1, 1};
    float right[4] = {1, 1, 1, 1};
    base.Process(left, right, left, right, 4);
    Expect(left[0] == 0.0f && right[3] == 0.0f, "an unprepared engine outputs silence");

    for (const float rate : {32000.0f, 44100.0f, 48000.0f}) {
        Expect(base.Prepare(rate, 512, kArena) == wf::PrepareStatus::Ok,
               "32, 44.1 and 48 kHz prepare");
    }
}

void CheckDefaultsAndEvents() {
    Engine& engine = rig.Get();
    Expect(static_cast<wf::Rig&>(rig).Prepare(48000.0f, 256, kArena) == wf::PrepareStatus::Ok,
           "prepares");

    // Every parameter starts at its table default.
    const wf::clouds::ControlSnapshot& c = engine.Controls();
    Expect(c.position == 0.5f && c.size == 0.5f && c.pitch == 0.0f && c.density == 0.75f &&
               c.texture == 0.5f && c.dryWet == 0.5f && c.stereoSpread == 0.0f &&
               c.feedback == 0.0f && c.reverb == 0.0f && !c.freeze && !c.gate,
           "the controls start at the table defaults");
    Expect(engine.InputGainDecibels() == 0.0f, "IN GAIN starts at 0 dB");
    Expect(engine.GetQuality() == Quality::Stereo16Bit32k &&
               engine.GetPlaybackMode() == PlaybackMode::Granular,
           "stereo 16-bit, granular");

    Expect(SetParam(Engine::kPosition, 0.25f) && c.position == 0.25f, "Position reaches the module");
    Expect(SetParam(Engine::kTexture, 0.8f) && c.texture == 0.8f, "Texture reaches the module");
    Expect(SetParam(Engine::kReverb, 0.3f) && c.reverb == 0.3f, "Reverb reaches the module");
    Expect(SetParam(Engine::kGate, 1.0f) && c.gate, "Gate reaches the module");

    // Out-of-range values are clamped at the rig, never reaching the engine.
    Expect(SetParam(Engine::kPitch, 30.0f) && c.pitch == 24.0f, "pitch clamps to +24 st");
    Expect(SetParam(Engine::kPosition, -1.0f) && c.position == 0.0f, "position clamps to 0");
    Expect(SetParam(Engine::kInputGain, -40.0f) && engine.InputGainDecibels() == -18.0f,
           "IN GAIN clamps to -18 dB");
    Expect(SetParam(Engine::kPlaybackMode, 7.0f) &&
               engine.GetPlaybackMode() == PlaybackMode::Spectral,
           "mode clamps to the last mode");
    Expect(!SetParam(Engine::kTrigger, 1.0f), "TRIG is a trigger, not a value");
    Expect(Send(Type::Trigger, Engine::kTrigger, 0.0f), "TRIG fires as a trigger");
    Expect(!SetParam(15, 0.0f), "an unknown parameter is dropped");
}

// The module's rules: a new quality, or entering or leaving spectral, erases
// the recording and releases freeze; other changes keep both.
void CheckFreezeRelease() {
    Engine& engine = rig.Get();
    const wf::clouds::ControlSnapshot& c = engine.Controls();
    Expect(static_cast<wf::Rig&>(rig).Prepare(48000.0f, 256, kArena) == wf::PrepareStatus::Ok,
           "prepares");

    SetParam(Engine::kFreeze, 1.0f);
    SetParam(Engine::kQuality, 0.0f);
    Expect(c.freeze, "requesting the current quality keeps freeze");
    SetParam(Engine::kQuality, 1.0f);
    Expect(!c.freeze && engine.GetQuality() == Quality::Mono16Bit32k,
           "a new quality applies and releases freeze");

    SetParam(Engine::kFreeze, 1.0f);
    SetParam(Engine::kPlaybackMode, 1.0f);
    Expect(c.freeze && engine.GetPlaybackMode() == PlaybackMode::Stretch,
           "granular to stretch keeps freeze");
    SetParam(Engine::kPlaybackMode, 3.0f);
    Expect(!c.freeze, "entering spectral releases freeze");
    SetParam(Engine::kFreeze, 1.0f);
    SetParam(Engine::kPlaybackMode, 0.0f);
    Expect(!c.freeze, "leaving spectral releases freeze");

    // Events apply in order: freeze after a quality change stays on.
    SetParam(Engine::kQuality, 2.0f);
    SetParam(Engine::kFreeze, 1.0f);
    Expect(c.freeze && engine.GetQuality() == Quality::StereoMuLaw16k,
           "freeze sent after a quality change stays on");

    Expect(static_cast<wf::Rig&>(rig).Prepare(48000.0f, 256, kArena) == wf::PrepareStatus::Ok &&
               engine.GetQuality() == Quality::Stereo16Bit32k && !c.freeze,
           "a second Prepare() starts again from the defaults");
}

// What a host's panel reads: every value as constrained, and the changes the
// engine makes itself.
void CheckParamValue() {
    wf::Rig& base = rig;
    Expect(base.Prepare(48000.0f, 256, kArena) == wf::PrepareStatus::Ok, "prepares");
    const wf::Engine& engine = *static_cast<const wf::Rig&>(rig).Slot(0);
    const wf::ParamTable& params = engine.Params();

    bool defaults = true;
    for (std::size_t i = 0; i < params.count; ++i) {
        const wf::ParamSpec& spec = params.specs[i];
        const float expected = spec.kind == wf::ParamKind::Trigger ? 0.0f : spec.def;
        defaults = defaults && engine.ParamValue(spec.id) == expected;
    }
    Expect(defaults, "every parameter reads back its default");

    SetParam(Engine::kPitch, 30.0f);
    SetParam(Engine::kInputGain, -3.0f);
    Expect(engine.ParamValue(Engine::kPitch) == 24.0f, "pitch reads back clamped");
    Expect(engine.ParamValue(Engine::kInputGain) == -3.0f, "IN GAIN reads back in dB");

    SetParam(Engine::kFreeze, 1.0f);
    SetParam(Engine::kPlaybackMode, 2.0f);
    Expect(engine.ParamValue(Engine::kFreeze) == 1.0f &&
               std::strcmp(params.Find(Engine::kPlaybackMode)
                               ->labels[static_cast<int>(engine.ParamValue(Engine::kPlaybackMode))],
                           "Looping Delay") == 0,
           "mode reads back as its label's index, freeze kept");
    SetParam(Engine::kQuality, 3.0f);
    Expect(engine.ParamValue(Engine::kQuality) == 3.0f && engine.ParamValue(Engine::kFreeze) == 0.0f,
           "a quality change reads back, with the freeze it released");
}

// --- Renders --------------------------------------------------------------------

// wf_render_clouds's timeline (Tools/Render/RenderClouds.cpp): a 220/330 Hz
// tone at half scale for two seconds, then silence with freeze on, pitch +7
// from four seconds; dry/wet fully wet.
constexpr std::size_t kFrames = 6 * 32000;
constexpr std::size_t kTwoSeconds = 64000;
constexpr std::size_t kFourSeconds = 128000;
constexpr std::size_t kBlock = wf::clouds::kBlockFrames;

std::vector<StereoFrame> MakeInput() {
    std::vector<StereoFrame> frames(kFrames);
    const double twoPi = 2.0 * M_PI;
    double phaseLeft = 0.0;
    double phaseRight = 0.0;
    for (std::size_t i = 0; i < kTwoSeconds; ++i) {
        frames[i].left = static_cast<short>(std::sin(phaseLeft) * 0.5f * 32767.0);
        frames[i].right = static_cast<short>(std::sin(phaseRight) * 0.5f * 32767.0);
        phaseLeft += twoPi * 220.0f / 32000.0f;
        phaseRight += twoPi * 330.0f / 32000.0f;
        if (phaseLeft >= twoPi) phaseLeft -= twoPi;
        if (phaseRight >= twoPi) phaseRight -= twoPi;
    }
    return frames;
}

// The Module driven directly, as wf_render_clouds does.
std::vector<StereoFrame> RenderModule(const std::vector<StereoFrame>& input, Quality quality,
                                      PlaybackMode mode) {
    std::vector<StereoFrame> output(kFrames);
    Module module;
    const wf::clouds::EngineMemory memory{
        {largeArena, sizeof(largeArena)},
        {smallArena, sizeof(smallArena)},
    };
    if (module.Initialize(memory, quality, Engine::kRandomSeed) != wf::clouds::InitStatus::Ok ||
        module.SetPlaybackMode(mode) != wf::clouds::PlaybackModeStatus::Ok) {
        return output;
    }
    module.Service();

    wf::clouds::ControlSnapshot controls{};
    controls.dryWet = 1.0f;
    wf::clouds::AudioBlock in{};
    wf::clouds::AudioBlock out{};
    for (std::size_t i = 0; i < kFrames; i += kBlock) {
        if (i == kTwoSeconds) controls.freeze = true;
        if (i == kFourSeconds) controls.pitch = 7.0f;
        for (std::size_t j = 0; j < kBlock; ++j) in[j] = input[i + j];
        module.ProcessBlock(in, out, controls, wf::clouds::BlockEvents{});
        module.Service();
        for (std::size_t j = 0; j < kBlock; ++j) output[i + j] = out[j];
    }
    return output;
}

// The same timeline through the rig and the Engine at 32 kHz, in 32-frame
// callbacks, with every setting sent as an event.
std::vector<StereoFrame> RenderEngine(const std::vector<StereoFrame>& input, Quality quality,
                                      PlaybackMode mode) {
    std::vector<StereoFrame> output(kFrames);
    wf::Rig& base = rig;
    if (base.Prepare(32000.0f, kBlock, kArena) != wf::PrepareStatus::Ok) return output;
    SetParam(Engine::kQuality, static_cast<float>(quality));
    SetParam(Engine::kPlaybackMode, static_cast<float>(mode));
    SetParam(Engine::kDryWet, 1.0f);

    float left[kBlock];
    float right[kBlock];
    for (std::size_t i = 0; i < kFrames; i += kBlock) {
        if (i == kTwoSeconds) SetParam(Engine::kFreeze, 1.0f);
        if (i == kFourSeconds) SetParam(Engine::kPitch, 7.0f);
        for (std::size_t j = 0; j < kBlock; ++j) {
            left[j] = static_cast<float>(input[i + j].left) / 32768.0f;
            right[j] = static_cast<float>(input[i + j].right) / 32768.0f;
        }
        base.Process(left, right, left, right, kBlock);
        for (std::size_t j = 0; j < kBlock; ++j) {
            // Exact: the adapter produced these from int16 samples.
            output[i + j].left = static_cast<std::int16_t>(left[j] * 32768.0f);
            output[i + j].right = static_cast<std::int16_t>(right[j] * 32768.0f);
        }
    }
    return output;
}

// Through the Engine, every quality and mode must give the Module's own
// render, delayed by the block adapter's 32 frames: the wrapper adds events,
// IN GAIN and the adapters, but never changes the sound. Each render also
// prepares the same Engine again, which must start it afresh.
void CheckRendersMatchModule() {
    const std::vector<StereoFrame> input = MakeInput();
    const char* qualityNames[] = {"stereo16", "mono16", "stereo-mulaw", "mono-mulaw"};
    const char* modeNames[] = {"granular", "stretch", "looping-delay", "spectral"};
    int matched = 0;
    for (int q = 0; q < 4; ++q) {
        for (int m = 0; m < 4; ++m) {
            const auto quality = static_cast<Quality>(q);
            const auto mode = static_cast<PlaybackMode>(m);
            const std::vector<StereoFrame> expected = RenderModule(input, quality, mode);
            const std::vector<StereoFrame> actual = RenderEngine(input, quality, mode);

            bool same = true;
            for (std::size_t j = 0; j < kBlock && same; ++j) {
                same = actual[j].left == 0 && actual[j].right == 0;
            }
            std::size_t first = 0;
            for (std::size_t j = 0; j + kBlock < kFrames && same; ++j) {
                same = actual[j + kBlock].left == expected[j].left &&
                       actual[j + kBlock].right == expected[j].right;
                first = j;
            }
            std::size_t nonzero = 0;
            for (const StereoFrame& f : expected) nonzero += (f.left != 0 || f.right != 0);
            if (!same) {
                std::cerr << "FAIL: " << qualityNames[q] << ' ' << modeNames[m]
                          << " differs from the Module from frame " << first << '\n';
                ++failures;
            } else if (nonzero == 0 && q < 2) {
                std::cerr << "FAIL: " << qualityNames[q] << ' ' << modeNames[m]
                          << " is silent, so the comparison proves nothing\n";
                ++failures;
            } else {
                ++matched;
            }
        }
    }
    std::cout << matched << " of 16 renders match the Module\n";
}

// IN GAIN scales the input before the engine: fully dry, -18 dB gives an
// output 18 dB lower (the dry path is linear well below full scale).
float DryPeak(float gainDb) {
    wf::Rig& base = rig;
    base.Prepare(48000.0f, 256, kArena);
    SetParam(Engine::kDryWet, 0.0f);
    SetParam(Engine::kInputGain, gainDb);
    float left[256];
    float right[256];
    float peak = 0.0f;
    double phase = 0.0;
    for (int block = 0; block < 375; ++block) {  // two seconds
        for (std::size_t j = 0; j < 256; ++j) {
            left[j] = right[j] = 0.25f * static_cast<float>(std::sin(phase));
            phase += 2.0 * M_PI * 440.0 / 48000.0;
        }
        base.Process(left, right, left, right, 256);
        if (block >= 188) {  // the second second
            for (std::size_t j = 0; j < 256; ++j) {
                peak = std::fmax(peak, std::fmax(std::fabs(left[j]), std::fabs(right[j])));
            }
        }
    }
    return peak;
}

void CheckInputGainAt48k() {
    const float unity = DryPeak(0.0f);
    const float reduced = DryPeak(-18.0f);
    const float ratio = reduced / unity;
    const float expected = std::pow(10.0f, -18.0f / 20.0f);
    Expect(wf::IsFinite(unity) && unity > 0.01f, "48 kHz output is finite and not silent");
    if (!(std::fabs(ratio - expected) < 0.05f * expected)) {
        std::cerr << "FAIL: -18 dB IN GAIN scaled the output by " << ratio << ", expected "
                  << expected << '\n';
        ++failures;
    }
}

// One second at 48 kHz, through both adapters, in callbacks of uneven sizes
// up to 64 frames, with freeze and pitch changing midway: in place, each
// output its input as on Bela and in a rig's chain, or into separate
// buffers.
std::vector<float> RenderAt48k(PlaybackMode mode, bool inPlace) {
    constexpr std::size_t kMaxFrames = 64;
    constexpr std::size_t kTotal = 48000;
    const std::size_t sizes[] = {37, 64, 5, 1, 64, 50};
    std::vector<float> output(2 * kTotal);
    wf::Rig& base = rig;
    if (base.Prepare(48000.0f, kMaxFrames, kArena) != wf::PrepareStatus::Ok) return output;
    SetParam(Engine::kPlaybackMode, static_cast<float>(mode));
    SetParam(Engine::kDryWet, 0.7f);

    float inLeft[kMaxFrames], inRight[kMaxFrames], outLeft[kMaxFrames], outRight[kMaxFrames];
    double phase = 0.0;
    for (std::size_t done = 0, call = 0; done < kTotal; ++call) {
        const std::size_t count = std::min(sizes[call % 6], kTotal - done);
        if (done <= kTotal / 2 && kTotal / 2 < done + count) {
            SetParam(Engine::kFreeze, 1.0f);
            SetParam(Engine::kPitch, -5.0f);
        }
        for (std::size_t i = 0; i < count; ++i) {
            phase += 0.031;
            inLeft[i] = 0.5f * static_cast<float>(std::sin(phase));
            inRight[i] = 0.4f * static_cast<float>(std::sin(1.7 * phase));
            outLeft[i] = outRight[i] = 123.0f;
        }
        if (inPlace) {
            base.Process(inLeft, inRight, inLeft, inRight, count);
        } else {
            base.Process(inLeft, inRight, outLeft, outRight, count);
        }
        const float* left = inPlace ? inLeft : outLeft;
        const float* right = inPlace ? inRight : outRight;
        for (std::size_t i = 0; i < count; ++i) {
            output[2 * (done + i)] = left[i];
            output[2 * (done + i) + 1] = right[i];
        }
        done += count;
    }
    return output;
}

// A rig chains engines in place (2026-10-25), so the engine must give the
// same output either way, bit for bit, in every mode. Prepare() seeds
// stmlib::Random, so both renders draw the same numbers.
void CheckInPlace() {
    const PlaybackMode modes[] = {PlaybackMode::Granular, PlaybackMode::Stretch, PlaybackMode::LoopingDelay,
                                  PlaybackMode::Spectral};
    for (const PlaybackMode mode : modes) {
        const std::vector<float> separate = RenderAt48k(mode, false);
        const std::vector<float> inPlace = RenderAt48k(mode, true);
        float peak = 0.0f;
        for (const float sample : separate) peak = std::max(peak, std::fabs(sample));
        Expect(peak > 0.01f, "the in-place renders are not silent");
        if (std::memcmp(separate.data(), inPlace.data(), separate.size() * sizeof(float)) != 0) {
            std::cerr << "FAIL: in-place output differs from separate buffers in mode "
                      << static_cast<int>(mode) << '\n';
            ++failures;
        }
    }
}

}  // namespace

int main() {
    CheckDescription();
    CheckPrepare();
    CheckDefaultsAndEvents();
    CheckFreezeRelease();
    CheckParamValue();
    CheckRendersMatchModule();
    CheckInputGainAt48k();
    CheckInPlace();

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
