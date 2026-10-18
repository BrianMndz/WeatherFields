// The Clouds app for Bela: the Bela host running Clouds on its own, with
// CloudsEngine's front panel in the browser (sketch.js).

#include "BelaHost.h"
#include "wf/clouds/Engine.h"

namespace {

wf::SoloRig<wf::clouds::Engine> rig;

}  // namespace

wf::Rig& wf::bela::AppRig() {
    return rig;
}
