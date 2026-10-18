#pragma once

#include <cstddef>
#include <memory>

#include "wf/Rig.h"

namespace wf {
namespace desktop {

// One entry of the desktop host's rig menu.
struct RigEntry {
    const char* name;
    std::unique_ptr<Rig> (*make)();
};

// Every rig the desktop app offers, in menu order; the first is the default.
const RigEntry* Rigs(std::size_t& count);

}  // namespace desktop
}  // namespace wf
