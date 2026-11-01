#pragma once

#include <Bela.h>

#include "BelaHost.h"

namespace wf {
namespace bela {

// The app's hardware panel on Bela's pins. It knows pins and nothing else:
// once per callback it hands the panel each knob's mean reading and each
// button's sample, in the panel's order, and publishes what the panel
// presents. Inputs nothing is wired to are never read: a floating analog input
// follows its neighbours.
class PanelLink {
public:
    // From setup(), once the rig is prepared. False, with the error printed,
    // if the board cannot provide the app's wiring; the host then refuses to
    // start. A null panel is fine: nothing runs.
    bool Start(const PanelWiring& wiring, const BelaContext* context);

    // From render(), after the browser panel's events and before Publish():
    // the panel's events reach the rig in this callback.
    void Update(BelaContext* context, Rig& rig, Published& published) noexcept;

    // Whether the panel's first readings have reached the rig, or there is no
    // panel to wait for.
    bool Synced() const noexcept { return wiring_.panel == nullptr || warmUpCallbacks_ == 0; }

private:
    static constexpr int kMaxKnobs = 8;     // the analog inputs
    static constexpr int kMaxButtons = 16;  // the digital pins

    PanelWiring wiring_{nullptr, nullptr, nullptr};
    int knobCount_ = 0;
    int buttonCount_ = 0;
    unsigned int warmUpCallbacks_ = 0;
};

}  // namespace bela
}  // namespace wf
