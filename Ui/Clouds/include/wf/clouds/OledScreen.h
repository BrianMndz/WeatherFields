#pragma once

#include <cstddef>
#include <cstdint>

#include "wf/oled/Screen.h"

namespace wf {
namespace clouds {

// Clouds on the OLED, from wf::clouds::Engine's published values:
//
//   top     the playback mode, the quality, FRZ (reversed) while frozen
//   middle  the grain cloud, a picture of the grain settings rather than the
//           grains themselves (the engine does not report them), above a
//           dotted floor with a marker at POSITION; framed while frozen
//   right   the IN (after IN GAIN) and OUT meters, -48 to 0 dB
//   bottom  the four BLEND values: MIX (dry/wet), SPR (spread), FBK, REV
//
// The cloud follows the module's grain generator: DENSITY gives no grains
// around noon, more grains away from it, evenly spaced below noon and
// random above; SIZE sets how long each grain lives and how long it is
// drawn; POSITION moves the cloud along the floor, PITCH up and down, and
// TEXTURE spreads it out and softens it. The output level thins it: no
// grains below -48 dB, all of them from -24 dB, so silence shows an empty
// cloud. Freeze stops the picture.
//
// A changed value shows for 1.5 s after its last change, wherever the
// change came from. POSITION, SIZE, PITCH, DENSITY, TEXTURE and IN GAIN,
// which the screen shows no number for, take the header: name and value,
// the rule under it a bar. Quality or mode take the whole screen, every
// option listed and the current one reversed, noting when the change erased
// the recording; they also show for as long as the view presents them
// (RigReadout::ViewFocus(): the panel's quality or mode display after B is
// held), changed or not. A newer change replaces the popup. The BLEND
// values, freeze, TRIG and its gate open none: the screen already shows
// them.
class OledScreen final : public oled::Screen {
public:
    // Parameters watched for changes: every wf::clouds::Engine ID.
    static constexpr int kParamCount = 15;

    // `slot`: the rig slot that holds the Clouds engine.
    explicit OledScreen(std::size_t slot) noexcept : slot_(slot) {}

    void Draw(u8g2_t* u8g2, std::uint32_t ms, const oled::RigReadout& rig) noexcept override;

private:
    std::size_t slot_;
    bool started_ = false;
    std::uint32_t lastMs_ = 0;
    std::uint32_t cloudMs_ = 0;  // the cloud's own time, which freeze stops
    float shown_[kParamCount] = {};  // the values the last frame drew
    int popup_ = -1;                 // the parameter in a popup, or -1
    int changed_ = -1;               // the last changed parameter, until no popup shows
    std::uint32_t changedMs_ = 0;
    bool erased_ = false;  // that change erased the recording
};

}  // namespace clouds
}  // namespace wf
