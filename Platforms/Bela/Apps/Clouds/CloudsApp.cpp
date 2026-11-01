// The Clouds app for Bela: the Bela host running Clouds on its own, with
// CloudsEngine's front panel in the browser (sketch.js), Clouds' screen on
// the OLED and its knobs and buttons on the board's pins.

#include "BelaHost.h"
#include "wf/clouds/Engine.h"
#include "wf/clouds/FrontPanel.h"
#include "wf/clouds/OledScreen.h"

namespace {

using wf::clouds::FrontPanel;

wf::SoloRig<wf::clouds::Engine> rig;
wf::clouds::OledScreen screen(0);  // the engine's slot in the solo rig
FrontPanel panel(0);

// As wired (PROJECT_GUIDE, Bela controls). IN GAIN (A6) and TRIG (D2) are not
// wired yet: the browser panel still has them.
const int kKnobChannels[FrontPanel::kKnobCount] = {0, 1, 2, 3, 4, 5};
const int kButtonPins[FrontPanel::kButtonCount] = {0, 1};

}  // namespace

wf::Rig& wf::bela::AppRig() {
    return rig;
}

wf::oled::Screen* wf::bela::AppScreen() {
    return &screen;
}

wf::bela::PanelWiring wf::bela::AppPanel() {
    return wf::bela::PanelWiring{&panel, kKnobChannels, kButtonPins};
}
