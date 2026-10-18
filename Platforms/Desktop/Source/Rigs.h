#pragma once

#include <cstddef>
#include <memory>

#include "wf/Rig.h"

namespace wf {
namespace desktop {

class AudioHost;
class RigView;

// One entry of the desktop host's rig menu: the rig, and its own view, or
// null for the generic RigPanel. The view is made after the host holds the
// rig.
struct RigEntry {
    const char* name;
    std::unique_ptr<Rig> (*make)();
    std::unique_ptr<RigView> (*makeView)(AudioHost& host);
};

// Every rig the desktop app offers, in menu order; the first is the default.
const RigEntry* Rigs(std::size_t& count);

}  // namespace desktop
}  // namespace wf
