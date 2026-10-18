#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "wf/ControlInput.h"
#include "wf/Params.h"
#include "wf/Rig.h"

namespace wf {
namespace bela {

// The Bela host runs one rig in Bela's render(), as the desktop host runs one
// in JUCE's callback: the browser panel's events first, then the published
// parameter values, then Process(). An app is this host plus the one
// function below, so the rig is chosen at link time (Platforms/Bela/Apps).

// Provided by the app: the rig the board runs. Called once, in setup().
Rig& AppRig();

constexpr std::size_t kGuiInputCapacity = 256;
using GuiInput = ControlInput<kGuiInputCapacity>;

// What render() publishes for the other threads, set up in setup() before
// they start. Every parameter of every slot has a flat index, in slot order
// and then table order.
struct Published {
    struct Param {
        std::uint8_t slot;
        const ParamSpec* spec;  // in the rig's table, static storage
    };

    std::vector<Param> params;
    std::unique_ptr<std::atomic<float>[]> values;  // ParamValue() after each callback's events
    std::atomic<float> inputLevel{0.0f};           // wf::PeakFollower of the rig's input
    std::atomic<float> outputLevel{0.0f};          // and of its output
};

}  // namespace bela
}  // namespace wf
