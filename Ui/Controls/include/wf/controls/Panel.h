#pragma once

#include <cstddef>

#include "wf/Params.h"
#include "wf/Rig.h"

namespace wf {
namespace controls {

// A rig's hardware front panel, as Screen is its display: the logic below the
// host, which only reads pins and hands over each callback's readings. It
// runs on the audio thread, the rig's one mutator, so it calls Rig::Handle()
// directly, with no queue.
class Panel {
public:
    // A parameter the panel presents for selection (see
    // oled::RigReadout::Focus), so the screen can show it.
    struct Focus {
        bool active;
        std::size_t slot;
        ParamId id;
    };

    virtual ~Panel() = default;

    virtual std::size_t KnobCount() const noexcept = 0;
    virtual std::size_t ButtonCount() const noexcept = 0;

    // Not real-time, once the rig is prepared. The next Update() starts anew.
    virtual void Prepare(float callbackMs) noexcept = 0;

    // Real-time, once per callback, before Rig::Process(). `knobs`: each
    // knob's mean reading over the callback, in ADC units from 0 to 1 of its
    // 4.096 V. `buttons`: each button's sample, true when pressed.
    virtual void Update(const float* knobs, const bool* buttons, Rig& rig) noexcept = 0;

    // Real-time, like Update().
    virtual Focus CurrentFocus() const noexcept = 0;
};

}  // namespace controls
}  // namespace wf
