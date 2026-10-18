#pragma once

#include <cstddef>
#include <cstdint>

#include "u8g2.h"
#include "wf/Params.h"

namespace wf {
namespace oled {

// What a screen reads from its host: the values the host published for the
// rig's parameters, and the levels of the rig's input and output. The host
// implements it over its published values; a screen never touches the rig.
class RigReadout {
public:
    // The last published value of parameter `id` in `slot`, or 0 when the
    // rig has no such parameter.
    virtual float Value(std::size_t slot, ParamId id) const noexcept = 0;

    // wf::PeakFollower levels: linear peaks, 1 at full scale.
    virtual float InputLevel() const noexcept = 0;
    virtual float OutputLevel() const noexcept = 0;

protected:
    ~RigReadout() = default;
};

// What the OLED shows for a rig. A host draws one frame at a time, from one
// thread (Bela's OLED thread, the desktop's message thread), so a screen may
// keep state between frames. `ms` is the host's running time in
// milliseconds: animations follow it, not frames, since the board's frame
// rate depends on its I2C bus.
class Screen {
public:
    virtual ~Screen() = default;

    // Clears the buffer and draws a whole frame.
    virtual void Draw(u8g2_t* u8g2, std::uint32_t ms, const RigReadout& rig) noexcept = 0;
};

}  // namespace oled
}  // namespace wf
