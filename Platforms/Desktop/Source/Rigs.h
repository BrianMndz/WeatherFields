#pragma once

#include <cstddef>
#include <memory>

#include "wf/Rig.h"
#include "wf/oled/Screen.h"

namespace wf {
namespace desktop {

class AudioHost;
class RigView;

// One entry of the desktop host's rig menu: the rig, its own view, or null
// for the generic RigPanel, and its OLED screen, or null for the test
// pattern. The view is made after the host holds the rig.
struct RigEntry {
    const char* name;
    std::unique_ptr<Rig> (*make)();
    std::unique_ptr<RigView> (*makeView)(AudioHost& host);
    std::unique_ptr<oled::Screen> (*makeScreen)();
};

// Every rig the desktop app offers, in menu order; the first is the default.
const RigEntry* Rigs(std::size_t& count);

}  // namespace desktop
}  // namespace wf
