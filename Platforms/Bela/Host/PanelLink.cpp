#include "PanelLink.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace wf {
namespace bela {

namespace {

// The first callbacks read full scale on every wired input (panel probe,
// 2026-10-24).
constexpr float kWarmUpSeconds = 0.1f;

}  // namespace

bool PanelLink::Start(const PanelWiring& wiring, const BelaContext* context) {
    wiring_ = wiring;
    if (wiring_.panel == nullptr) return true;

    knobCount_ = static_cast<int>(wiring_.panel->KnobCount());
    buttonCount_ = static_cast<int>(wiring_.panel->ButtonCount());
    if (knobCount_ > kMaxKnobs || buttonCount_ > kMaxButtons) {
        fprintf(stderr, "Error: the panel has more controls than the board has pins\n");
        return false;
    }
    if (knobCount_ > 0 && context->analogFrames == 0) {
        fprintf(stderr, "Error: the panel needs Bela's analog inputs enabled\n");
        return false;
    }
    for (int i = 0; i < knobCount_; ++i) {
        if (wiring_.knobChannels[i] < 0 || wiring_.knobChannels[i] >= static_cast<int>(context->analogInChannels)) {
            fprintf(stderr, "Error: the panel's knob %d is on analog input %d, of %u\n", i, wiring_.knobChannels[i],
                    static_cast<unsigned int>(context->analogInChannels));
            return false;
        }
    }
    for (int i = 0; i < buttonCount_; ++i) {
        if (wiring_.buttonPins[i] < 0 || wiring_.buttonPins[i] >= static_cast<int>(context->digitalChannels)) {
            fprintf(stderr, "Error: the panel's button %d is on digital pin %d, of %u\n", i, wiring_.buttonPins[i],
                    static_cast<unsigned int>(context->digitalChannels));
            return false;
        }
    }

    const float callbackSeconds = static_cast<float>(context->audioFrames) / context->audioSampleRate;
    wiring_.panel->Prepare(callbackSeconds * 1000.0f);
    warmUpCallbacks_ = static_cast<unsigned int>(std::ceil(kWarmUpSeconds / callbackSeconds));
    return true;
}

void PanelLink::Update(BelaContext* context, Rig& rig, Published& published) noexcept {
    if (wiring_.panel == nullptr) return;
    if (warmUpCallbacks_ > 1) {
        --warmUpCallbacks_;
        return;
    }
    warmUpCallbacks_ = 0;

    // Non-interleaved, as the host asks for (Bela_userSettings()).
    float knobs[kMaxKnobs];
    const unsigned int analogFrames = context->analogFrames;
    for (int i = 0; i < knobCount_; ++i) {
        float sum = 0.0f;
        for (unsigned int frame = 0; frame < analogFrames; ++frame) {
            sum += analogReadNI(context, frame, wiring_.knobChannels[i]);
        }
        knobs[i] = sum / static_cast<float>(analogFrames);
    }

    bool buttons[kMaxButtons];
    for (int i = 0; i < buttonCount_; ++i) {
        pinMode(context, 0, wiring_.buttonPins[i], INPUT);
        buttons[i] = digitalRead(context, 0, wiring_.buttonPins[i]) != 0;
    }

    wiring_.panel->Update(knobs, buttons, rig);

    const controls::Panel::Focus focus = wiring_.panel->CurrentFocus();
    published.panelFocus.store(focus.active ? PackFocus(focus.slot, focus.id) : -1, std::memory_order_relaxed);
}

}  // namespace bela
}  // namespace wf
