// The Clouds app for Bela: the Bela host running Clouds on its own, with
// CloudsEngine's front panel in the browser (sketch.js) and Clouds' screen on
// the OLED.

#include "BelaHost.h"
#include "wf/clouds/Engine.h"
#include "wf/clouds/OledScreen.h"

namespace {

wf::SoloRig<wf::clouds::Engine> rig;
wf::clouds::OledScreen screen(0);  // the engine's slot in the solo rig

}  // namespace

wf::Rig& wf::bela::AppRig() {
    return rig;
}

wf::oled::Screen* wf::bela::AppScreen() {
    return &screen;
}
