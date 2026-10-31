#pragma once

#include <cstddef>
#include <cstdint>

#include "wf/oled/Screen.h"

namespace wf {
namespace stratus {

// Stratus on the OLED, from wf::stratus::Engine's published values
// (proposal D of the 2026-10-31 mockups, the desktop panel's LED row as
// cells):
//
//   top     the state (EMPTY, RECORDING, PLAYING, OVERDUB, STOPPED),
//           reversed while recording or overdubbing; on the right the time
//           left during the first take, else position / length
//   middle  sixteen cells: during the first take they fill over the
//           maximum length, so the empty ones are the time left; with a
//           loop, the cell under the playhead stands tall while it plays,
//           and the cells it has passed this lap are stippled while
//           overdubbing; dots when empty. Under them, 0 and the take's
//           limit or the loop's length
//   right   the IN and OUT meters, -48 to 0 dB
//   bottom  LVL, the Loop Level bar, and THRU, reversed while on
//
// Everything shows all the time, so no popups. The state follows from the
// values: Length 0 with Record on is the first take.
class OledScreen final : public oled::Screen {
public:
    // `slot`: the rig slot that holds the Stratus engine.
    explicit OledScreen(std::size_t slot) noexcept : slot_(slot) {}

    void Draw(u8g2_t* u8g2, std::uint32_t ms, const oled::RigReadout& rig) noexcept override;

private:
    std::size_t slot_;
};

}  // namespace stratus
}  // namespace wf
