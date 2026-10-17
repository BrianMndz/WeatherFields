#include <cmath>
#include <limits>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <string>

#include "wf/clouds/Engine.h"

alignas(clouds_engine::kMemoryAlignment)
static std::byte largeArena[clouds_engine::kLargeMemoryBytes]{};

alignas(clouds_engine::kMemoryAlignment)
static std::byte smallArena[clouds_engine::kSmallMemoryBytes]{};

static clouds_engine::EngineMemory engine_memory {
    {largeArena, sizeof(largeArena)},
    {smallArena, sizeof(smallArena)}
};

// Clouds runs its audio path at 32 kHz; Process() always expects this rate.
static constexpr float kSampleRate = 32000.0f;
static constexpr float kDurationSeconds = 6.0f;
static constexpr float kFrequencyLeft = 220.0f;
static constexpr float kFrequencyRight = 330.0f;
// -6 dBFS, leaving headroom so the wet signal does not clip on sum.
static constexpr float kAmplitude = 0.5f;

static constexpr size_t kTwoSecondsFrame = 64000;
static constexpr size_t kFourSecondsFrame = 128000;

// Fills `frames` with a stereo sine pair: `freq_l` in the left channel and
// `freq_r` in the right. Phase accumulators are kept in double and wrapped so
// phase error does not drift over long renders.
static std::vector<clouds_engine::StereoFrame> GenerateStereoSine(
    size_t num_frames, float freq_l, float freq_r, float amplitude) {
    std::vector<clouds_engine::StereoFrame> frames(num_frames);

    const double two_pi = 2.0 * M_PI;
    const double increment_l = two_pi * freq_l / kSampleRate;
    const double increment_r = two_pi * freq_r / kSampleRate;
    double phase_l = 0.0;
    double phase_r = 0.0;

    for (size_t i = 0; i < num_frames; ++i) {
        // 32767 rather than 32768: keeps full-scale input inside int16 range.
        frames[i].left = static_cast<short>(std::sin(phase_l) * amplitude * 32767.0);
        frames[i].right = static_cast<short>(std::sin(phase_r) * amplitude * 32767.0);

        phase_l += increment_l;
        phase_r += increment_r;
        if (phase_l >= two_pi) phase_l -= two_pi;
        if (phase_r >= two_pi) phase_r -= two_pi;
    }

    return frames;
}

static bool ParseQuality(const std::string& name, clouds_engine::Quality& quality) {
    using clouds_engine::Quality;

    if (name == "stereo16") quality = Quality::Stereo16Bit32k;
    else if (name == "mono16") quality = Quality::Mono16Bit32k;
    else if (name == "stereo-mulaw") quality = Quality::StereoMuLaw16k;
    else if (name == "mono-mulaw") quality = Quality::MonoMuLaw16k;
    else return false;

    return true;
}

static bool ParsePlaybackMode(const std::string& name, clouds_engine::PlaybackMode& mode) {
    using clouds_engine::PlaybackMode;

    if (name == "granular") mode = PlaybackMode::Granular;
    else if (name == "stretch") mode = PlaybackMode::Stretch;
    else if (name == "looping-delay") mode = PlaybackMode::LoopingDelay;
    else if (name == "spectral") mode = PlaybackMode::Spectral;
    else return false;

    return true;
}

// Minimal 16-bit PCM stereo WAV writer, so the render can be listened to.
static bool WriteWav(const std::string& path,
                     const std::vector<clouds_engine::StereoFrame>& frames) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open " << path << " for writing\n";
        return false;
    }

    const uint16_t num_channels = 2;
    const uint16_t bits_per_sample = 16;
    const uint32_t sample_rate = static_cast<uint32_t>(kSampleRate);
    const uint32_t byte_rate = sample_rate * num_channels * bits_per_sample / 8;
    const uint16_t block_align = num_channels * bits_per_sample / 8;
    const uint32_t data_size =
        static_cast<uint32_t>(frames.size()) * block_align;

    auto u32 = [&out](uint32_t v) { out.write(reinterpret_cast<char*>(&v), 4); };
    auto u16 = [&out](uint16_t v) { out.write(reinterpret_cast<char*>(&v), 2); };

    out.write("RIFF", 4);
    u32(36 + data_size);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    u32(16);
    u16(1);  // PCM
    u16(num_channels);
    u32(sample_rate);
    u32(byte_rate);
    u16(block_align);
    u16(bits_per_sample);

    out.write("data", 4);
    u32(data_size);
    for (const auto&[left, right] : frames) {
        u16(static_cast<uint16_t>(left));
        u16(static_cast<uint16_t>(right));
    }

    return out.good();
}

static int Init(clouds_engine::Engine &engine, const clouds_engine::Quality quality) {
    auto err = engine.Initialize(engine_memory, quality, 0x23u);
    if (err != clouds_engine::InitStatus::Ok) return 1;

    return 0;
}

static int Usage() {
    std::cerr
        << "Usage: wf_render_clouds [stereo16|mono16|stereo-mulaw|mono-mulaw] "
           "[--mode granular|stretch|looping-delay|spectral]\n";
    return 2;
}

int main(int argc, char *argv[]) {
    std::string qualityName = "stereo16";
    std::string modeName = "granular";
    bool qualityGiven = false;
    for (int arg = 1; arg < argc; ++arg) {
        const std::string option = argv[arg];
        if (option == "--mode") {
            if (++arg == argc) return Usage();
            modeName = argv[arg];
        } else if (!qualityGiven) {
            qualityName = option;
            qualityGiven = true;
        } else {
            return Usage();
        }
    }

    auto quality = clouds_engine::Quality::Stereo16Bit32k;
    auto mode = clouds_engine::PlaybackMode::Granular;
    if (!ParseQuality(qualityName, quality) || !ParsePlaybackMode(modeName, mode)) return Usage();

    clouds_engine::Engine processor;
    if (Init(processor, quality) != 0) return 1;

    // The engine starts in granular mode; another mode takes effect at the
    // next Service() call.
    if (processor.SetPlaybackMode(mode) != clouds_engine::PlaybackModeStatus::Ok) return 1;
    processor.Service();

    clouds_engine::ControlSnapshot controls{};
    controls.position = 0.5f;
    controls.size = 0.5f;
    controls.pitch = 0.0f;
    controls.density = 0.75f;
    controls.texture = 0.5f;
    controls.dryWet = 1.0f;
    controls.stereoSpread = 0.0f;
    controls.feedback = 0.0f;
    controls.reverb = 0.0f;
    controls.freeze = false;
    controls.gate = false;

    const size_t num_frames =
        static_cast<size_t>(kSampleRate * kDurationSeconds);

    auto input = GenerateStereoSine(
        num_frames, kFrequencyLeft, kFrequencyRight, kAmplitude);
    // Silence after 2 seconds:
    for (size_t i = kTwoSecondsFrame; i < input.size(); ++i) {
        input[i].left = 0;
        input[i].right = 0;
    }

    std::vector<clouds_engine::StereoFrame> output(num_frames);
    clouds_engine::AudioBlock inputBlock{};
    clouds_engine::AudioBlock outputBlock{};

    std::printf("Rendering %.1f s @ %.0f Hz — L %.0f Hz / R %.0f Hz, %s mode\n",
        kDurationSeconds, kSampleRate, kFrequencyLeft, kFrequencyRight, modeName.c_str());

    // Each block must finish within its own duration, 1 ms at 32 kHz, for a
    // host that services the engine on the audio thread.
    using Clock = std::chrono::steady_clock;
    const double blockBudgetUs = 1e6 * clouds_engine::kBlockFrames / kSampleRate;
    double serviceMaxUs = 0.0;
    double serviceTotalUs = 0.0;
    double blockMaxUs = 0.0;
    size_t blockCount = 0;

    // Inverted sentinels: any real sample must move each bound inward.
    short leftMin = std::numeric_limits<short>::max();
    short leftMax = std::numeric_limits<short>::min();
    short rightMin = std::numeric_limits<short>::max();
    short rightMax = std::numeric_limits<short>::min();
    size_t nonzeroFrameCount = 0;

    bool twoSecChange = false;
    bool fourSecChange = false;

    for (size_t i = 0; i < num_frames; i += clouds_engine::kBlockFrames) {
        for (std::size_t j = 0; j < clouds_engine::kBlockFrames; ++j) {
            inputBlock[j] = input[i + j];
        }

        if (i >= kTwoSecondsFrame && !twoSecChange) {
            controls.freeze = true;
            controls.pitch = 0.0f;
            twoSecChange = true;
            std::printf("2-sec changed!\n");
        }

        if (i >= kFourSecondsFrame && !fourSecChange) {
            controls.freeze = true;
            controls.pitch = 7.0f;
            fourSecChange = true;
            std::printf("4-sec changed!\n");
        }

        const auto blockStart = Clock::now();
        processor.ProcessBlock(inputBlock, outputBlock, controls, clouds_engine::BlockEvents{});
        const auto serviceStart = Clock::now();
        processor.Service();
        const auto blockEnd = Clock::now();

        const double serviceUs =
            std::chrono::duration<double, std::micro>(blockEnd - serviceStart).count();
        serviceMaxUs = std::max(serviceMaxUs, serviceUs);
        serviceTotalUs += serviceUs;
        blockMaxUs = std::max(blockMaxUs,
            std::chrono::duration<double, std::micro>(blockEnd - blockStart).count());
        ++blockCount;

        for (std::size_t j = 0; j < clouds_engine::kBlockFrames; ++j) {
            output[i + j] = outputBlock[j];
        }

        for (size_t frameWithinBlock = 0; frameWithinBlock < clouds_engine::kBlockFrames; ++frameWithinBlock) {
            const clouds_engine::StereoFrame& frame = output[i + frameWithinBlock];

            leftMin = std::min(leftMin, frame.left);
            leftMax = std::max(leftMax, frame.left);
            rightMin = std::min(rightMin, frame.right);
            rightMax = std::max(rightMax, frame.right);

            if (frame.left != 0 || frame.right != 0) {
                ++nonzeroFrameCount;
            }
        }
    }

    std::printf("left  min %6d  max %6d\n", leftMin, leftMax);
    std::printf("right min %6d  max %6d\n", rightMin, rightMax);
    std::printf("nonzero frames %zu / %zu\n", nonzeroFrameCount, num_frames);
    std::printf("Service() mean %.1f us, max %.1f us; worst block %.1f us of %.0f us\n",
        serviceTotalUs / static_cast<double>(blockCount), serviceMaxUs, blockMaxUs, blockBudgetUs);

    const std::string outputPath = "clouds_output_" + qualityName + "_" + modeName + ".wav";
    if (!WriteWav("clouds_input.wav", input)) return 1;
    if (!WriteWav(outputPath, output)) return 1;
    std::cout << "Wrote clouds_input.wav and " << outputPath << '\n';

    return 0;
}
