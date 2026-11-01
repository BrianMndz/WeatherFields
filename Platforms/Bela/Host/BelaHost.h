#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "wf/ControlInput.h"
#include "wf/Params.h"
#include "wf/Rig.h"
#include "wf/controls/Panel.h"
#include "wf/oled/Screen.h"

namespace wf {
namespace bela {

// The Bela host runs one rig in Bela's render(), as the desktop host runs one
// in JUCE's callback: the browser panel's events first, then the hardware
// panel's, then the published parameter values, then Process(). An app is this
// host plus the three functions below, so the rig is chosen at link time
// (Platforms/Bela/Apps). Besides the browser panel (GuiLink), the host drives
// the OLED (OledLink) and the app's knobs and buttons (PanelLink).

// Provided by the app: the rig the board runs. Called once, in setup().
Rig& AppRig();

// Provided by the app: what the OLED shows for its rig, or null for the test
// pattern. Called once, in setup(); only the OLED's thread draws it.
oled::Screen* AppScreen();

// An app's hardware panel and the pins it is wired to, in the panel's order;
// pots across the 3.3 V rail, buttons high when pressed.
struct PanelWiring {
    controls::Panel* panel;  // null: no hardware
    const int* knobChannels;
    const int* buttonPins;
};

// Provided by the app. Called once, in setup(); only render() runs the panel.
PanelWiring AppPanel();

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

    // What the browser panel presents for selection (wf-focus): a flat
    // index, or -1 for nothing, and when the page last said so
    // (FocusClockMs()). The page repeats it while it lasts, and it lapses
    // kFocusLifetimeMs after the last, so a page that goes away leaves none.
    std::atomic<std::int32_t> focus{-1};
    std::atomic<std::uint32_t> focusMs{0};

    // What the hardware panel presents (PackFocus()), or -1 for nothing. It
    // wins over the page's, and needs no lifetime: the panel always knows.
    std::atomic<std::int32_t> panelFocus{-1};

    // The hardware panel's first readings are in `values`, so the OLED's
    // first frame learns them instead of opening a popup for them.
    std::atomic<bool> synced{false};
};

constexpr std::uint32_t kFocusLifetimeMs = 2500;  // the page repeats every 1000

constexpr std::int32_t PackFocus(std::size_t slot, ParamId id) {
    return static_cast<std::int32_t>(slot << 16 | id);
}

inline std::uint32_t FocusClockMs() {
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace bela
}  // namespace wf
