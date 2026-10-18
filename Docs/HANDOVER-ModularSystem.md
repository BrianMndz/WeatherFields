# Handover: starting the modular music system repo

> **Status, 2026-10-18.** The repository exists: WeatherFields, namespace `wf`
> (steps 0 to 3 below are done, and step 5 is under way: Clouds runs on Bela).
> `PROJECT_GUIDE.md` holds the current status, next actions and decisions, and
> wins wherever this file disagrees. This file stays for its background: how
> the user works (section 3), the platform lessons (section 8) and the
> milestones (section 9), which are kept up to date.

Written 2026-10-17 at the end of the CloudsEngine work. It starts a **new, empty
repository** in a fresh session. Copy this file and `ModularMusicSystem.md`
into the new repo's `docs/`. Read this file first.

`ModularMusicSystem.md` describes the vision. Where the two disagree, this file
wins: it records the decisions taken since.

The repository name is still open. `<name>` stands for it throughout (directory,
CMake namespace, C++ namespace); replace it in step 0. The candidates are:

- **Mycelium** (favourite)
- **Palimpsest**
- **Triptych**
- **Microclimate**
- **Tapeloom**

---

## 1. Where this comes from

`/Users/brianmendoza/Development/audiodev/CloudsEngine` (branch `devel`, commit
`e7738d9`) is a faithful, portable port of the Mutable Instruments Clouds
engine. It has:

- `Modules/CloudsDSP`: a Git submodule (`github.com/BrianMndz/CloudsDSP`, commit
  `f69c1f4`). It is a curated fork of the MI Eurorack code at upstream commit
  `08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4`, holding only the files Clouds
  needs (`clouds/`, `stmlib/`) in the original layout. Provenance is in
  `UPSTREAM.md` and `UPSTREAM_FILES.txt`; every change is in `PATCHES.md`.
- `Engine/` (`CloudsEngine::core`): a C++14 wrapper that compiles with Bela's
  Clang 3.9.
  - `Engine` takes 32 stereo int16 frames per block at 32 kHz.
  - The host provides two memory arenas of exactly 118,784 and 65,408 bytes.
  - Status enums; no heap allocation.
  - `BlockAdapter` accepts any callback size, as planar float.
  - `SampleRateAdapter` supports 32, 44.1 and 48 kHz with linear
    interpolation. Band-limited resampling is still to do.
- `Apps/CLI`: deterministic renders of every quality × mode.
- `Source/`: a JUCE standalone app with a full Clouds panel.
- `Apps/Bela`: `Render.cpp`, plus a p5.js browser panel (`sketch.js`) speaking
  a small WebSocket protocol, IN GAIN, the level meter, and `Profiler.h`.
- `Tools/Bela`: `check.sh` checks code with Clang 3.9 on the Mac against the
  board's headers. `deploy.sh` stages, copies and builds on the board.
  `profile.sh` measures callback cost.
- `PROJECT_GUIDE.md`: the engine contracts, measurements and lessons.
  Read its "Engine contracts" and "Adapters, service and concurrency" sections
  before touching the Clouds code.

The CloudsEngine repo stays as it is: the Clouds reference and its panel. The
new repo takes the portable parts (section 7 lists exactly which).

## 2. Decisions so far

| Date | Decision |
|---|---|
| 2026-10-10 | Everything runs in JUCE/desktop first, then moves to Bela and Seed3. |
| 2026-10-10 | Bela's analog/digital I/O stays enabled, because peripherals and PO-33 sync need it. Never propose disabling it to save CPU. |
| 2026-10-16 | A new repo for the whole system. |
| 2026-10-16 | Engines first, from simple to bigger, with the architecture kept in mind. **Scenes come later.** |
| 2026-10-16 | **Every milestone must be playable.** Never wait for infrastructure to make sound. |
| 2026-10-16 | The musical focus is **lo-fi beats, granular sound design, dark ambient and synth music**. G-funk and hip-hop are no longer goals. |
| 2026-10-16 | Bela's module list is cut down. The problem was build effort, not CPU. Deferred: the sequencer (the PO-33 is the beat sequencer), the scene manager, the modulation engine, the sample manager and the recorder. Skipped: a separate reverb and feedback processor, because Clouds has both. |
| 2026-10-17 | More Eurorack ports will follow (Rings and Plaits are the likely ones). |

## 3. How the user works

- Small, incremental steps. After each step the user tests it (by listening),
  often asks for a review, then commits it themselves. **Don't commit unless
  asked.**
- The user asks Claude to implement directly. They are a C/C++ engineer with
  DSP experience, so explain the design reasons, not the basics.
- For Eurorack ports, **match the firmware closely**. Read the behaviour in the
  original source and copy it. State any deliberate deviation and why.
- Keep one condensed `PROJECT_GUIDE.md`: purpose, a status table, numbered next
  actions, contracts, build commands and open questions. **Edit it in place
  after each step; never append a log.** Long detail goes to an archive under
  `docs/`.
- When the user pastes a review finding, check it against the code first, then
  fix it and explain.
- Report measurements honestly. Keep these separate: observed maxima,
  callback-cost overruns, device underruns, and WCET (which is never claimed).
- Reviews often come from a separate reviewer, pasted as findings, sometimes
  with a harness left under `/private/tmp/weatherfields-*-review.*`. Check each
  finding against the code (and the library sources it depends on), fix it,
  and rerun the harness, adapted if the fix changes a contract it relied on.
- The Bela board is also used for other work. When the user says it is busy,
  run nothing on it (no `deploy.sh`, no SSH) until they say it is free; local
  checks such as `check.sh` are fine.
- The user develops in CLion, which loads `CMakePresets.json` as CMake
  profiles sharing `build/<preset>` with the terminal.
- For screen design (the OLED), show about four proposals drawn with u8g2
  itself (real pixels, a scratch program against `build/dev`) and let the
  user choose before coding. They prefer quiet UI: no popup for what the
  screen already shows. Mockups they keep go to `References/CloudsOled/`.

## 4. Repository architecture

```text
<name>/
├── CMakeLists.txt            # options: <NAME>_BUILD_DESKTOP (JUCE), <NAME>_BUILD_CHECKS
├── PROJECT_GUIDE.md
├── docs/                     # this handover, ModularMusicSystem.md, profiles, archives
├── ThirdParty/
│   ├── JUCE/                 # submodule; desktop only
│   └── MutableDSP/           # submodule: curated MI Eurorack fork (today's CloudsDSP, see 5)
├── Core/                     # <name>::core: C++14, no JUCE/Bela/Daisy/OS APIs
│   ├── include/<name>/
│   │   ├── Engine.h          # the common engine interface (section 6)
│   │   ├── Params.h          # ParamId, ParamSpec, ParamTable
│   │   ├── Events.h          # ControlEvent (POD), SpscQueue<T, N>
│   │   ├── Midi.h            # MIDI 1.0 message parsing, MidiMap (table-driven)
│   │   ├── Dsp.h             # IsFinite, GainRamp, PeakFollower, small shared DSP
│   │   └── Transport.h       # later: tempo, bar/beat, pulse output
│   └── src/
├── Engines/                  # one library per engine, each C++14 and platform-free
│   ├── Clouds/               # CloudsEngine's Engine + adapters, plus a <name>::Engine wrapper
│   ├── Looper/
│   └── Tone/                 # the Seed3 "Tone Engine", also runs on desktop
├── Rigs/                     # a rig is what runs on one device: engines + routing + MIDI map
│   ├── BelaRig/              # Clouds + Looper + mixer + MIDI map; runs on desktop AND Bela
│   └── SeedRig/              # Tone (later Rings/Plaits); runs on desktop AND Seed3
├── Platforms/                # hosts: device audio/MIDI/GUI I/O only, no musical logic
│   ├── Desktop/              # JUCE GUI app: one audio callback runs one or both rigs
│   ├── Bela/                 # render.cpp + browser GUI (sketch.js)
│   └── Daisy/                # libDaisy main + Makefile
├── Tools/
│   ├── Render/               # offline CLI: WAV in + scripted events → WAV out + SHA-256
│   └── Bela/                 # check.sh / deploy.sh / profile.sh (ported, section 7)
└── Tests/                    # small check executables, registered with CTest
```

Rules:

1. **Dependencies point one way.** The chain is Platforms → Rigs → Engines →
   Core → ThirdParty. Nothing below Platforms includes JUCE, Bela, libDaisy or
   OS headers, or does file or network I/O.
2. **C++14 below Platforms**, so Bela's Clang 3.9 builds it. Use
   `-pedantic-errors`, as CloudsEngine's `Engine/CMakeLists.txt` does.
   Public headers must also compile as C++17. Desktop code may use C++17 or
   newer (JUCE requires it).
   - Nothing from C++17: no `std::optional`, `string_view` or structured
     bindings.
   - No exceptions or RTTI in real-time code (Daisy builds without them).
3. **Real-time rules in every `process()`:**
   - no allocation after `prepare()`
   - no locks
   - no I/O or logging
   - no `std::function`
   - no virtual calls per sample (per block is fine)
4. **A rig is the unit of portability.** Desktop-first means running *the same
   rig* in the JUCE host. Moving it to Bela or Seed3 is then a new host file,
   not a rewrite.
5. **The desktop host simulates the whole system.** One JUCE audio callback
   can run BelaRig and SeedRig together, with the Seed's output fed into the
   Bela rig's input as the cable would. Use one callback, not
   `AudioProcessorGraph`, to mirror Bela's `render()`.
6. **Parameters have stable numeric IDs from day one.** Scenes, modulation,
   MIDI maps and the Bela↔Seed3 protocol then become data on top of them.
   Never renumber an ID; retire it instead.
7. **Generalise only on the second use.** For example, the Clouds-specific
   `BlockAdapter`/`SampleRateAdapter` become shared code in `Core/` when the
   second MI engine (with another native rate and block size) needs them.

## 5. Mutable Instruments ports: shared `stmlib`

All MI modules use their own copy of the same `stmlib`. Linking two separately
curated forks (say CloudsDSP and a RingsDSP), each with its own `stmlib/*.cc`,
gives duplicate symbols, or silent ODR violations.

**Decision to confirm with the user in step 0:** grow the existing CloudsDSP
repo into one curated fork, for example renamed `MutableDSP`. GitHub redirects
renamed repos. It would hold:

- One `stmlib/`.
- A directory per module (`clouds/`, `rings/`, `plaits/`), all from the
  **same upstream commit** `08460a6…`, so they agree on `stmlib`.
- One CMake target per module (`MutableDSP::clouds`, `MutableDSP::rings`, …),
  each linking a shared `MutableDSP::stmlib` target.
- The existing policy: copy only the files each module needs, keep license
  headers, and log every change in `PATCHES.md` and `UPSTREAM_FILES.txt`.

The `stmlib` files currently present were picked for Clouds. Adding a module
adds the `stmlib` files it needs, from the same commit.

Other facts about the MI code:

- `TEST=1` must be defined identically in the DSP library and in every
  translation unit that includes its headers (`PUBLIC` in CMake). It swaps
  ARM-only inline assembly for portable C.
- `stmlib::Random` is **process-global**. Today only one Clouds instance is
  supported. With several MI engines in one process, they share one random
  stream, so a render is reproducible only for the whole rig, not per engine.
  Seed it explicitly (CloudsEngine uses `0x23` for its references).

**Recipe per module** (this is how Clouds went):

1. Curate the dependency closure into MutableDSP.
2. Write a C++14 wrapper `Engine` at the firmware's native rate and block
   size:
   - the host provides the memory
   - a full control snapshot per block
   - one-block event pulses
   - status enums
   - the firmware's own control semantics, read from its `ui.cc`, `cv_scaler.cc`
     and `settings`
3. Write a deterministic CLI render and record its SHA-256 per configuration.
4. Wrap it as a `<name>::Engine` with a parameter table.
5. Run it on desktop and listen.
6. Profile it on the target hardware.

**Where they belong:**

- **Rings** (resonator) is the document's "second Seed3 engine". It fits dark
  ambient.
- **Plaits** (macro oscillator) is a strong Seed3 synth voice.
- The MI firmware was written for an STM32F4 (Cortex-M4F at 168 MHz). A Daisy
  Seed with an H7-class Cortex-M7 is the natural home; confirm Seed3's chip,
  clock and RAM before relying on that.
- On Bela, Clouds already uses ~11–31% of the CPU on average. A rough planning
  guess (not a measurement) is 1.5–2 Cortex-A8 cycles per F4 cycle.

## 6. Core interfaces (sketch, to refine in milestone 1)

```cpp
namespace <name> {

using ParamId = std::uint16_t;                  // stable forever

enum class ParamKind : std::uint8_t { Continuous, Choice, Toggle, Trigger };

struct ParamSpec {
    ParamId id;
    const char* name;                            // static storage
    ParamKind kind;
    float min, max, def;                         // plain units; Choice: 0..count-1
    const char* unit;                            // "dB", "st", "" ...
};

struct ParamTable { const ParamSpec* specs; std::size_t count; };

struct ControlEvent {                            // POD: fits a queue, UART frame or scene file
    enum class Type : std::uint8_t { Param, NoteOn, NoteOff, Trigger };
    Type type;
    std::uint8_t target;                         // engine slot within the rig
    ParamId id;                                  // Param/Trigger: parameter; Note*: note number
    float value;                                 // Param: plain value; Note*: velocity 0..1
};

class Engine {
public:
    virtual ~Engine() = default;
    virtual const ParamTable& Params() const noexcept = 0;
    virtual bool Prepare(float sampleRate, std::size_t maxFrames) noexcept = 0; // not real-time
    virtual void Handle(const ControlEvent& event) noexcept = 0;                // real-time, before Process
    virtual void Process(const float* inL, const float* inR,
                         float* outL, float* outR, std::size_t frames) noexcept = 0;
};

}  // namespace <name>
```

**Control flow:**

- **Producers.** Each producer thread owns one `SpscQueue<ControlEvent>`.
  Producers are the JUCE MIDI callback, the GUI/WebSocket thread, and later
  the scene loader.
- **Consumer.** At the start of each callback, the audio thread drains every
  queue into the rig. The rig validates and clamps at that boundary, using the
  `ParamSpec`. It then calls `Handle()` on the target engine, then `Process()`.
- **One mutator.** All engine state changes on the audio thread only. That is
  what later makes a scene (a batch of events) atomic. CloudsEngine's
  per-control atomics could not offer that.
- **MIDI.** On desktop, use your own SPSC queue fed by
  `MidiInputCallback`. Avoid `juce::MidiMessageCollector` on the audio path,
  because it takes a lock. On Bela, read MIDI in `render()` as Bela's MIDI
  examples do, then confirm the mode-switch (MSW) count stays at 1.
- **`MidiMap`.** A table with entries like (port, channel, CC or note) →
  (target, ParamId, range, curve) or a trigger. Start with a hard-coded table
  for the ROLI blocks; load it from JSON later, off the audio thread.
- **Outputs to the GUI** (meters, loop position): the audio thread stores them
  in atomics, and a non-real-time thread reads and sends them. This is the
  CloudsEngine step 9.3 pattern.

**The Clouds wrapper:**

- It keeps a `clouds_engine::ControlSnapshot` and turns `Handle()` into field
  updates. TRIG and FREEZE map to the trigger/gate semantics in CloudsEngine's
  guide.
- Quality and mode are `Choice` parameters. **They erase the recording**
  (quality always; mode only to or from spectral), and freeze must be released
  first. The wrapper applies `SetQuality`/`SetPlaybackMode` plus `Service()`
  on the audio thread, as Bela does today.
- Only one instance is allowed (global random state).
- Bela stays on granular with `ServicePolicy::Manual`. Stretch and spectral
  have not been profiled on Bela.

## 7. What to bring over from CloudsEngine (`e7738d9`)

| From | To | Change |
|---|---|---|
| `Modules/CloudsDSP` (submodule) | `ThirdParty/MutableDSP` | Same repo or the renamed one; add a separate `stmlib` target when the second module arrives |
| `Engine/` (src, include, tests) | `Engines/Clouds/` | Copy as is; add the `<name>::Engine` wrapper beside it |
| `Apps/CLI/main.cpp` | `Tools/Render/` | Keep the Clouds timeline; acceptance is identical SHA-256 for all 16 quality × mode renders, compared with CloudsEngine's `clouds_cli` built from `e7738d9` |
| `IsFinite`, `PeakFollower`, the gain ramp in `Apps/Bela/Render.cpp` | `Core/` (`Dsp.h`) | Make them platform-free; keep the exponent-bit `IsFinite` (see section 8) |
| `Apps/Bela/Profiler.h` | `Platforms/Bela/` | Generalise the presets to drive a rig |
| `Apps/Bela/Render.cpp`, `sketch.js` | `Platforms/Bela/` | Patterns to reuse: the meter thread, hello-ID sync, atomics checks. Generate the panel from `ParamTable` instead of hand-written knobs at first |
| `Tools/Bela/*` | `Tools/Bela/` | Make the source list per rig: it currently reads the targets `clouds_engine_core` and `clouds_dsp`, `Apps/Bela` and `PROJECT_INCLUDE_DIRS`. Rename the `clouds-engine` cache directory and the `CLOUDS_PROFILE` macro. Add setting the board clock to `deploy.sh` (section 8) |
| `Source/` (JUCE Clouds panel) | stays | Optional later: reuse its drawing for a Clouds view in the desktop host |

## 8. Platform notes and hard-won lessons

**Desktop (JUCE):**

- Use `juce_add_gui_app` with an `AudioDeviceManager` and an
  `AudioDeviceSelectorComponent`. A plugin can come later.
- `DONT_SET_USING_JUCE_NAMESPACE=1` avoids the `juce::Random` /
  `stmlib::Random` clash.
- The ROLI Lightpad and Control Block are class-compliant USB MIDI, so test
  them on the Mac first. Log what each control sends before mapping it: a
  MIDI monitor, or `amidi -d` on Bela. The Lightpad may send per-note
  channels, pitch bend, CC74 and pressure (MPE style). Verify; don't assume.
- The PO-33 goes into the audio interface's input.
- **A dialog launched with `launchAsync()` outlives its owner.** JUCE's modal
  manager deletes it only when it closes, or at shutdown after `shutdown()`
  has destroyed the main window; an `AudioDeviceSelectorComponent` then
  unregisters from a destroyed `AudioDeviceManager`. Keep a `SafePointer` and
  delete it first.
- **Warnings:** `juce_recommended_warning_flags` adds `-Wfloat-equal`, which
  fires on Core's deliberate exact comparisons; it arrives through the link
  interface, after the target's own options, so it cannot be turned off
  there. The desktop target uses the repo's `-Wall -Wextra` instead.
- **CLion and the terminal share `build/desktop`.** A configure run while CLion
  regenerates can fail once; run it again.

**Bela (BeagleBone Black + Bela cape):**

- **The software is old.** Debian 9.13, Clang 3.9.1, libstdc++ 6, Xenomai on
  Linux 4.14, Bela core `e43b8abe`. Bela's flags include
  `-O3 -ffast-math -DNDEBUG`, so `assert` does nothing.
- **NaN checks.** Under `-ffast-math`, Clang 3.9 folds `std::isfinite()` to
  true for NaN (checked on the board). Test the exponent bits instead with
  `IsFinite()`.
- **No microSD.** The microSD reader is broken; the board boots from eMMC and
  recovery is hard. Don't upgrade the Bela image or core casually.
- **No clock battery.** The board boots in 2016 unless the browser IDE sets the
  time. Then make sees sources older than their objects and rebuilds nothing.
  Fix: `ssh root@192.168.7.2 date -u -s @$(date +%s)`, then touch the sources
  on the board. Put this in `deploy.sh`.
- **Real-time thread** (`render()`): no allocation, locks or syscalls. Use
  Xenomai's calls such as `__wrap_clock_gettime()`, because Bela's Makefile
  removes the POSIX wrappers. The `bela-audio` line in
  `/proc/xenomai/sched/stat` must keep MSW = 1 (the startup switch).
  `gui.sendControl()` is not real-time safe: send from a `std::thread`.
- **Atomics.** The old library lacks `is_always_lock_free`. Use
  `static_assert(__atomic_always_lock_free(sizeof(float), 0) && ATOMIC_BOOL_LOCK_FREE == 2 && ATOMIC_INT_LOCK_FREE == 2)`.
- **Inputs are read-only.** Copy them into the output buffers (with IN GAIN)
  and process in place.
- **Bela's Makefile** compiles only `.cpp`/`.c` files, and tracks header
  dependencies only for files at the project's top level. `deploy.sh` stages
  every translation unit at the top, and wraps `.cc` files in one-line
  `.cpp` includes. The board's make flags: `CPPFLAGS=-std=c++14 -DTEST=1`.
- **The browser GUI** broadcasts to every open page, so replies echo a random
  hello ID. Page state the board keeps (the OLED's `wf-focus`) needs an
  owner too: with two pages open, one page's repeats or timeout otherwise
  override the other's.
- **Make does not track flags.** A new `-D` was silently ignored by unchanged
  files; `deploy.sh` keeps the last CPPFLAGS on the board and rebuilds
  everything when they differ.
- **The OLED's I2C bus** (bus 1, 400 kHz) takes about 35.5 ms per full
  128x64 frame in u8g2's 24-byte transfers, so about 28 fps at most; 25 is the
  default. Draw it on an ordinary thread, never in `render()`.
- **The Gui library cannot be stopped.** `Gui::cleanup()` is empty and
  `WSServer::cleanup()` only asks seasocks to terminate; nothing joins its
  threads, so a message can arrive during or after `cleanup()`. Whatever its
  callback touches must outlive the process, or be guarded by a flag it
  checks under a mutex that is never destroyed (`GuiLink`).
- **The panel's sync:** the board sends values only when they change, so a
  page must keep the board's latest report and show it once a local edit's
  hold ends, never drop it; and resend momentary controls (TRIG's gate) on
  every connection, since a release made while disconnected is lost.
- **Bela's Makefile picks the libraries** to link (Gui, WSServer) from
  `#include <libraries/...>` in the project's source files; include them in a
  `.cpp`, not only in a header.
- **Node on this Mac** gets EHOSTUNREACH to 192.168.7.2. Tunnel instead:
  `ssh -f -N -L 15555:127.0.0.1:5555 root@192.168.7.2`, then connect to
  `ws://127.0.0.1:15555/gui_control`.
- **Block size.** Use 32-frame callbacks (726 µs deadline at 44.1 kHz); 16
  frames underran with Clouds. Raise to 64 or 128 if the rig's peaks need
  headroom.
- **Analog/digital I/O stays enabled.** It is planned for PO-33 sync pulses and
  peripherals.
- **Audio inputs.** There is only one stereo input. The PO-33 and Seed3 both
  feeding Bela means a small mixer, or using Bela's analog inputs (DC-coupled;
  check their level and rate). Decide before milestone 3.

**Measured on Bela at 32 frames** (CloudsEngine `docs/bela-profile-2026-10-*.txt`):

- Clouds granular at defaults: about 11% mean, 162 µs max (22%).
- Heaviest preset (mono 16-bit, all grains): 376 µs max (52%).
- Bela's own overhead: about 7%.
- No underruns.
- Everything else planned for Bela is control code or memory copies, so cheap.
  Profile after each new engine anyway.

**Daisy Seed3:**

- Not used yet. Check the board's specifications and libDaisy support first.
- The build uses arm-none-eabi-gcc, so the C++14 code compiles as is.
- Use static allocation only. Put large buffers in SDRAM explicitly.

## 9. Milestones

Each milestone is first playable on desktop, then on the hardware. Split each
into small steps the user can test.

| # | Playable result | Desktop | Hardware |
|---|---|---|---|
| 1 | **ROLI → Clouds**: Lightpad XY plays position/density; Control Block buttons do freeze, trigger and quality | JUCE host + BelaRig(Clouds) + MidiMap | BelaRig on Bela with USB MIDI |
| 2 | **One RAM loop** (record, play, overdub, clear) feeding Clouds' input: a PO-33 beat turns into texture | Looper engine | Bela |
| 3 | **Tone Engine v1**: mono saw/pulse, low-pass filter, envelope, drive, glide | SeedRig in the same callback, its output into BelaRig | Seed3; Bela forwards notes over MIDI |
| 4 | **Transport + PO-33 sync**; loop length snaps to bars | Transport in Core | Pulse out on Bela's I/O |
| 5 | **Loop extras** (reverse, half speed, feedback), a mixer, a tape delay | Looper/Core | Bela |
| 6 | **Two LFOs** routable to any ParamId on either rig | Core | Bela; Seed3 via PARAM messages |
| 7 | **Scenes**: saved events per rig, recalled as one batch | Core + JSON in the host | Bela, files on USB storage |
| 8 | **Recorder**: SPSC FIFO to a writer thread to a WAV on USB | Desktop host | Bela |
| — | **More MI ports** (Rings, Plaits): one per milestone, following the recipe in section 5 | Engines/ | Seed3 or Bela, after profiling |

Later, not planned yet:

- polyphony and wavetables in Tone
- the sequencer's event lanes
- MPE
- streaming samples from USB
- UART binary protocol to Seed3 (USB/MIDI first)

## 10. First session: step list

0. **Setup decisions.** Ask the user for:
   - the repo name
   - the MutableDSP plan in section 5, or keeping CloudsDSP for now
   - the remote, and the default and working branches. CloudsEngine uses
     `main` plus `devel`.

   Then create the skeleton: CMake, the submodules, an empty `Core`, CTest,
   `PROJECT_GUIDE.md`, and copies of this file and `ModularMusicSystem.md`.
1. **Bring Clouds over** (section 7). The CLI renders must match CloudsEngine's
   SHA-256 values for all 16 combinations. `Tools/Bela/check.sh` must pass on
   the copied sources.
2. **Core types and the wrapper.** Write `Params.h`, `Events.h` (SPSC queue
   plus check) and the Clouds `<name>::Engine` wrapper with its ParamTable:
   - the nine controls, IN GAIN, freeze, trigger, quality and mode
   - IDs and ranges as in CloudsEngine's panel; host pitch ±24 st
   - a check that events reach the snapshot and that out-of-range values are
     clamped at the rig.
3. **Desktop host, first playable.** BelaRig(Clouds), audio in → out, and a
   generic panel: one slider per `ParamSpec`, plus buttons for Toggle/Trigger.
   Listen with the PO-33.
4. **MIDI.** Log the ROLI messages, write a hard-coded `MidiMap`, and play
   Clouds from the blocks. That completes milestone 1 on desktop.
5. **Bela host** for BelaRig. Port the tools, the generic browser panel and
   USB MIDI. Profile it, then listen. That completes milestone 1.

## 11. Open questions

- The repo name; the MutableDSP rename; whether Clouds' Engine stays copied or
  CloudsEngine later consumes the new repo.
- The Seed3 hardware (chip, RAM, audio codec, USB host or device) and how it
  connects to Bela (USB MIDI, TRS MIDI, UART).
- Input routing on Bela for the PO-33 plus Seed3 (mixer or analog inputs).
- Clouds deferred work, carried over:
  - band-limited resampling
  - make-up gain (fully dry is about −9 dB)
  - stretch and spectral cost on Bela
  - the sanitizer findings listed in CloudsDSP `PATCHES.md`
