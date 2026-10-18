#include "Rigs.h"

#include "wf/clouds/Engine.h"

namespace wf {
namespace desktop {

namespace {

template <typename R>
std::unique_ptr<Rig> Make() {
    return std::make_unique<R>();
}

const RigEntry kRigs[] = {
    {"Clouds", &Make<SoloRig<clouds::Engine>>},
};

}  // namespace

const RigEntry* Rigs(std::size_t& count) {
    count = sizeof(kRigs) / sizeof(kRigs[0]);
    return kRigs;
}

}  // namespace desktop
}  // namespace wf
