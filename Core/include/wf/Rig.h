#pragma once

#include <cstddef>

#include "wf/Engine.h"
#include "wf/Events.h"
#include "wf/Params.h"

namespace wf {

// Makes `event` valid for an engine with `params`, or returns false to drop
// it. Param events need a Continuous, Choice or Toggle parameter and get
// their value constrained; Trigger events need a Trigger parameter; notes
// need a MIDI note number, and their velocity is clamped to 0..1. Non-finite
// values are always dropped.
inline bool ConstrainEvent(const ParamTable& params, ControlEvent& event) noexcept {
    switch (event.type) {
        case ControlEvent::Type::Param:
        case ControlEvent::Type::Trigger: {
            const ParamSpec* spec = params.Find(event.id);
            if (spec == nullptr) return false;
            const bool isTrigger = spec->kind == ParamKind::Trigger;
            if (isTrigger != (event.type == ControlEvent::Type::Trigger)) return false;
            return ConstrainValue(*spec, event.value);
        }
        case ControlEvent::Type::NoteOn:
        case ControlEvent::Type::NoteOff:
            if (event.id > 127 || !IsFinite(event.value)) return false;
            event.value = event.value < 0.0f ? 0.0f : (event.value > 1.0f ? 1.0f : event.value);
            return true;
    }
    return false;
}

// What runs on one device: a fixed set of engines in numbered slots, and the
// audio routing between them. A host (desktop, Bela, Daisy) drives a rig
// without knowing what is inside, which is what lets the same rig run on
// every host. Lifecycle and threads as for Engine.
class Rig {
public:
    virtual ~Rig() = default;

    // Static storage; shown in the desktop host's rig menu.
    virtual const char* Name() const noexcept = 0;

    virtual std::size_t SlotCount() const noexcept = 0;

    // The engine in `slot`, or null past the last one. The host reads names
    // and parameter tables through it; changes go through Handle().
    virtual Engine* Slot(std::size_t slot) noexcept = 0;

    // The total for every slot, each part a multiple of kMemoryAlignment.
    virtual std::size_t MemoryBytes(float sampleRate, std::size_t maxFrames) const noexcept = 0;

    // Prepares every engine, giving each its part of `memory`.
    virtual PrepareStatus Prepare(float sampleRate, std::size_t maxFrames,
                                  MemoryBlock memory) noexcept = 0;

    // Real-time. The one way into an engine: the event goes to its slot only
    // if ConstrainEvent() accepts it. Returns false for a dropped event.
    bool Handle(ControlEvent event) noexcept {
        Engine* engine = Slot(event.target);
        if (engine == nullptr || !ConstrainEvent(engine->Params(), event)) return false;
        engine->Handle(event);
        return true;
    }

    // Real-time; the same buffer rules as Engine::Process().
    virtual void Process(const float* inLeft, const float* inRight,
                         float* outLeft, float* outRight, std::size_t frames) noexcept = 0;
};

// The rig for one engine on its own: input to E to output, E in slot 0. It
// holds E by value, so no rig is written per engine and calls to E are
// direct.
template <typename E>
class SoloRig final : public Rig {
public:
    // The engine itself, for hosts and checks that know its type.
    E& Get() noexcept { return engine_; }

    const char* Name() const noexcept override { return engine_.Name(); }
    std::size_t SlotCount() const noexcept override { return 1; }
    Engine* Slot(std::size_t slot) noexcept override { return slot == 0 ? &engine_ : nullptr; }

    std::size_t MemoryBytes(float sampleRate, std::size_t maxFrames) const noexcept override {
        return engine_.MemoryBytes(sampleRate, maxFrames);
    }

    PrepareStatus Prepare(float sampleRate, std::size_t maxFrames,
                          MemoryBlock memory) noexcept override {
        return engine_.Prepare(sampleRate, maxFrames, memory);
    }

    void Process(const float* inLeft, const float* inRight,
                 float* outLeft, float* outRight, std::size_t frames) noexcept override {
        engine_.Process(inLeft, inRight, outLeft, outRight, frames);
    }

private:
    E engine_;
};

}  // namespace wf
