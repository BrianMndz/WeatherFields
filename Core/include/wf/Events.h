#pragma once

#include <cstdint>
#include <type_traits>

#include "wf/Params.h"

namespace wf {

// Every change to the sound, from any source (GUI, MIDI, scenes, later LFOs
// and the Bela-Seed link), as one 8-byte plain struct: it copies into a
// queue slot, a UART frame or a scene file byte for byte.
struct ControlEvent {
    enum class Type : std::uint8_t {
        Param,    // set parameter `id` to `value` (plain units)
        NoteOn,   // note number `id`, velocity `value` in 0..1
        NoteOff,  // note number `id`, release velocity `value` in 0..1
        Trigger,  // fire Trigger parameter `id`; `value` is unused
    };

    Type type;
    std::uint8_t target;  // engine slot within the rig
    ParamId id;
    float value;
};

static_assert(sizeof(ControlEvent) == 8, "ControlEvent must stay 8 bytes");
static_assert(std::is_trivially_copyable<ControlEvent>::value &&
                  std::is_standard_layout<ControlEvent>::value,
              "ControlEvent must stay plain data");

}  // namespace wf
