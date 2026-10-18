// The Bela host: Bela's setup(), render() and cleanup() around the app's rig
// (BelaHost.h). Bela's Makefile builds it on the board, with the app, from
// the app's manifest (Tools/Bela/deploy.sh).

#include <Bela.h>

#include <cstdint>
#include <cstdio>
#include <memory>

#include "BelaHost.h"
#include "GuiLink.h"
#include "OledLink.h"
#include "wf/Dsp.h"
#include "wf/Engine.h"

namespace {

// render() runs on the Xenomai thread, so the published values must not fall
// back to a lock. The board's library predates is_always_lock_free.
static_assert(__atomic_always_lock_free(sizeof(float), nullptr), "published values need lock-free atomics");

// 32-frame callbacks by default (726 us at 44.1 kHz): at 16 frames Clouds
// underran with heavy settings, since one callback in two must fit a whole
// 32-frame engine block (CloudsEngine docs/bela-profile-2026-10-07.txt).
// Bela's -p option still overrides it.
constexpr int kDefaultPeriodFrames = 32;

// Per callback, from the browser panel. A Clouds quality or mode event runs
// the module's Service(), about 0.2 ms on the board; the page's pace keeps
// these rare, and the limit bounds a burst.
constexpr std::size_t kMaxGuiEventsPerCallback = 32;

// The OLED (an SH1106 128x64 module on Bela's I2C bus 1). Override with
// deploy.sh's -D. A whole frame takes about 35.5 ms to send (mean; up to 51
// ms seen, 2026-10-18), so the bus allows about 28 frames a second; 25 keeps
// frames evenly paced and leaves the bus some room for other devices.
#ifndef WF_OLED_I2C_BUS
#define WF_OLED_I2C_BUS 1
#endif
#ifndef WF_OLED_I2C_ADDRESS
#define WF_OLED_I2C_ADDRESS 0x3C
#endif
#ifndef WF_OLED_FPS
#define WF_OLED_FPS 25
#endif

// Set up in setup(), before render() and the other threads start.
wf::Rig* rig = nullptr;
std::unique_ptr<unsigned char[]> memory;
wf::bela::Published published;
wf::bela::GuiInput guiInput;
// Made in setup() and never destroyed: its WebSocket server can still call
// it while the process exits (GuiLink.h).
wf::bela::GuiLink* guiLink = nullptr;
// Its thread runs from setup() to cleanup(), which joins it.
wf::bela::OledLink oledLink;

// render() only.
wf::PeakFollower inputMeter;
wf::PeakFollower outputMeter;

const char* Describe(wf::PrepareStatus status) {
    switch (status) {
        case wf::PrepareStatus::Ok: return "ok";
        case wf::PrepareStatus::MemoryTooSmall: return "memory too small";
        case wf::PrepareStatus::MemoryMisaligned: return "memory misaligned";
        case wf::PrepareStatus::UnsupportedSampleRate: return "unsupported sample rate";
        case wf::PrepareStatus::UnsupportedBlockSize: return "unsupported block size";
    }
    return "unknown";
}

// After the callback's events, so the panel sees the changes they caused.
void Publish() {
    for (std::size_t i = 0; i < published.params.size(); ++i) {
        const wf::bela::Published::Param& param = published.params[i];
        published.values[i].store(rig->Slot(param.slot)->ParamValue(param.spec->id), std::memory_order_relaxed);
    }
}

}  // namespace

// Called by Bela before it parses the command line, which can still override
// these. Non-interleaved buffers hold every left sample, then every right
// one: the planar layout the rig takes, so render() passes Bela's buffers
// directly.
void Bela_userSettings(BelaInitSettings* settings) {
    settings->interleave = 0;
    settings->periodSize = kDefaultPeriodFrames;
}

// An ordinary thread, before audio starts: every allocation happens here.
// Any failure refuses to start rather than running silently.
bool setup(BelaContext* context, void*) {
    if (context->audioInChannels != 2 || context->audioOutChannels != 2) {
        fprintf(stderr, "Error: WeatherFields needs exactly two audio inputs and two audio outputs\n");
        return false;
    }
    if (context->flags & BELA_FLAG_INTERLEAVED) {
        fprintf(stderr, "Error: WeatherFields needs non-interleaved audio buffers\n");
        return false;
    }

    rig = &wf::bela::AppRig();
    const float sampleRate = context->audioSampleRate;
    const std::size_t frames = context->audioFrames;

    // The rig's memory, aligned by hand: C++14's new does not honour
    // alignments above alignof(std::max_align_t).
    const std::size_t bytes = rig->MemoryBytes(sampleRate, frames);
    memory.reset(new unsigned char[bytes + wf::kMemoryAlignment]());
    const auto address = reinterpret_cast<std::uintptr_t>(memory.get());
    const std::size_t offset = (wf::kMemoryAlignment - address % wf::kMemoryAlignment) % wf::kMemoryAlignment;
    const wf::PrepareStatus status = rig->Prepare(sampleRate, frames, wf::MemoryBlock{memory.get() + offset, bytes});
    if (status != wf::PrepareStatus::Ok) {
        fprintf(stderr, "Error: %s could not prepare at %.0f Hz, %u frames: %s\n", rig->Name(), sampleRate,
                static_cast<unsigned int>(frames), Describe(status));
        return false;
    }

    for (std::size_t slot = 0; slot < rig->SlotCount(); ++slot) {
        const wf::ParamTable& table = rig->Slot(slot)->Params();
        for (std::size_t i = 0; i < table.count; ++i) {
            published.params.push_back({static_cast<std::uint8_t>(slot), &table.specs[i]});
        }
    }
    published.values.reset(new std::atomic<float>[published.params.size()]);
    Publish();

    inputMeter.Configure(sampleRate);
    outputMeter.Configure(sampleRate);

    guiLink = new wf::bela::GuiLink(*rig, published, guiInput);
    guiLink->Start(context->projectName);

    // Without a panel, the board runs as before.
    oledLink.Start(WF_OLED_I2C_BUS, WF_OLED_I2C_ADDRESS, WF_OLED_FPS);

    printf("WeatherFields: %s running at %.0f Hz, %u-frame callbacks; panel at http://bela.local/gui\n",
           rig->Name(), sampleRate, static_cast<unsigned int>(frames));
    return true;
}

// The Xenomai real-time thread: no allocation, locks, logging or system
// calls. Bela's input buffers are read-only and its outputs separate, which
// Engine::Process() allows.
void render(BelaContext* context, void*) {
    guiInput.Drain(*rig, kMaxGuiEventsPerCallback);
    Publish();

    const unsigned int frames = context->audioFrames;
    const float* inLeft = context->audioIn;
    const float* inRight = context->audioIn + frames;
    float* outLeft = context->audioOut;
    float* outRight = context->audioOut + frames;

    inputMeter.Process(inLeft, inRight, frames);
    rig->Process(inLeft, inRight, outLeft, outRight, frames);
    outputMeter.Process(outLeft, outRight, frames);
    published.inputLevel.store(inputMeter.Level(), std::memory_order_relaxed);
    published.outputLevel.store(outputMeter.Level(), std::memory_order_relaxed);
}

// Audio has stopped. The rig and its memory have static storage and outlive
// this; the panel's link closes, and its sender thread ends; the OLED goes
// dark and its thread ends.
void cleanup(BelaContext*, void*) {
    if (guiLink != nullptr) guiLink->Stop();
    oledLink.Stop();
}
