#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "wf/Events.h"
#include "wf/Rig.h"
#include "wf/SpscQueue.h"

namespace wf {

// One producer's way to the audio thread: the MIDI thread, the GUI, later
// the scene loader each own one. The audio thread drains every input once
// per callback, always in the same order, before Rig::Process(); that thread
// is then the only one changing the rig.
//
// A drain handles at most `maxEvents`, and never more than the input held
// when the drain began, so a producer refilling it cannot stretch the
// callback; the rest waits for the next one. The host chooses `maxEvents`
// from its callback budget, counting Handle() costs (see Engine::Handle()).
//
// The counters only grow, wrapping at 2^32; any thread may read them.
template <std::size_t Capacity>
class ControlInput {
#if defined(__GNUC__)
    static_assert(__atomic_always_lock_free(sizeof(std::uint32_t), nullptr),
                  "the counters must be lock-free atomics");
#endif

public:
    ControlInput() = default;
    ControlInput(const ControlInput&) = delete;
    ControlInput& operator=(const ControlInput&) = delete;

    // Producer thread only. Returns false, counting an overflow, when the
    // queue is full. What to do then is the producer's policy: the GUI keeps
    // its latest value per parameter and retries.
    bool Push(const ControlEvent& event) noexcept {
        if (queue_.TryPush(event)) return true;
        overflows_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Consumer (audio) thread only. Hands queued events to `rig` in order,
    // within the limits above, counting the ones the rig drops. Returns how
    // many it took from the queue.
    std::size_t Drain(Rig& rig, std::size_t maxEvents) noexcept {
        std::size_t count = queue_.PopAvailable();
        if (count > maxEvents) count = maxEvents;
        ControlEvent event;
        for (std::size_t i = 0; i < count; ++i) {
            queue_.TryPop(event);  // cannot fail: PopAvailable() said so
            if (!rig.Handle(event)) dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        return count;
    }

    // Consumer side, when no Drain() can run: on the audio thread, or on the
    // host's thread while the callback is stopped. Drops every queued event,
    // for example those for a rig being replaced. Returns how many.
    std::size_t Discard() noexcept {
        std::size_t count = 0;
        ControlEvent event;
        while (queue_.TryPop(event)) ++count;
        return count;
    }

    // Events Push() refused because the queue was full.
    std::uint32_t Overflows() const noexcept { return overflows_.load(std::memory_order_relaxed); }

    // Events Drain() took but the rig dropped (ConstrainEvent() refused them).
    std::uint32_t Dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    SpscQueue<ControlEvent, Capacity> queue_;
    std::atomic<std::uint32_t> overflows_{0};  // written by the producer
    std::atomic<std::uint32_t> dropped_{0};    // written by the consumer
};

}  // namespace wf
