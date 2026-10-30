#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include "wf/ControlInput.h"
#include "wf/Events.h"
#include "wf/Params.h"

namespace wf {

// The producer side of a ControlInput for a source that must not lose events
// when the input is full, such as a GUI. Events leave in the order sent; when
// the input is full they wait, in that order, for Flush(). Only a Continuous
// parameter's events coalesce: a new value replaces the waiting one and moves
// to the back, which is safe because setting one has no side effects
// (wf/Params.h). Every other event waits on its own, so each Choice or Toggle
// transition and each trigger or note arrives as sent.
//
// Not real-time: waiting events are kept in a std::deque. All calls come from
// the producer side of the input: one thread, or several that hold one lock.
template <std::size_t Capacity>
class ControlOutbox {
public:
    explicit ControlOutbox(ControlInput<Capacity>& input) : input_(input) {}

    ControlOutbox(const ControlOutbox&) = delete;
    ControlOutbox& operator=(const ControlOutbox&) = delete;

    // `kind` is the kind of the parameter a Param event sets; it is ignored
    // for other events. Returns true when the event replaced a waiting one
    // (a Continuous value), so one event fewer will arrive than were sent.
    bool Send(const ControlEvent& event, ParamKind kind) {
        bool replaced = false;
        if (event.type == ControlEvent::Type::Param && kind == ParamKind::Continuous) {
            for (auto it = waiting_.begin(); it != waiting_.end(); ++it) {
                if (IsParam(*it, event.target, event.id)) {
                    waiting_.erase(it);
                    replaced = true;
                    break;
                }
            }
        }
        waiting_.push_back(event);
        Flush();
        return replaced;
    }

    // Sends what waits, until the input is full again. Call it regularly,
    // from a timer, while anything waits.
    void Flush() {
        while (!waiting_.empty() && input_.Push(waiting_.front())) waiting_.pop_front();
    }

    // Drops everything waiting, for example for a rig being replaced.
    void Clear() { waiting_.clear(); }

    bool Empty() const noexcept { return waiting_.empty(); }

    // Whether a Param event for parameter `id` in slot `target` waits.
    bool IsWaiting(std::uint8_t target, ParamId id) const noexcept {
        for (const ControlEvent& event : waiting_) {
            if (IsParam(event, target, id)) return true;
        }
        return false;
    }

    // The value of the newest waiting Param event for that parameter, if
    // one waits: what the producer's next change should build on.
    bool LatestWaiting(std::uint8_t target, ParamId id, float& value) const noexcept {
        for (auto it = waiting_.rbegin(); it != waiting_.rend(); ++it) {
            if (IsParam(*it, target, id)) {
                value = it->value;
                return true;
            }
        }
        return false;
    }

private:
    static bool IsParam(const ControlEvent& event, std::uint8_t target, ParamId id) noexcept {
        return event.type == ControlEvent::Type::Param && event.target == target && event.id == id;
    }

    ControlInput<Capacity>& input_;
    std::deque<ControlEvent> waiting_;  // oldest first
};

}  // namespace wf
