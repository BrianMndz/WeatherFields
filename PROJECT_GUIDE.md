# WeatherFields Project Guide

Last updated: 2026-10-17.

Read this at the start of a session. It holds the current status, next
actions, contracts and decisions. Edit it in place after each step; never
append a log. Long detail goes to an archive under `docs/`.

- [`docs/ModularMusicSystem.md`](docs/ModularMusicSystem.md): the vision.
- [`docs/HANDOVER-ModularSystem.md`](docs/HANDOVER-ModularSystem.md): the
  decisions up to 2026-10-17, how the user works, platform lessons and the
  milestones. Where it disagrees with this guide (its section 4 layout, its
  `ThirdParty/` and `<name>`), this guide wins.

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
- Check a pasted review finding against the code before fixing it.
- Report measurements honestly: observed maxima, callback-cost overruns,
  device underruns and WCET (never claimed) are different things.

## Status

| Area | Status |
|---|---|
| MutableDSP | `Modules/MutableDSP`, `main` at `24b201f`: Clouds closure imported unchanged from Eurorack `f4cfa76` (stmlib `e3bd7c9`), CloudsDSP patches applied, `MutableDSP::stmlib` and `MutableDSP::clouds` targets. Object code byte-identical to CloudsDSP's. |
| JUCE | `Modules/JUCE` pinned at 9.0.4; configures from `Platforms/Desktop`. No app yet. |
| Build skeleton | Top-level CMake, presets `dev` and `desktop`, `cmake/WeatherFields.cmake`, CTest. |
| Core | Header-only `wf::core` with `wf/Dsp.h` (`IsFinite`). Checked as built and under `-O3 -ffast-math`; compiles for the board with Clang 3.9. |
| Engines/Clouds | `wf::clouds`: CloudsEngine's `Engine/` from `e7738d9`. Headers moved to `include/wf/clouds/` (includes `"wf/clouds/..."`); the code is unchanged and keeps namespace `clouds_engine` until the wrapper in step 2. Its five checks pass in Debug and Release; their only other change is `std::byte` → `unsigned char` for the test arenas, as checks compile as C++14. |
| Tools/Render | `wf_render_clouds`: CloudsEngine's CLI (`e7738d9`), only its include and name changed. CTest `wf_render_clouds_references` renders all 16 quality × mode combinations and compares them with `clouds_references.sha256`, taken from CloudsEngine's `clouds_cli` built from `e7738d9`: byte-identical in Debug and Release, and equal to CloudsEngine's recorded table. The references hold for macOS arm64 with Apple Clang. |
| Rigs, hosts | None yet. |
| Tools/Bela | Copied from CloudsEngine, not adapted: still reads CloudsEngine paths and targets. |

## Next actions

1. **Finish bringing Clouds over** (handover section 7): CMake-generated
   per-app source manifests; `Tools/Bela/check.sh` reads them and passes on
   the copied sources.
2. **Core types and the Clouds wrapper**: `Params.h`, `Events.h` (SPSC queue
   plus check), the `wf::Engine` and `wf::Rig` interfaces with host-provided
   memory, `SoloRig<E>`, and the Clouds `wf::Engine` with its ParamTable
   (handover section 10, step 2).
3. **Desktop host, first playable**: a JUCE app with a rig menu, audio in to
   out, and a generic panel built from `ParamSpec`. Listen with the PO-33.
4. **MIDI**: log the ROLI messages, write a hard-coded `MidiMap`, play Clouds
   from the blocks. Milestone 1 on desktop.
5. **Bela host**: port the tools, the generic browser panel and USB MIDI;
   profile, then listen. Milestone 1 complete.

## Architecture

```text
WeatherFields/
├── CMakeLists.txt      # CMake ≥ 3.25; WF_BUILD_TESTS, WF_BUILD_DESKTOP; no global C++ standard
├── CMakePresets.json   # dev (no JUCE), desktop; later daisy
├── cmake/              # wf_add_rt_library, wf_add_check; later app manifests, Daisy toolchain
├── Modules/            # submodules: JUCE, MutableDSP; later libDaisy
├── Core/               # wf::core
├── Engines/<Name>/     # wf::<name>: CMakeLists, include/wf/<name>/, src/, tests/
├── Rigs/               # SoloRig<E>, BelaRig, SeedRig, the whole system
├── Platforms/          # Desktop (JUCE), Bela, Daisy: host libraries + their app lists
├── Tools/              # Render (offline CLI), Bela (check/deploy/profile)
├── Tests/              # cross-component checks only
└── docs/
```

Contracts:

1. **Dependencies point one way**: Platforms → Rigs → Engines → Core →
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
8. **Parameter IDs are stable forever**: retire an ID, never renumber it.
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
- **MI namespaces:** inside `namespace wf { namespace clouds {`, an unqualified
  `clouds::` finds `wf::clouds`. Wrappers write `::clouds::`.
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

## Open questions

- Seed3 hardware (chip, RAM, codec, USB host or device) and its link to Bela
  (USB MIDI, TRS MIDI, UART).
- Input routing on Bela for the PO-33 plus Seed3 (mixer or analog inputs);
  decide before milestone 3.
- Whether Clouds' engine stays copied here or CloudsEngine later consumes
  this repo.
- Beads vs Clouds on Bela: compare cost and sound once both run on desktop.
- Clouds deferred work: band-limited resampling, make-up gain (fully dry is
  about −9 dB), stretch and spectral cost on Bela, the sanitizer findings in
  MutableDSP `PATCHES.md`.
