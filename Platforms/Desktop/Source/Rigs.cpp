#include "Rigs.h"

#include "AudioHost.h"
#include "Clouds/CloudsPanel.h"
#include "Stratus/StratusPanel.h"
#include "wf/clouds/Engine.h"
#include "wf/clouds/OledScreen.h"

#include "wf/stratus/Engine.h"
#include "wf/stratus/OledScreen.h"

namespace wf {
namespace desktop {

namespace {

template <typename R>
std::unique_ptr<Rig> Make() {
    return std::make_unique<R>();
}

// Clouds' engine sits in slot 0 of its solo rig.
std::unique_ptr<RigView> MakeCloudsView(AudioHost& host) {
    return std::make_unique<CloudsPanel>(host, 0);
}

// Likewise Stratus' in its solo rig.
std::unique_ptr<RigView> MakeStratusView(AudioHost& host) {
    return std::make_unique<StratusPanel>(host, 0);
}

std::unique_ptr<oled::Screen> MakeCloudsScreen() {
    return std::make_unique<clouds::OledScreen>(0);
}

std::unique_ptr<oled::Screen> MakeStratusScreen() {
    return std::make_unique<stratus::OledScreen>(0);
}

const RigEntry kRigs[] = {
    {"Clouds", &Make<SoloRig<clouds::Engine>>, &MakeCloudsView, &MakeCloudsScreen},
    {"Stratus", &Make<SoloRig<stratus::Engine>>, &MakeStratusView, &MakeStratusScreen},
};

}  // namespace

const RigEntry* Rigs(std::size_t& count) {
    count = sizeof(kRigs) / sizeof(kRigs[0]);
    return kRigs;
}

}  // namespace desktop
}  // namespace wf
