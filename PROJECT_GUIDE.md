# WeatherFields Project Guide

Last updated: 2026-10-18.

Read this at the start of a session. It holds the current status, next
actions, contracts and decisions. Edit it in place after each step; never
append a log. Long detail goes to an archive under `docs/`.

- [`docs/ModularMusicSystem.md`](docs/ModularMusicSystem.md): the vision.
- [`docs/HANDOVER-ModularSystem.md`](docs/HANDOVER-ModularSystem.md): the
  decisions up to 2026-10-17, how the user works, platform lessons (kept up to
  date) and the milestones. Where it disagrees with this guide (its section 4
  layout, its `ThirdParty/` and `<name>`), this guide wins.

## Purpose

A modular music system of complementary engines (granular, looper, synth
voices, Mutable Instruments ports) for lo-fi beats, granular sound design,
dark ambient and synth music. Every engine is a self-contained, platform-free
C++14 library. The same code runs on desktop (JUCE) first, then on Bela and
Daisy Seed3, alone or combined into one system.

## Working style

- Small steps. After each one the user tests it (usually by listening), often
  asks for a review, then commits it. **Don't commit unless asked.**
- Implement directly; explain design reasons, not basics (the user is a C/C++
  engineer with DSP experience).
- Eurorack ports match the firmware closely; state every deliberate deviation.
- Check a pasted review finding against the code before fixing it; reviewer
  harnesses under `/private/tmp/weatherfields-*-review.*` can be rerun.
- The Bela board is shared with other work: when the user says it is busy,
  nothing runs on it (no `deploy.sh`, no SSH) until they say it is free.
- Report measurements honestly: observed maxima, callback-cost overruns,
  device underruns and WCET (never claimed) are different things.

## Status

| Area | Status |
|---|---|
| MutableDSP | `Modules/MutableDSP`, `main` at `24b201f`: Clouds closure imported unchanged from Eurorack `f4cfa76` (stmlib `e3bd7c9`), CloudsDSP patches applied, `MutableDSP::stmlib` and `MutableDSP::clouds` targets. Object code byte-identical to CloudsDSP's. |
| JUCE | `Modules/JUCE` pinned at 9.0.4; configures from `Platforms/Desktop`. |
| u8g2 | `Modules/u8g2`: a curated copy (not a submodule) of upstream u8g2 `2.37.1` (`414efcd`), 23 C files and 2 headers, unchanged, with `UPSTREAM.md` (policy, SHA-256 per file). Target `u8g2::u8g2` (C, gnu11 as on Bela). The exact link closure of the u8g2 core plus the SH1106/SSD1306 128x64 driver: no other drivers, no font data, no `u8g2_d_setup.c`/`u8g2_d_memory.c`, no MUI or Arduino wrapper. Replaces the copy from Bela's tutorial (O2O), which was older. |
| Build skeleton | Top-level CMake, presets `dev` and `desktop`, `cmake/WeatherFields.cmake`, CTest. |
| Core | Header-only `wf::core`, all compiling for the board with Clang 3.9: `wf/Dsp.h` (`IsFinite`, checked as built and under `-O3 -ffast-math`; `ApplyGainRamp`, JUCE's `applyGainRamp` semantics; `PeakFollower`, the Clouds module's meter, 10 ms attack and 250 ms release, NaN-safe); `wf/Params.h` (`ParamId`, `ParamKind`, `ParamSpec` with static choice labels, `ParamTable`, `ConstrainValue`, which rejects non-finite values, and `IsValidParamTable`, which also requires a label per Choice index); `wf/Events.h` (8-byte POD `ControlEvent`); `wf/SpscQueue.h` (lock-free, power-of-two capacity, trivially copyable items, `PopAvailable()` for the consumer; its own header because the recorder will use it too). `wf/ControlOutbox.h` (`ControlOutbox<N>`: a producer that must not lose events, the GUI's on both hosts; events wait in order when the input is full, only Continuous values coalesce; checked with both review cases and a mutation that restores the old bug). `wf/ControlInput.h` (`ControlInput<N>`: one producer's queue; `Drain(rig, max)` takes at most `max` and never more than was queued when it began, so a refilling producer cannot stretch the callback; overflow and dropped counters; `Discard()` for rig swaps). The queue's two-thread check also runs clean under ThreadSanitizer, which flags a copy with relaxed ordering. `wf/Engine.h` (`wf::Engine`: `Name`, `Params`, `MemoryBytes`, `Prepare`, `Handle`, `ParamValue` (audio-thread readback, including changes the engine makes itself), `Process`; silent and deaf until a successful `Prepare()`; input buffers never null; `MemoryBlock`, `PrepareStatus`, `CheckMemory`, 16-byte `kMemoryAlignment`); `wf/Rig.h` (`ConstrainEvent`, `wf::Rig` with a const `Slot()` and a private `MutableSlot()` that only its non-virtual `Handle()` uses, so every change is validated; the rig-swap procedure; `SoloRig<E>`), checked with a gain engine (readback, lifecycle) and compiled for the board with `-fno-exceptions -fno-rtti`. |
| Engines/Clouds | `wf::clouds`: CloudsEngine's `Engine/` from `e7738d9`, now in namespace `wf::clouds` with its `Engine` class renamed `Module` (`Module.h`, `Module.cpp`; macro `WF_CLOUDS_NODISCARD`; MI code written `::clouds::`). Otherwise unchanged: the 16 render hashes still match and `check.sh` passes. Its five checks pass in Debug and Release; their only other change is `std::byte` → `unsigned char` for the test arenas, as checks compile as C++14. `wf::clouds::Engine` implements `wf::Engine` around it: 15 parameters (IDs 0-8 the knobs in module order, 9 IN GAIN, 10 Freeze, 11 Trigger, 12 Gate, 13 Quality, 14 Mode) with JUCE's ranges and defaults, applied from the table in `Prepare()`; labels for quality ("Stereo 16-bit" ...) and mode ("Granular", "Stretch", "Looping Delay", "Spectral"); `ParamValue()` reads quality and mode from the Module; one 184,192-byte arena for both Module arenas; 32/44.1/48 kHz through `SampleRateAdapter` with `ServicePolicy::AfterEachBlock`; quality and mode changes in event order with `Service()` at once, releasing freeze when they erase; a second `Prepare()` rebuilds the Module in place. `wf_clouds_engine_check`: all 16 quality × mode renders through the rig are byte-identical to the Module's own, 32 frames later (the seven that use randomness fail with another seed), plus defaults, clamping at the rig, the freeze rules, readback (a quality change reads back with freeze released) and IN GAIN at 48 kHz. |
| Tools/Render | `wf_render_clouds`: CloudsEngine's CLI (`e7738d9`), only its include and name changed. CTest `wf_render_clouds_references` renders all 16 quality × mode combinations and compares them with `clouds_references.sha256`, taken from CloudsEngine's `clouds_cli` built from `e7738d9`: byte-identical in Debug and Release, and equal to CloudsEngine's recorded table. The references hold for macOS arm64 with Apple Clang. |
| Desktop host | `wf_desktop` (`WeatherFields.app`, `desktop` preset): rig menu (Clouds' `SoloRig`), audio settings saved between runs, status line (rate, block, CPU, a rig's failed `Prepare()`, GUI overflow and dropped counts), and the rig's view (`RigView`): its own if `RigEntry::makeView` gives one, else `RigPanel` generated from `ParamSpec` (knobs, label menus, latching toggles, trigger buttons). Clouds' view is `Clouds/CloudsPanel`, CloudsEngine's front panel (`e7738d9`) ported to the host: same drawing, B/C/TRIG behaviour, LED modes and mode tooltips, but it sends events and shows published values, so engine-made changes appear; its meter is the host's input level times IN GAIN, or the output level while frozen. Tested 2026-10-18: behaves as in CloudsEngine. `AudioHost` runs the rig in JUCE's callback: GUI `ControlInput` drained (at most 32 per callback), then `ParamValue()` published to atomics, then `Process()` from scratch buffers, with `PeakFollower`s on the rig's input and output in pieces of at most the prepared block; silent input when there is none, mono input to both sides. The rig is prepared in `audioDeviceAboutToStart()`; a device restart replays the published values as events, Choices first. While a `Prepare()` has failed, the callback is silent and neither drains nor publishes, so queued events wait and the last valid values survive. Rig swaps detach the callback, discard queued events, destroy the old rig before making the new one. When the GUI's input is full, its events wait in order; only a Continuous parameter's newer value replaces its waiting one (moving to the back), while Choice, Toggle and Trigger events are all kept, since they can act on transitions; the panel shows only published values. Builds clean with `-Wall -Wextra`. Generic panel listened 2026-10-18: works. "OLED..." opens the emulated screen (`OledView`, in its own window): a panel-less `wf::oled::Display` drawing the current rig's screen (`RigEntry::makeScreen`; Clouds' `OledScreen`, else the test pattern) from the host's published values and levels at the board's 25 frames a second, shown at the largest integer scale with nearest-neighbour scaling; lit pixels blue as on the board's panel, a click cycles blue, white, yellow. Test pattern looked at 2026-10-18: opens and animates. Clouds' screen looked at 2026-10-18: works, follows the panel. |
| Bela host | `Platforms/Bela/Host`: `BelaHost.cpp` (Bela's `setup()`/`render()`/`cleanup()` around the app's `wf::bela::AppRig()` and `AppScreen()` (its OLED screen, or null for the test pattern), chosen at link time; 32-frame default period; rig memory allocated in `setup()`; failure refuses to start; `render()` drains the panel's `ControlInput` (at most 32 events), publishes `ParamValue()`s, processes Bela's buffers directly, meters input and output) and `GuiLink` (the browser panel over Bela's Gui library, generic by slot and ID: `wf-hello`/`wf-state`, `wf-param`, `wf-trigger`, and `wf-update` about 30 times a second with levels and changed values; the WebSocket thread and the sender thread share one `ControlOutbox` under a mutex; only the sender thread sends; it is never destroyed, and `Stop()` closes it under the mutex, since Bela's Gui cannot stop its WebSocket server synchronously). App `bela_clouds` (`Apps/Clouds`: `CloudsApp.cpp`, and `sketch.js`, CloudsEngine's panel ported onto `wf-*` messages, with the playback mode display added; it keeps the board's latest report per control and shows it once the control is not held (500 ms after a local edit, or while dragging), and re-sends TRIG's gate on every connection). Built and run on the board 2026-10-18: starts at 44.1 kHz, 32 frames, audio thread MSW 1; the protocol checked end to end over a tunnel (state, updates, clamping at the rig, freeze released by a quality change, invalid messages ignored). Not yet listened to through the browser panel. The Bela review fixes (below) are checked locally only and not yet deployed. The OLED (`OledLink`, `I2cPanel`): every app drives it from `setup()` to `cleanup()` on an ordinary Linux thread (never `render()`), drawing the app's screen from the published values and levels (`bela_clouds`: Clouds' `OledScreen`) and sending it whole at up to `WF_OLED_FPS` (default 25) on I2C bus `WF_OLED_I2C_BUS` (1) at `WF_OLED_I2C_ADDRESS` (0x3C), all overridable with deploy's `-D`; it prints fps and draw/send times (mean, max) every 5 s. `I2cPanel` is our u8x8 glue: one `write()` per u8x8 transfer, state per display through u8x8's user pointer (defined on Linux), errors kept for the frame to check. A bus that cannot be opened, or a panel that does not answer, is printed once and never stops audio; the thread retries once a second and re-initialises the panel; `cleanup()` blanks it and joins. Run on the board 2026-10-18 with Clouds: the panel is at 0x3C on bus 1 (`i2cdetect`), the pattern shows as on the desktop; at a 30 fps cap it sent back to back at 27.2-28.2 fps, send 35.1-36.8 ms mean (40-51 ms max), draw 0.34-0.41 ms mean (up to 2.1 ms). About 1.16 KB a frame (1 KB of data in 24-byte transfers, plus addresses and commands), so the bus runs at 400 kHz with about 9 ms of per-transfer overhead. At the 25 fps default: a steady 24.9-25.0 fps, send 35.6-35.8 ms mean (up to 45 ms after start-up), draw about 0.33 ms mean. The outline reaches all four edges of the panel (SH1106 driver and column offset right), and Clouds sounds the same with the OLED running; the CPU cost with and without it is not yet measured. Clouds' screen run on the board 2026-10-18: follows the browser panel as the desktop one follows its panel. With nothing playing the cloud showed grains, which read as activity; it now thins with the output level (not yet looked at). |
| Ui/Oled | `wf::oled` (C++14): `Display`, a 128x64 SH1106 in u8g2 full-buffer mode with its own 1 KB buffer; the default constructor has no panel (drawing only), the other takes the byte and GPIO callbacks; `Buffer()` and `Pixel(x, y)` read the panel layout. `Fonts.h` names the compiled fonts (`kFontSmall` 5x7, `kFontText` 6x10, upstream X11 misc-fixed, public domain) and `src/Fonts.c` holds their data, copied unchanged; any other upstream font fails to link. `Screen.h`: `Screen` (`Draw(u8g2, ms, readout)`, one frame from one thread, so it may keep state) and `RigReadout` (published value by slot and ID, input and output levels), which each host implements over its published values. `DrawTestPattern(u8g2, ms)`: outline, title, running time, a 1-pixel checkerboard, a scrolling sine, a bouncing ball, timed by milliseconds rather than frames; `TestPatternScreen` shows it for rigs without a screen. `wf_oled_display_check`: buffer layout, fonts, the pattern's determinism and motion, and that `u8g2_SendBuffer()` reaches the byte callback with the whole buffer. Manifest `oled` passes `check.sh`. |
| Ui/Clouds | `wf::clouds_ui` (C++14, links `wf::oled` and `wf::clouds`): `OledScreen(slot)`, the main screen chosen 2026-10-18 from mockups. Top: mode (GRANULAR, STRETCH, LOOP DELAY, SPECTRAL), quality (16b ST ... 8b MO), FRZ reversed while frozen. Middle: the grain cloud, a picture of the grain settings, not the grains (the engine reports none): DENSITY as the module's overlap (none from 0.47 to 0.53, evenly placed grains swelling in turn below noon, random ones above), SIZE the grains' life and length, POSITION the cloud's place along a dotted floor with a marker, PITCH its height, TEXTURE its spread, stippled above 0.75 (where the module diffuses); the output level thins it (no grains below -48 dB, all from -24 dB, each grain keeping a fixed draw so the same ones stay), so silence shows an empty cloud; freeze stops its clock and frames it. Right: IN (after IN GAIN) and OUT meters, -48 to 0 dB in 11 segments. Bottom: MIX, SPR, FBK, REV bars. Only `kFontSmall`. `wf_clouds_oled_screen_check`: determinism, the FRZ tag and frame, a still frozen cloud that moves again on release, no grains at noon, position and pitch moving the cloud, no grains in silence and fewer below -24 dB, bar fills, meters and IN GAIN. Manifest `clouds_ui` passes `check.sh`. |
| Rigs | Only `SoloRig<E>`. |
| Manifests | `wf_add_manifest(<name> TARGET <t> [SOURCES] [INCLUDES] [RESOURCES])` writes `build/<preset>/manifests/<name>.txt` (sources, include dirs, defines of the target's link closure, plus files only the external build compiles and resources it ships). `TARGET` takes several targets (their link closures are merged). Four: `clouds` (`wf::clouds`, 14 translation units), `bela_clouds` (the Bela host, `WF_BELA_HOST_TARGETS` (`wf::oled`) and the Clouds app with `wf::clouds_ui`, 46 units, `sketch.js`), `oled` (`wf::oled` and u8g2, 26 units) and `clouds_ui` (41 units). |
| Tools/Bela | `check.sh` reads the manifests (configuring the `dev` preset first) and passes on every manifest with Clang 3.9 and the board's headers; `--self-test` passes. `deploy.sh [app]` sets the board clock, stages an app from its manifest (units at the top named after their repo path, `.cc` wrappers, headers by include path, resources) and builds with Bela's Makefile; it keeps the last build's CPPFLAGS on the board (`build/wf-cppflags.txt`) and rebuilds everything when they differ, since make does not track flags (a new `-D` was silently ignored by unchanged files). Toolchain cache shared by all projects at `~/.cache/bela-toolchain` (CloudsEngine reaches it through a symlink at its old path). `profile.sh` is still CloudsEngine's and stops with a message until the profiling step. |

## Next actions

Bela hardware integration comes before MIDI (decided 2026-10-18).

The OLED runs on Bela at 25 fps and shows Clouds' main screen, the same on
both hosts.

1. **Clouds' OLED screen, next steps**, each its own step: popups on change
   (quality and mode as a list, other parameters as name, value and bar,
   about 1.5 s), then burn-in care (dim after a minute without changes, a
   1-pixel shift every few minutes).
2. **Clouds on Bela: listen.** The Bela review fixes are now on the board
   (deployed with the OLED). `Tools/Bela/deploy.sh --run`, open
   `http://bela.local/gui`, PO-33 into Bela's input. Check the panel as on the
   desktop: knobs, B (1 s quality, 4 s mode), freeze following a quality
   change, TRIG and its gate, the LED meter. Stretch, looping delay and
   spectral now run on Bela with `ServicePolicy::AfterEachBlock`, unprofiled:
   underruns there are expected data for the next step, not a bug yet.
3. **Bela profiling**: `profile.sh` and a `Profiler.h` that drives a rig
   (fixed input, presets through events), `CLOUDS_PROFILE` becomes
   `WF_PROFILE`; profile every mode and quality at 32 frames, with the OLED
   running and without it (`-DWF_OLED_I2C_BUS=9`, a missing bus), to measure
   what its thread and I2C traffic cost the board. Decide the
   Clouds service policy from it: CloudsEngine's Bela build used
   `ServicePolicy::Manual` in granular only; the wrapper uses
   `AfterEachBlock` (needed by stretch and spectral).
4. **MIDI**: log the ROLI messages, write a hard-coded `MidiMap`, play Clouds
   from the blocks, on desktop, then USB MIDI on Bela. Milestone 1.

## Architecture

```text
WeatherFields/
├── CMakeLists.txt      # CMake ≥ 3.25; WF_BUILD_TESTS, WF_BUILD_DESKTOP; no global C++ standard
├── CMakePresets.json   # dev (no JUCE), desktop; later daisy
├── cmake/              # wf_add_rt_library, wf_add_check, wf_add_manifest; later Daisy toolchain
├── Modules/            # third party: JUCE, MutableDSP (submodules), u8g2 (curated copy); later libDaisy
├── Core/               # wf::core
├── Engines/<Name>/     # wf::<name>: CMakeLists, include/wf/<name>/, src/, tests/
├── Ui/Oled/            # wf::oled: the OLED through u8g2, fonts, the Screen interface
├── Ui/<Name>/          # wf::<name>_ui: a rig's or engine's screens, shared by every host
├── Rigs/               # SoloRig<E>, BelaRig, SeedRig, the whole system
├── Platforms/          # Desktop (JUCE), Bela (Host/, Apps/<Name>/), Daisy: hosts + their apps
├── Tools/              # Render (offline CLI), Bela (check/deploy/profile)
├── Tests/              # cross-component checks only
└── docs/
```

Contracts:

1. **Dependencies point one way**: Platforms → Ui → Rigs → Engines → Core →
   Modules. Nothing below Platforms includes JUCE, Bela, libDaisy or OS
   headers, or does file or network I/O.
2. **C++14 below Platforms**, compiled with `-pedantic-errors` on every host
   (`wf_add_rt_library`), so Bela's Clang 3.9 builds it. Public headers also
   compile as C++17. No exceptions or RTTI in real-time code: `WF_STRICT_RT`
   (on in `dev`) builds the libraries with `-fno-exceptions -fno-rtti`.
3. **Real-time rules in every `Process()`**: no allocation after `Prepare()`,
   no locks, no I/O or logging, no `std::function`, no virtual calls per
   sample (per block is fine).
4. **Any engine on any platform.** An engine depends only on Core (and its
   MutableDSP module). A rig composes engines; `SoloRig<E>` runs one engine
   without writing a rig. A host is a library that runs a `wf::Rig&`. An app
   is a host plus a rig, declared in CMake, with the rig chosen at link time
   on embedded targets and from a menu on desktop.
5. **Memory is a host service.** Engines report what they need; `Prepare()`
   receives it (SDRAM on Daisy, static or `setup()` on Bela, heap on desktop).
6. **The desktop host simulates the whole system** in one JUCE audio callback
   (not `AudioProcessorGraph`), mirroring Bela's `render()`; the Seed rig's
   output feeds the Bela rig's input as the cable would.
7. **One mutator.** Producers (MIDI, GUI, later scenes) each own an SPSC
   queue of POD `ControlEvent`s; the audio thread drains them, clamps at the
   rig using `ParamSpec`, then calls `Handle()` and `Process()`.
8. **Parameter IDs are stable forever**: retire an ID, never renumber it. Setting a Continuous parameter has no side effects, so hosts may drop intermediate values; Choice, Toggle and Trigger changes are delivered every one, in order (`wf/Params.h`).
9. **Generalise on the second use** (for example, Clouds' block and rate
   adapters move to Core when the second MI engine needs them).
10. **Explicit source lists** in CMake, no globs: the Bela and Daisy builds
    take their file lists from the same targets.

## Conventions

- **Names:** C++ namespace `wf`; CMake targets `wf_<name>` with alias
  `wf::<name>`; options and macros `WF_` (e.g. `WF_PROFILE`).
- **Headers:** a component's public headers live in `include/wf/<name>/` and
  are included as `"wf/<name>/File.h"` (Core: `"wf/File.h"`). Engines share
  file names such as `Engine.h`; the prefix keeps them apart on one include
  path, including the flat staging folder of the Bela build.
- **Engine classes:** an engine's `wf::Engine` implementation is always
  `wf::<name>::Engine`. A Mutable Instruments port also has a
  `wf::<name>::Module`: the module itself, at its native rate and block size
  with its firmware's controls; the `Engine` adapts it.
- **C++14 static members:** a `static constexpr` array member, such as a
  ParamSpec table, also needs its definition outside the class in C++14
  (implicit from C++17).
- **MI namespaces:** inside `namespace wf { namespace clouds {`, an unqualified
  `clouds::` finds `wf::clouds`. Wrappers write `::clouds::`.
- **Modules:** a submodule, or, when only a small part of a large upstream
  is needed, a curated copy whose `UPSTREAM.md` records the tag, the commit,
  the policy and each file's SHA-256 (u8g2). Either way, files stay unchanged
  unless a patch is logged.
- **MutableDSP:** one curated fork for all MI ports, one shared `stmlib/`,
  upstream layout. A new module is imported unchanged in one commit, patched in
  later commits, each change logged in `PATCHES.md`; all modules come from the
  same upstream commits (`UPSTREAM.md`). Only `main`; WeatherFields pins a
  commit. Commit inside the submodule on `main` (not detached HEAD), push, then
  commit the new pointer in WeatherFields.
- **Vendored headers are system headers:** `Modules/MutableDSP` is added with
  `SYSTEM`, so `-Wall -Wextra` reports only our code.
- **`TEST=1`** is a public definition of `MutableDSP::stmlib`; code built
  outside CMake (the Bela board) must define it too.
- **`stmlib::Random` is process-global**: renders are reproducible per rig,
  not per engine. Seed it explicitly (`0x23` for the Clouds references).
- **Branches:** `main` and `devel`.

## Build

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --preset desktop && cmake --build --preset desktop     # adds JUCE
```

The user's IDE is CLion: it lists `dev` and `desktop` from `CMakePresets.json`
as CMake profiles, building into the same `build/<preset>` trees, so the
terminal and the IDE share one build. Keep the presets the single source of
build settings.

Bela compile check (needs `Tools/Bela/fetch-toolchain.sh` once, and the
board's headers from `pull-board-headers.sh`):

```sh
Tools/Bela/check.sh                   # every manifest; configures the dev preset first
Tools/Bela/check.sh --self-test
```

Build trees go to `build/<preset>`. `ctest` includes the Clouds render
references; a change that alters Clouds' output must fail it, or come with new
references and a reason. Platform notes and the Bela board's
quirks are in the handover, section 8.

## Decisions

Earlier decisions are in the handover, section 2.

| Date | Decision |
|---|---|
| 2026-10-17 | The name is WeatherFields; the namespace is `wf`. |
| 2026-10-17 | Submodules live in `Modules/` (not `ThirdParty/`). |
| 2026-10-17 | MutableDSP is a fresh repo holding every MI port with one shared stmlib; it supersedes CloudsDSP here. CloudsDSP stays for CloudsEngine. Base moved to Eurorack `f4cfa76`, identical to `08460a6` for every Clouds file and adding Beads. |
| 2026-10-17 | Any engine runs on any platform: solo rigs, hosts as libraries, apps = host + rig. Engines are self-contained folders with their own checks. |
| 2026-10-17 | The repo is a plain C++ project; JUCE is one optional target, configured only by the desktop host. |
| 2026-10-17 | JUCE pinned to release 9.0.4. |
| 2026-10-17 | `IsFinite` puts an empty `asm` barrier between the bits and the test: current Clang folds the plain exponent-bit test to true under `-ffast-math`. |
| 2026-10-18 | One memory arena per engine: `MemoryBytes()` then `Prepare()` with one aligned block; a list of regions (for example, SRAM for Clouds' small buffer on Daisy) only if profiling needs it. |
| 2026-10-18 | Naming: `<name>::Engine` implements `wf::Engine`; MI ports keep the module-faithful class as `<name>::Module`. |
| 2026-10-18 | Core review accepted: Core keeps its shape and grows by milestone. Readback, choice labels, bounded event handling, lifecycle wording and a const `Rig::Slot()` come before the desktop host; the later contracts are listed under open questions. |
| 2026-10-18 | Readback is `Engine::ParamValue()`, audio thread only; hosts publish it to atomics. Choice labels live in `ParamSpec::labels` (null for other kinds). Event bounds are per producer (`ControlInput::Drain(rig, max)`), so a flooding MIDI input cannot starve the GUI. `Rig::Slot()` is const; only `Rig::Handle()` reaches a mutable engine. |
| 2026-10-18 | Desktop host: one app target (`wf_desktop`) holding the host and the rig list; the host becomes a library when a second desktop app needs it. Rigs are prepared in `audioDeviceAboutToStart()` (never concurrent with the callback); a device restart replays the published values, so changing the buffer size keeps the sound. The desktop target uses the repo's `-Wall -Wextra`, not `juce_recommended_warning_flags` (its `-Wfloat-equal` hits Core's deliberate exact comparisons). |
| 2026-10-18 | Rigs may bring their own desktop view (`RigEntry::makeView`); the generic `RigPanel` stays the fallback. Views reach the rig only through `AudioHost` (events out, published values and levels in). Meters are host-side: `PeakFollower`s on the rig's input and output, combined by the view (Clouds: input × IN GAIN, or output while frozen; exact because the follower scales with its input), so engines need no meter output yet. `PeakFollower` moved to Core now, as the Bela host's meter is its next use. |
| 2026-10-18 | Desktop host review (all four findings confirmed and fixed): the audio settings dialog is deleted before the device manager; restarts restore Choices before other parameters (modes can reset state); GUI overflow keeps every Choice, Toggle and Trigger event in order and coalesces only Continuous values (second review: coalescing a Choice dropped a side effect); nothing is drained or published while `Prepare()` has failed. Checked with the reviewer's harness (fake device driving the real host). |
| 2026-10-18 | Bela host: generic like the desktop one (it knows only `wf::Rig`); an app is the host plus `AppRig()` and its manifest, which lists the Bela-only sources and resources (`wf_add_manifest` SOURCES/INCLUDES/RESOURCES), so no fake CMake target is needed. The browser protocol is generic by slot and parameter ID; each app's `sketch.js` is the rig's view, as `CloudsPanel` is on desktop. The GUI overflow policy moved to Core as `ControlOutbox` (its second use). The blend selection now lives in the page (CloudsEngine's board kept it), so it resets on reload, as on desktop. |
| 2026-10-18 | Bela host review (three findings confirmed and fixed): `GuiLink` lives until the process exits and closes under its mutex in `Stop()`, as Bela's WebSocket server can still deliver a message during teardown; the page reconciles to the board's latest report after its hold (the board sends only changes, so a suppressed correction would never return) and treats a reconnect snapshot the same way; on every connection the page re-sends TRIG's gate as its button stands. Checked with an adapted teardown harness under ASan/UBSan and the real `sketch.js` in Node; not run on the board. |
| 2026-10-18 | Bela hardware integration before MIDI, starting with a 1.3" I2C OLED (SH1106, 128x64). u8g2 is a curated copy of upstream at a tag, not O2O (an app carrying an older u8g2) and not an upstream submodule (over 400 MB for 25 files). Only the link closure is imported; display setup and buffer come from `wf::oled`, so `u8g2_d_setup.c` and `u8g2_d_memory.c` (every display's setups and static buffers) stay out. Fonts are ours: only those `Fonts.h` names are compiled. The I2C glue is rewritten in `Platforms/Bela`. |
| 2026-10-18 | The OLED belongs to the Bela host, not an app: every app gets it, on an ordinary Linux thread, and it is optional at run time (no panel, no error beyond a message). Its I2C glue is ours (`I2cPanel`): per-display state, errors reported to the frame, no globals. Bus, address and frame rate are compile-time defaults overridable with `-D` until a second board setup needs run-time options. Manifests merge several targets, so a host adds its own libraries (`WF_BELA_HOST_TARGETS`) without a fake CMake target. |
| 2026-10-18 | The desktop emulates the OLED with u8g2 itself, not by redrawing in JUCE: a panel-less `Display` draws into the same buffer, JUCE only scales it up, so desktop and board show the same pixels with the same fonts and code. Screens draw from time in milliseconds, not frames, as the board's frame rate depends on the I2C bus. Screen code lives in `Ui/`, below Platforms and shared by every host. |
| 2026-10-18 | OLED screens: `wf::oled::Screen` draws a frame from a `RigReadout` (published values by slot and ID, levels) and the time, so screens never touch the rig and run unchanged on the desktop's message thread and Bela's OLED thread. A rig supplies its screen as it supplies its view: `RigEntry::makeScreen` on desktop, `AppScreen()` on Bela; none gives the test pattern. Clouds' screen is in `Ui/Clouds` (`wf::clouds_ui`, not in the engine, which stays free of drawing). Clouds' main screen: header, cloud, meters, four BLEND bars (chosen from mockups over two sketches: those need more lines or columns than the panel has, and "BLEND: FBK" needs the blend selection, which lives in the browser page and never reaches the host). The cloud pictures the settings, not the grains; showing real grains needs engine telemetry, deferred. Overlays appear on change rather than as menus until the board has controls of its own. |

## Open questions

- Core contracts deferred to the milestone that needs them (2026-10-18 Core
  review); design each before building that milestone:
  - Looper + Clouds (2): the first multi-engine rig must check arena
    partitioning, routing buffers and in-place processing.
  - Tone (3): channels; pitch bend, pressure and MPE later.
  - Transport and PO-33 sync (4): events have no sample offset; musical
    timing needs a sample-based transport and scheduling.
  - Mixer (5): engines report no latency, and Clouds' adapters add 32+
    frames; parallel dry/processed paths need latency reporting or
    compensation.
  - LFOs (6): keep base parameter values separate from modulation and the
    effective value, so modulation never overwrites what scenes save.
  - Scenes (7): a batch must apply in one callback (atomic publication and
    capacity), which one mutator alone does not guarantee.
  - Scene files and the Bela-Seed protocol: `ControlEvent`'s layout is not a
    file or wire format; add explicit encoding, versioning and rig/engine
    identity.
  - More MI ports in one rig: `stmlib::Random` is process-global, so MI
    engines are not independent instances.

- Seed3 hardware (chip, RAM, codec, USB host or device) and its link to Bela
  (USB MIDI, TRS MIDI, UART).
- Input routing on Bela for the PO-33 plus Seed3 (mixer or analog inputs);
  decide before milestone 3.
- Whether Clouds' engine stays copied here or CloudsEngine later consumes
  this repo.
- Beads vs Clouds on Bela: compare cost and sound once both run on desktop.
- OLED speed, if a screen needs it: a cad of our own in `wf::oled` sending
  each 128-byte page in one transfer instead of u8g2's 24-byte pieces (an
  Arduino Wire limit, not Linux's) should save most of the 9 ms per-frame
  overhead (about 36 fps instead of 28).
- Clouds' OLED screen, later: a picture per mode (stretch, looping delay and
  spectral read the knobs differently; the cloud is the granular picture in
  every mode), and real grain data from the engine (a per-block snapshot of
  the grains and the buffer's loudness, through a MutableDSP patch).
- Clouds deferred work: band-limited resampling, make-up gain (fully dry is
  about −9 dB), stretch and spectral cost on Bela, the sanitizer findings in
  MutableDSP `PATCHES.md`.
