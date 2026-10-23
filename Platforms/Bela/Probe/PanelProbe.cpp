// The panel probe: a Bela program, not an app (no rig, no host), that
// measures the hardware controls before any code depends on them. Every two
// seconds it prints, per analog input, the mean, the noise (peak-to-peak, raw
// and averaged over each callback's analog frames, which is what the knob
// conditioning will see) and the range seen since start; per button, the
// presses and releases, the shortest press and how much they bounce.
//
// The pots sit across the 3.3 V rail, but the ADC measures against its own
// 4.096 V reference, so rail ripple and drift show up in every reading (the
// first run, 2026-10-24: noise proportional to the reading). With the rail
// itself wired to the reference input (A7), the probe also prints each
// input's ratio to it, in which the rail's share cancels.
//
// render() gathers each window and hands it to an ordinary thread through
// wf::SpscQueue, which prints it, as render() may not.

#include <Bela.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "wf/SpscQueue.h"

namespace {

constexpr int kMaxAnalogChannels = 8;
constexpr int kButtons = 2;
constexpr int kButtonPins[kButtons] = {0, 1};
const char* const kButtonNames[kButtons] = {"FREEZE", "B"};

constexpr float kWindowSeconds = 2.0f;
// The first callbacks read full scale on every wired input; skip them.
constexpr float kWarmUpSeconds = 0.1f;
// Edges closer together than this belong to one press or release: a bounce.
// Above a tactile switch's few milliseconds; a quick tap was 27 ms.
constexpr float kBurstGapSeconds = 0.010f;
// The 3.3 V rail, if wired; below this level the input is taken as unwired.
constexpr int kReferenceChannel = 7;
constexpr float kReferenceMinimum = 0.5f;
// Bela's analog values are the ADC's 16-bit codes over 65536.
constexpr float kAdcSteps = 65536.0f;

struct AnalogStats {
    double sum;
    float min, max;             // raw samples, this window
    float blockMin, blockMax;   // callback means, this window
    float everMin, everMax;     // callback means, since start
};

// The same, for the ratio of an input to the reference, sample by sample
// (they are converted microseconds apart) and callback mean by mean.
struct RatioStats {
    double sum;
    float min, max;
    float blockMin, blockMax;
    float everMin, everMax;
};

struct ButtonStats {
    int state;  // at the window's end
    std::uint32_t presses, releases;
    std::uint32_t glitches;  // bursts ending where they began: noise, not a press
    std::uint32_t bounced;   // presses or releases with more than one edge
    std::uint32_t maxEdges;  // in one burst
    float maxBurstMs;        // first edge to last, in one burst
    float minPressMs;        // press to release, the shortest; 0 for none
};

struct Report {
    std::uint32_t seconds;
    std::uint32_t samples;  // analog samples per channel in the window
    int analogChannels;
    AnalogStats analog[kMaxAnalogChannels];
    bool referenced;  // the reference input reads as wired, the whole window
    RatioStats ratio[kReferenceChannel];
    ButtonStats buttons[kButtons];
    std::uint32_t dropped;  // reports the printer had no room for, since start
};

// render() only.
struct ButtonTracker {
    int state = 0;
    bool open = false;  // a burst is in progress
    int burstFrom = 0;
    std::uint32_t edges = 0;
    std::uint64_t firstEdge = 0, lastEdge = 0;
    std::uint64_t pressedAt = 0;
    bool pressSeen = false;
};

Report window;
ButtonTracker trackers[kButtons];
float everMin[kMaxAnalogChannels];
float everMax[kMaxAnalogChannels];
float everRatioMin[kReferenceChannel];
float everRatioMax[kReferenceChannel];
std::uint64_t frame = 0;  // audio frames since start
std::uint64_t windowEnd = 0;
std::uint64_t windowFrames = 0;
std::uint64_t warmUpFrames = 0;
std::uint64_t burstGapFrames = 0;
float framesPerMs = 0.0f;
std::uint32_t dropped = 0;

wf::SpscQueue<Report, 4> reports;
std::atomic<bool> running{false};
std::thread printer;

void ResetWindow(int channels) {
    window.samples = 0;
    window.analogChannels = channels;
    for (int c = 0; c < kMaxAnalogChannels; ++c) {
        AnalogStats& a = window.analog[c];
        a.sum = 0.0;
        a.min = a.blockMin = 1.0f;
        a.max = a.blockMax = 0.0f;
    }
    window.referenced = channels > kReferenceChannel;
    for (int c = 0; c < kReferenceChannel; ++c) {
        RatioStats& r = window.ratio[c];
        r.sum = 0.0;
        r.min = r.blockMin = 2.0f;
        r.max = r.blockMax = 0.0f;
    }
    for (int b = 0; b < kButtons; ++b) {
        ButtonStats& s = window.buttons[b];
        s = ButtonStats{};
        s.state = trackers[b].state;
    }
}

void CloseBurst(ButtonTracker& t, ButtonStats& s) {
    t.open = false;
    if (t.state == t.burstFrom) {
        ++s.glitches;
    } else if (t.state != 0) {
        ++s.presses;
        t.pressedAt = t.firstEdge;
        t.pressSeen = true;
    } else {
        ++s.releases;
        if (t.pressSeen) {
            const float ms = static_cast<float>(t.firstEdge - t.pressedAt) / framesPerMs;
            if (s.minPressMs == 0.0f || ms < s.minPressMs) s.minPressMs = ms;
        }
    }
    if (t.edges > 1 && t.state != t.burstFrom) ++s.bounced;
    s.maxEdges = std::max(s.maxEdges, t.edges);
    s.maxBurstMs = std::max(s.maxBurstMs, static_cast<float>(t.lastEdge - t.firstEdge) / framesPerMs);
}

void Print(const Report& r) {
    printf("\n[%5u s] analog: mean, noise peak-to-peak in ADC steps (raw / callback mean), range since start\n",
           static_cast<unsigned int>(r.seconds));
    for (int c = 0; c < r.analogChannels; ++c) {
        const AnalogStats& a = r.analog[c];
        printf("  A%d  mean %.5f  pp %5.0f / %5.0f  range %.5f - %.5f\n", c,
               r.samples > 0 ? a.sum / r.samples : 0.0, (a.max - a.min) * kAdcSteps,
               (a.blockMax - a.blockMin) * kAdcSteps, a.everMin, a.everMax);
    }
    if (r.referenced) {
        printf("  ratio to A%d (in steps of 1/65536 too):\n", kReferenceChannel);
        for (int c = 0; c < kReferenceChannel; ++c) {
            const RatioStats& q = r.ratio[c];
            printf("  A%d  ratio %.5f  pp %5.0f / %5.0f  range %.5f - %.5f\n", c,
                   r.samples > 0 ? q.sum / r.samples : 0.0, (q.max - q.min) * kAdcSteps,
                   (q.blockMax - q.blockMin) * kAdcSteps, q.everMin, q.everMax);
        }
    } else if (r.analogChannels > kReferenceChannel) {
        printf("  (A%d below %.1f: no reference wired, no ratios)\n", kReferenceChannel, kReferenceMinimum);
    }
    for (int b = 0; b < kButtons; ++b) {
        const ButtonStats& s = r.buttons[b];
        printf("  D%d %-6s %s  presses %u releases %u glitches %u  bounced %u, max %u edges over %.2f ms",
               kButtonPins[b], kButtonNames[b], s.state != 0 ? "DOWN" : "up  ", s.presses, s.releases,
               s.glitches, s.bounced, s.maxEdges, s.maxBurstMs);
        if (s.minPressMs > 0.0f) printf("  shortest press %.1f ms", s.minPressMs);
        printf("\n");
    }
    if (r.dropped > 0) printf("  (%u reports dropped)\n", r.dropped);
    fflush(stdout);
}

}  // namespace

void Bela_userSettings(BelaInitSettings* settings) {
    settings->interleave = 0;
    settings->periodSize = 32;
}

bool setup(BelaContext* context, void*) {
    if (context->analogFrames == 0 || context->digitalFrames == 0) {
        fprintf(stderr, "Error: the probe needs Bela's analog and digital I/O enabled\n");
        return false;
    }
    if (context->flags & BELA_FLAG_INTERLEAVED) {
        fprintf(stderr, "Error: the probe needs non-interleaved buffers\n");
        return false;
    }
    const int channels = std::min(static_cast<int>(context->analogInChannels), kMaxAnalogChannels);
    std::fill(everMin, everMin + kMaxAnalogChannels, 1.0f);
    std::fill(everMax, everMax + kMaxAnalogChannels, 0.0f);
    std::fill(everRatioMin, everRatioMin + kReferenceChannel, 2.0f);
    std::fill(everRatioMax, everRatioMax + kReferenceChannel, 0.0f);
    windowFrames = static_cast<std::uint64_t>(kWindowSeconds * context->audioSampleRate);
    warmUpFrames = static_cast<std::uint64_t>(kWarmUpSeconds * context->audioSampleRate);
    windowEnd = warmUpFrames + windowFrames;
    burstGapFrames = static_cast<std::uint64_t>(kBurstGapSeconds * context->audioSampleRate);
    framesPerMs = context->audioSampleRate / 1000.0f;
    ResetWindow(channels);

    running = true;
    printer = std::thread([] {
        Report report;
        while (running) {
            while (reports.TryPop(report)) Print(report);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    printf("Panel probe: audio %.0f Hz, %u frames; analog %u inputs at %.0f Hz, %u frames; digital %u frames\n",
           context->audioSampleRate, static_cast<unsigned int>(context->audioFrames),
           static_cast<unsigned int>(context->analogInChannels), context->analogSampleRate,
           static_cast<unsigned int>(context->analogFrames), static_cast<unsigned int>(context->digitalFrames));
    printf("Buttons on D0 (FREEZE) and D1 (B), active high. Bursts: edges less than %.0f ms apart.\n",
           kBurstGapSeconds * 1000.0f);
    printf("Reference: the 3.3 V rail on A%d, if wired. The first %.0f ms are skipped.\n", kReferenceChannel,
           kWarmUpSeconds * 1000.0f);
    return true;
}

void render(BelaContext* context, void*) {
    const int channels = window.analogChannels;
    const unsigned int analogFrames = context->analogFrames;
    if (frame < warmUpFrames) {
        frame += context->digitalFrames;
        return;
    }
    for (int c = 0; c < channels; ++c) {
        AnalogStats& a = window.analog[c];
        float blockSum = 0.0f;
        for (unsigned int f = 0; f < analogFrames; ++f) {
            const float v = analogReadNI(context, f, c);
            blockSum += v;
            a.min = std::min(a.min, v);
            a.max = std::max(a.max, v);
        }
        a.sum += blockSum;
        const float mean = blockSum / analogFrames;
        a.blockMin = std::min(a.blockMin, mean);
        a.blockMax = std::max(a.blockMax, mean);
        everMin[c] = std::min(everMin[c], mean);
        everMax[c] = std::max(everMax[c], mean);
    }
    window.samples += analogFrames;

    if (window.referenced) {
        float referenceSum = 0.0f;
        for (unsigned int f = 0; f < analogFrames; ++f) {
            const float reference = analogReadNI(context, f, kReferenceChannel);
            referenceSum += reference;
            if (reference < kReferenceMinimum) window.referenced = false;
        }
        for (int c = 0; c < kReferenceChannel && window.referenced; ++c) {
            RatioStats& r = window.ratio[c];
            float blockSum = 0.0f;
            for (unsigned int f = 0; f < analogFrames; ++f) {
                const float v = analogReadNI(context, f, c) / analogReadNI(context, f, kReferenceChannel);
                r.sum += v;
                r.min = std::min(r.min, v);
                r.max = std::max(r.max, v);
                blockSum += analogReadNI(context, f, c);
            }
            const float mean = blockSum / referenceSum;
            r.blockMin = std::min(r.blockMin, mean);
            r.blockMax = std::max(r.blockMax, mean);
            everRatioMin[c] = std::min(everRatioMin[c], mean);
            everRatioMax[c] = std::max(everRatioMax[c], mean);
        }
    }

    // Bela's pins are inputs unless set otherwise; set them anyway, as the
    // direction lives in the digital buffer, which only render() can reach.
    for (int b = 0; b < kButtons; ++b) pinMode(context, 0, kButtonPins[b], INPUT);
    for (unsigned int f = 0; f < context->digitalFrames; ++f, ++frame) {
        for (int b = 0; b < kButtons; ++b) {
            ButtonTracker& t = trackers[b];
            ButtonStats& s = window.buttons[b];
            const int state = digitalRead(context, f, kButtonPins[b]);
            if (state != t.state) {
                if (!t.open) {
                    t.open = true;
                    t.burstFrom = t.state;
                    t.edges = 0;
                    t.firstEdge = frame;
                }
                ++t.edges;
                t.lastEdge = frame;
                t.state = state;
            } else if (t.open && frame - t.lastEdge > burstGapFrames) {
                CloseBurst(t, s);
            }
        }
    }

    if (frame >= windowEnd) {
        window.seconds = static_cast<std::uint32_t>(frame / static_cast<std::uint64_t>(context->audioSampleRate));
        for (int c = 0; c < channels; ++c) {
            window.analog[c].everMin = everMin[c];
            window.analog[c].everMax = everMax[c];
        }
        for (int c = 0; c < kReferenceChannel; ++c) {
            window.ratio[c].everMin = everRatioMin[c];
            window.ratio[c].everMax = everRatioMax[c];
        }
        for (int b = 0; b < kButtons; ++b) window.buttons[b].state = trackers[b].state;
        window.dropped = dropped;
        if (!reports.TryPush(window)) ++dropped;
        ResetWindow(channels);
        windowEnd += windowFrames;
    }
}

void cleanup(BelaContext*, void*) {
    running = false;
    if (printer.joinable()) printer.join();
}
