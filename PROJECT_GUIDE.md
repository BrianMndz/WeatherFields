# WeatherFields Project Guide

Last updated: 2026-10-30.

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
- **Except the looper (milestone 2), a tutoring project** as CloudsEngine
  was: the project is also a way to learn real-time audio, DSP and embedded
  Linux. Claude introduces a small problem, the user proposes and implements
  it, then we explain, review, listen and measure together. Claude writes no
  production code for it unless the user authorizes it; documentation edits
  are fine.
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
| Engines/Stratus | Stratus, the looper (`wf::stratus::Engine`), written by the user as lesson 1 (2026-10-26), committed up to `22b0600`; in the desktop menu as a solo rig (generic panel, OLED test pattern), listened 2026-10-30: works. Target `wf::stratus` (`src/Engine.cpp`, links `wf::core`), manifest `stratus`. `MemoryBytes()`: 30 s, two channels, each aligned; 0 for an unsupported rate. `Prepare()`: rates up to 192 kHz (sizes fit Bela's 32-bit `size_t`), checks before carving, the buffer never cleared. Parameters in ID order: 0 Play, 1 Record (Toggles the engine sets itself), 2 Clear (Trigger), 3 Loop Level (-60 to 0 dB, default 0; -60 is gain 0), 4 Thru (default on). Transitions are private helpers, a `switch (state_)` each, duplicates no-ops: `OnRecord()`, `OnPlay()`, `OnClear()` (O(1)) and `FinishFirstTake(next)` (length from the frames written; to Playing on Record off or at capacity, to Stopped on Play off; a zero-frame take goes back to Empty). `ParamValue()` derives Play (Recording, Playing, Overdubbing) and Record (Recording, Overdubbing) from the state through `PlayOn()`/`RecordOn()`, readback only. `Handle()`: deaf before `Prepare()`; Loop Level's gain computed once per event. `Process()`, with 5 ms Thru and overdub input ramps (rounded up to whole frames): silent before `Prepare()`; per frame (the state can change mid-block when the take fills the buffer), both inputs read before either output is written; output `in × dryGain`, plus the loop × Loop Level while Playing or Overdubbing, read before adding input × inputGain; Playing keeps writing during punch-out; the first take records the input before Thru; wrap at `length_`; overdubs sum unbounded. `wf_stratus_engine_check`: the table; the whole Record/Play table through a `SoloRig` with duplicates; Clear from every state; zero-frame takes; the rig's constraints; deafness before `Prepare()` and after a failed one; playback, overdub, Thru and Loop Level sample by sample; in-place byte-identical to separate buffers in every state; the maximum length at 1 kHz (30,000 frames). Mutation-tested 2026-10-26: all 17 mutants caught (before ramps). Thru ramp added 2026-10-30: integer gain position, one step per frame before use, exact endpoints, reverses from its current position; readback follows the target immediately. Checks cover 100 Hz, 1/44.1/48/192 kHz, callback boundaries, zero frames, duplicates, reversal, Clear and re-Prepare; existing audio checks settle Thru before steady-state assertions. Overdub input ramp added 2026-10-30: the shared `AdvanceRamp()` moves each gain once per frame; Record off reads back immediately while Playing writes the input fade-out, including across wraps; re-punch reverses from the current gain. First takes stay full-level. Until step 3, Play stop cuts the input gain and can write a click into the retained loop; code and checks mark this temporary behavior for replacement by a moving playhead and fading punch. Clear permanently cancels pending writes because the loop is discarded. Buffer readback checks both ramps at six rates, exact endpoints, duplicates, reversal, zero frames, Record from Stopped, stop/Clear cancellation and full-level first takes. `SetThru()` derives its default settling duration from `kSampleRate`. Desktop listening pending for both ramps. |
| Tools/Render | `wf_render_clouds`: CloudsEngine's CLI (`e7738d9`), only its include and name changed. CTest `wf_render_clouds_references` renders all 16 quality × mode combinations and compares them with `clouds_references.sha256`, taken from CloudsEngine's `clouds_cli` built from `e7738d9`: byte-identical in Debug and Release, and equal to CloudsEngine's recorded table. The references hold for macOS arm64 with Apple Clang. |
| Desktop host | `wf_desktop` (`WeatherFields.app`, `desktop` preset): rig menu (Clouds' and Stratus' `SoloRig`s), audio settings saved between runs, status line (rate, block, CPU, a rig's failed `Prepare()`, GUI overflow and dropped counts), and the rig's view (`RigView`): its own if `RigEntry::makeView` gives one, else `RigPanel` generated from `ParamSpec` (knobs, label menus, latching toggles, trigger buttons). Clouds' view is `Clouds/CloudsPanel`, CloudsEngine's front panel (`e7738d9`) ported to the host: same drawing, B/C/TRIG behaviour, LED modes and mode tooltips, but it sends events and shows published values, so engine-made changes appear; its meter is the host's input level times IN GAIN, or the output level while frozen. Tested 2026-10-18: behaves as in CloudsEngine. `AudioHost` runs the rig in JUCE's callback: GUI `ControlInput` drained (at most 32 per callback), then `ParamValue()` published to atomics, then `Process()` from scratch buffers, with `PeakFollower`s on the rig's input and output in pieces of at most the prepared block; silent input when there is none, mono input to both sides. The rig is prepared in `audioDeviceAboutToStart()`; a device restart replays the published values as events, Choices first. While a `Prepare()` has failed, the callback is silent and neither drains nor publishes, so queued events wait and the last valid values survive. Rig swaps detach the callback, discard queued events, destroy the old rig before making the new one. When the GUI's input is full, its events wait in order; only a Continuous parameter's newer value replaces its waiting one (moving to the back), while Choice, Toggle and Trigger events are all kept, since they can act on transitions; the panel shows only published values. Builds clean with `-Wall -Wextra`. Generic panel listened 2026-10-18: works. "OLED..." opens the emulated screen (`OledView`, in its own window): a panel-less `wf::oled::Display` drawing the current rig's screen (`RigEntry::makeScreen`; Clouds' `OledScreen`, else the test pattern) from the host's published values and levels at the board's 25 frames a second, shown at the largest integer scale with nearest-neighbour scaling; lit pixels blue as on the board's panel, a click cycles blue, white, yellow. Test pattern looked at 2026-10-18: opens and animates. Clouds' screen looked at 2026-10-18: works, follows the panel. The panel's quality and mode displays set the host's view focus (`AudioHost::SetViewFocus()`, message thread, cleared by a rig swap and the panel's destructor), which the OLED's readout passes to the screen. |
| Bela host | `Platforms/Bela/Host`: `BelaHost.cpp` (Bela's `setup()`/`render()`/`cleanup()` around the app's `wf::bela::AppRig()` and `AppScreen()` (its OLED screen, or null for the test pattern), chosen at link time; 32-frame default period; rig memory allocated in `setup()`; failure refuses to start; `render()` drains the panel's `ControlInput` (at most 32 events), publishes `ParamValue()`s, processes Bela's buffers directly, meters input and output) and `GuiLink` (the browser panel over Bela's Gui library, generic by slot and ID: `wf-hello`/`wf-state`, `wf-param`, `wf-trigger`, `wf-focus` (what the page's display presents, into `Published::focus` with its time; the page repeats it every second while it lasts and the OLED ignores one older than 2.5 s, so a closed page leaves none; the focus has an owner, a random id per page: a new selection takes it, a repeat only refreshes its owner's or takes a lapsed one, only the owner clears it, so two open pages do not fight (review finding, confirmed with the reviewer's harness, adapted for page ids, and the real `sketch.js` in Node)), and `wf-update` about 30 times a second with levels and changed values; the WebSocket thread and the sender thread share one `ControlOutbox` under a mutex; only the sender thread sends; it is never destroyed, and `Stop()` closes it under the mutex, since Bela's Gui cannot stop its WebSocket server synchronously). App `bela_clouds` (`Apps/Clouds`: `CloudsApp.cpp`, and `sketch.js`, CloudsEngine's panel ported onto `wf-*` messages, with the playback mode display added; it keeps the board's latest report per control and shows it once the control is not held (500 ms after a local edit, or while dragging), and re-sends TRIG's gate on every connection). Built and run on the board 2026-10-18: starts at 44.1 kHz, 32 frames, audio thread MSW 1; the protocol checked end to end over a tunnel (state, updates, clamping at the rig, freeze released by a quality change, invalid messages ignored). Not yet listened to through the browser panel. The Bela review fixes (below) are checked locally only and not yet deployed. The OLED (`OledLink`, `I2cPanel`): every app drives it from `setup()` to `cleanup()` on an ordinary Linux thread (never `render()`), drawing the app's screen from the published values and levels (`bela_clouds`: Clouds' `OledScreen`) and sending it whole at up to `WF_OLED_FPS` (default 25) on I2C bus `WF_OLED_I2C_BUS` (1) at `WF_OLED_I2C_ADDRESS` (0x3C), all overridable with deploy's `-D`; it prints fps and draw/send times (mean, max) every 5 s. `I2cPanel` is our u8x8 glue: one `write()` per u8x8 transfer, state per display through u8x8's user pointer (defined on Linux), errors kept for the frame to check. A bus that cannot be opened, or a panel that does not answer, is printed once and never stops audio; the thread retries once a second and re-initialises the panel; `cleanup()` blanks it and joins. Run on the board 2026-10-18 with Clouds: the panel is at 0x3C on bus 1 (`i2cdetect`), the pattern shows as on the desktop; at a 30 fps cap it sent back to back at 27.2-28.2 fps, send 35.1-36.8 ms mean (40-51 ms max), draw 0.34-0.41 ms mean (up to 2.1 ms). About 1.16 KB a frame (1 KB of data in 24-byte transfers, plus addresses and commands), so the bus runs at 400 kHz with about 9 ms of per-transfer overhead. At the 25 fps default: a steady 24.9-25.0 fps, send 35.6-35.8 ms mean (up to 45 ms after start-up), draw about 0.33 ms mean. The outline reaches all four edges of the panel (SH1106 driver and column offset right), and Clouds sounds the same with the OLED running; the CPU cost with and without it is not yet measured. Clouds' screen run on the board 2026-10-18: follows the browser panel as the desktop one follows its panel. With nothing playing the cloud showed grains, which read as activity; it now thins with the output level, checked on both hosts 2026-10-18. |
| Bela controls | Wired 2026-10-24 (USB power): six 10k linear pots (Bojack, marked P103) on A0-A5 (POSITION, SIZE, PITCH, DENSITY, TEXTURE, BLEND), wiper to the input, legs on 3.3 V and GND; tactile buttons FREEZE on D0 and B on D1, from 3.3 V with a 10k pull-down (high when pressed). IN GAIN (A6) and TRIG (D2) not wired yet. Probed 2026-10-24 on USB power: every pot reads 0.00000 to 0.8275-0.8285 (3.39 V against the ADC's 4.096 V; an earlier 0.848 was likely the start-up reading, as the first callbacks read full scale on every wired input). Noise is proportional to the reading: at rest, raw peak-to-peak about 2 steps (of 1/65536) at the bottom, 100-130 at 0.4, 200-300 at the top; the callback mean's 0, 10-25 and 20-65 (at most 0.12% of the knob's range); resting values drift by about 20 steps, and all channels quieten together in some windows. A second run with the 3.3 V rail wired to A7 ruled out rail ripple: A7 itself (no pot) is as noisy as a pot at the top, and the ratio to it is slightly noisier than the direct reading (A3 at the top: 200-260 / 16-25 direct, 270-350 / 25-60 as a ratio in the same units) and drifts no less; so the noise is broadband (the ADC's reference, or rail noise too fast to share between inputs converted microseconds apart), for averaging and filtering, not division. The rail itself is stable (A7 0.82760-0.82840 over 90 s) and every pot's top matches it. With A7 driven, the floating A6 reads about 0.19 (charge from the input converted before it). No crosstalk between wired inputs (moving one leaves the others' noise unchanged; the floating A6 and A7 follow anything). Buttons: mostly one edge per press at the 22.7 us digital rate; two presses in the second run bounced (3 edges over 0.54 and 0.36 ms); presses 91-170 ms, the quickest tap 27 ms. `Platforms/Bela/Probe/PanelProbe.cpp` (manifest `bela_probe`): a standalone Bela program (no rig, no host) printing every 2 s, per analog input, the mean, the peak-to-peak noise in ADC steps (raw, and of each callback's mean over its analog frames) and the range since start; per button, presses, releases, glitches (bursts ending where they began), bounce (edges less than 10 ms apart: count and span; 30 ms merged a quick tap) and the shortest press. It skips the first 100 ms, and with the 3.3 V rail wired to A7 also prints each input's ratio to it (per sample, and per callback as the sums' ratio). `render()` hands each window to a printing thread through `wf::SpscQueue`. Passes `check.sh`; both versions run on the board 2026-10-24. The A7 jumper goes again: A7 stays spare. |
| Ui/Oled | `wf::oled` (C++14): `Display`, a 128x64 SH1106 in u8g2 full-buffer mode with its own 1 KB buffer; the default constructor has no panel (drawing only), the other takes the byte and GPIO callbacks; `Buffer()` and `Pixel(x, y)` read the panel layout. `Fonts.h` names the compiled fonts (`kFontTiny` 4x6, `kFontSmall` 5x7, `kFontText` 6x10, upstream X11 misc-fixed, public domain) and `src/Fonts.c` holds their data, copied unchanged; any other upstream font fails to link. `Screen.h`: `Screen` (`Draw(u8g2, ms, readout)`, one frame from one thread, so it may keep state) and `RigReadout` (published value by slot and ID, input and output levels, and `ViewFocus()`: a parameter the rig's view presents for selection, none by default), which each host implements over its published values. `DrawTestPattern(u8g2, ms)`: outline, title, running time, a 1-pixel checkerboard, a scrolling sine, a bouncing ball, timed by milliseconds rather than frames; `TestPatternScreen` shows it for rigs without a screen. `wf_oled_display_check`: buffer layout, fonts, the pattern's determinism and motion, and that `u8g2_SendBuffer()` reaches the byte callback with the whole buffer. Manifest `oled` passes `check.sh`. |
| Ui/Clouds | `wf::clouds_ui` (C++14, links `wf::oled` and `wf::clouds`): `OledScreen(slot)`, the main screen chosen 2026-10-18 from mockups. Top: mode (GRANULAR, STRETCH, LOOP DELAY, SPECTRAL), quality (16b ST ... 8b MO), FRZ reversed while frozen. Middle: the grain cloud, a picture of the grain settings, not the grains (the engine reports none): DENSITY as the module's overlap (none from 0.47 to 0.53, evenly placed grains swelling in turn below noon, random ones above), SIZE the grains' life and length, POSITION the cloud's place along a dotted floor with a marker, PITCH its height, TEXTURE its spread, stippled above 0.75 (where the module diffuses); the output level thins it (no grains below -48 dB, all from -24 dB, each grain keeping a fixed draw so the same ones stay), so silence shows an empty cloud; freeze stops its clock and frames it. Right: IN (after IN GAIN) and OUT meters, -48 to 0 dB in 11 segments. Bottom: MIX, SPR, FBK, REV bars. Popups for 1.5 s after a published value changes, from any source (the first frame only learns the values; a newer change replaces the popup). POSITION, SIZE, PITCH, DENSITY, TEXTURE and IN GAIN, which the screen shows no number for, take the header (style A, chosen 2026-10-18 from four mockups over a box on the cloud the user found too invasive): name left, value right (percent, ±st, ±dB; DENSITY also regular, random or no grains), the rule a 2-row bar from its origin (the middle for PITCH and DENSITY, 0 dB for IN GAIN, marked) to the value; the cloud, meters and BLEND bars stay. The BLEND values, freeze, TRIG and gate open none, as the screen already shows them. Quality or mode take the whole screen, after a change and also for as long as the view presents them (its focus: the panels' quality and mode displays, entered by holding B, changed or not): the options in `kFontText`, the current one reversed and, when the change erased the recording (quality, or mode to or from spectral), "recording erased, freeze off" in `kFontTiny`, kept with that choice's popup until no popup shows. `wf_clouds_oled_screen_check`: determinism, the FRZ tag and frame, a still frozen cloud that moves again on release, no grains at noon, position and pitch moving the cloud, no grains in silence and fewer below -24 dB, popups (none on the first frame, timing, bar halves and origin mark, the BLEND values, freeze and gate opening none, choice rows, the erased note per change, replacement, the view's focus: shown unchanged, kept while focused, a knob over it, 1.5 s after it ends, another slot or a knob ignored, no stale note from another choice), bar fills, meters and IN GAIN. Manifest `clouds_ui` passes `check.sh`. Looked at on desktop and board 2026-10-18: works. Mockups (the user's sketches, the proposals, the four popup styles) and frames drawn by the real code are in `References/CloudsOled/` (untracked). |
| Rigs | Only `SoloRig<E>`. |
| Manifests | `wf_add_manifest(<name> TARGET <t> [SOURCES] [INCLUDES] [RESOURCES])` writes `build/<preset>/manifests/<name>.txt` (sources, include dirs, defines of the target's link closure, plus files only the external build compiles and resources it ships). `TARGET` takes several targets (their link closures are merged). A header-only root contributes its include directories and defines (an interface library used to add nothing unless something else linked it). Six: `clouds` (`wf::clouds`, 14 translation units), `stratus` (`wf::stratus`), `bela_clouds` (the Bela host, `WF_BELA_HOST_TARGETS` (`wf::oled`) and the Clouds app with `wf::clouds_ui`, 46 units, `sketch.js`), `oled` (`wf::oled` and u8g2, 26 units), `clouds_ui` (41 units) and `bela_probe` (the panel probe on `wf::core`, 1 unit). |
| Tools/Bela | `check.sh` reads the manifests (configuring the `dev` preset first) and passes on every manifest with Clang 3.9 and the board's headers; `--self-test` passes. `deploy.sh [app]` sets the board clock, stages an app from its manifest (units at the top named after their repo path, `.cc` wrappers, headers by include path, resources) and builds with Bela's Makefile; it keeps the last build's CPPFLAGS on the board (`build/wf-cppflags.txt`) and rebuilds everything when they differ, since make does not track flags (a new `-D` was silently ignored by unchanged files). A manifest with no defines or include directories deploys too (empty arrays are guarded for macOS's bash 3.2 under `set -u`; `bela_probe` has no defines). Toolchain cache shared by all projects at `~/.cache/bela-toolchain` (CloudsEngine reaches it through a symlink at its old path). `profile.sh` is still CloudsEngine's and stops with a message until the profiling step. |

## Next actions

Milestone 2, the looper, comes first while only the computer is at hand
(2026-10-25); milestone 1 resumes when the hardware is: MIDI first on desktop
with the ROLI blocks, then Bela. Design in Decisions (2026-10-25); taught
step by step (Working style).

1. **Looper** (milestone 2), in steps, each listened to on desktop:
   1. `Engines/Stratus` (`wf::stratus::Engine`): parameters, the transitions,
      ramps, Thru and Loop Level, 30 s. Checks: transitions, maximum length,
      in-place byte-identical to separate buffers, determinism, silence
      before `Prepare()`. In the desktop menu as a solo rig.
      **Resume here** (written 2026-10-30): the engine without ramps is
      done, committed, and in the desktop menu as a solo rig (the user's
      `kRigs` line and link, generic `RigPanel`, OLED test pattern);
      listened in the JUCE app 2026-10-30: works. The dry (Thru) and overdub
      input ramps are now implemented and checked (2026-10-30,
      user-authorized); listen on desktop next. The earlier Thru review
      caught 11 of 13 mutants: the exact-1.0 endpoint was unchecked and
      the `fadeFrames_ == 0` guard is unreachable for supported rates.
      Both ramp checks now include 8,200 Hz (41 frames) to cover that endpoint
      case. The input ramp review initially caught 12 of 13 mutants.
      `CheckInputRampReprepare()` now covers the surviving reset mutant:
      re-Prepare mid-punch, Record immediately, a two-frame silent take,
      then four frames of live input; the buffer must remain silent.
      Confirmed that removing the reset fails this check under
      `-O3 -ffast-math`; the normal check, desktop build and Bela compile
      check pass. `AdvanceRamp()` is now non-const; both zero-frame guards
      are documented as backstops (unreachable for supported rates).
      Re-reviewed 2026-10-30: all four findings addressed, mutant G now
      caught (13 of 13), all checks and `check.sh` pass.
      Known gap until step 3: Play off mid-punch zeroes the input gain at
      once, writing a step into the loop at the stop point. The code and
      `CheckInputRampEdges` explicitly mark this temporary behavior for
      replacement: step 3 keeps the punch fading while the playhead moves
      and the loop fades out. Clear's cut is right for good (the loop is
      discarded, and a new take must not receive the old punch).
      Next exercise: the loop gain at play start and stop, with the
      tail record and the four cases below (the seam is next action 1.2). No clicks heard with the
      PO-33's percussion; a sustained sound will show them. Many overdub
      layers exceed 1.0 and will clip in Clouds later.
      Design so far (agreed 2026-10-30): the state sets targets, gains ramp
      toward them per frame. Loop gain target 1 in Playing and
      Overdubbing, else 0; overdub input target 1 in Overdubbing, else 0
      (the first take bypasses this ramp); dry follows Thru. Per frame: `loop =
      buffer[pos]` (plain read); `out = in × dry + loop × loopLevel ×
      loopGain`; `buffer[pos] = loop + in × inGain` (overdub; the first
      take writes `in`). Loop Level only changes what is heard, never the
      buffer. The first take's edges are content, not punches: the take
      writes at full from frame 0, and Recording → Playing sets the loop
      gain straight to 1 (the seam crossfade, step 2, smooths the edges).
      A fade-out keeps the playhead moving; it resets when the gain
      reaches 0. Play during a fade-out waits for it to end, then restarts
      at 0 and fades in (one read head). Clear waits for the fade-out to
      reset `length_`, never zeroes the buffer; Record after Clear starts
      at once, so the fade-out needs its own position, length and gain.
      When the loop gain reaches 0: Stopped → `pos = 0`; Playing (Play
      again mid-fade) → `pos = 0`, fade in; Empty (Clear) → `length_ = 0`;
      Recording (Clear, then Record mid-fade) → the old tail stops, the
      take is untouched. The state cannot tell these apart alone (the last
      case), hence the tail's own record. Steps, each listened to and
      checked (a constant 1.0 input; no neighbour difference above
      1/fadeFrames; 5 frames at 1 kHz; checks that assume instant changes
      wait out or test the fade): 1. the dry (Thru) ramp (implemented and
      checked; listening pending); 2. the input ramp, punch-in and out,
      the fade read back from the buffer (implemented and checked; listening
      pending); 3. the loop gain with the four cases and the tail record.
      Both ramp steps pass all 17 dev checks, the optimized `-O3 -ffast-math`
      Stratus check, the desktop build and Bela's Clang 3.9 syntax check.
      Remember for later: `PlayOn()` is not "the loop sounds" (Recording
      reads Play on with no loop yet); a minimum loop length once the seam
      crossfade exists; the first take's tail past `length_` needs room in
      `capacity_`; `CheckMaximumLength` assumes 30 s.
      Lessons so far: Clear is O(1) by an invariant (the first take
      overwrites, overdub adds, nothing reads past `length_`); `Handle()` is
      real-time too; validate before computing; Bela's 32-bit `size_t`;
      `prepared_` false first so a failed `Prepare()` never leaves pointers
      into a freed block live; the key function puts the vtable in its
      translation unit, so every virtual needs a definition (stubs) before
      the class can be used; readback state is not processing state;
      overdub reads the old loop before adding; check the state per frame
      when the engine can change it mid-block; mutation testing shows what a
      check cannot see (rewind and gain were invisible until `Process()`
      made sound).
   2. The seam crossfade, with a check that the loop wraps without a jump.
   3. The Stratus + Clouds rig (name to choose, by the naming rule;
      Nimbostratus proposed, not yet confirmed): the arena split, the chain in
      place; the in-place check added to `wf_clouds_engine_check`. Desktop
      with the PO-33: milestone 2's sound.
   4. Loop state, length and position on screen: a read-only parameter kind
      in Core (or engine telemetry), then the desktop view and an OLED
      screen, mockups first.
   5. Bela app: `sketch.js`, listen, profile.

Paused until the hardware is at hand (with the ROLI blocks, item 4 on
desktop comes first; the others need the board):

1. **Bela controls** (design agreed 2026-10-24, see Decisions), in steps:
   1. *Probe*: done (Status). Knobs read directly, not as a ratio.
   2. `Ui/Controls` (`wf::controls`), from the probe's numbers: `Knob`
      (callback mean; calibration 0.0-0.82 to 0-1, so the top (0.828) is
      reached with 1% to spare and no dead zone at the bottom, where the
      noise is 2 steps; one-pole with the firmware's time constants; touch
      at 1% of the range, 8 times the worst callback-mean noise of 0.12%)
      and `Button` (one sample per callback, a change after 4 equal ones,
      about 3 ms: 5 times the worst bounce, far below a 27 ms tap; press
      durations), with checks.
   3. `Ui/Clouds` `FrontPanel`: `ui.cc`'s B and FREEZE logic and
      `cv_scaler.cc`'s BLEND catch-up, from knob values, button states and
      time to `ControlEvent`s and what it presents; scripted checks.
   4. Bela host: a generic pin reader and the app's `AppPanel()` (null: no
      hardware), in `render()` after the GUI drain, through `rig->Handle()`;
      the panel's displays drive the OLED's focus. Listen.
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
   what its thread and I2C traffic cost the board. Seen 2026-10-18: 16
   "Underrun detected: 1 blocks dropped" within 5 s after switching granular
   to looping delay (32 frames, OLED running). Not an erase: that switch is
   Clouds' benign change (filters and pitch shifter reset, recording and
   freeze kept); whether the burst is the switch or looping delay's own
   cost is for the profiler. Decide the
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
├── Rigs/               # SoloRig<E> and named rigs (by what they are, not the board)
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
- **Engine and rig names:** proper names, not functions (functions collide:
  Clouds and Beads are both granular). MI ports keep MI's name; ours get our
  own (Stratus, the looper). The name is the folder (`Engines/Stratus`), the
  namespace (`wf::stratus`), the target and the manifest; the comment says
  what it does. Rigs likewise, named by what they are, never by the board,
  since apps bind a rig to a board.
- **Engine classes:** an engine's `wf::Engine` implementation is always
  `wf::<name>::Engine` (the namespace names the engine, the class its role). A Mutable Instruments port also has a
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
| 2026-10-18 | Views report what they present for selection (`RigReadout::ViewFocus()`), so the OLED shows the quality or mode list as soon as the panel's LEDs do, changed or not (the user's request after trying the popups). Only those two displays for now; the BLEND selection could use it later. Desktop: plain state on the message thread in `AudioHost`. Bela: `wf-focus`, repeated by the page and lapsing on the board, since the board cannot tell when a page goes away; owned by the page that last selected, so another page's repeats or timeout leave it alone. |
| 2026-10-25 | Milestone 2 (looper into Clouds) follows milestone 1, before more MI ports: it is the first multi-engine rig, it is playable with the PO-33 alone, and Rings and Plaits belong on the Seed3 and need notes. Multi-engine rigs keep the existing contracts. **Memory**: one arena per engine; the rig splits the host's block (`AlignMemory` per part) and adds memory regions only when a target needs them (Daisy's SRAM for hot buffers, after profiling there); the split stays in the first rig and moves to Core on the second use. **Routing**: a serial chain runs through the output buffer in place (looper `in` → `out`, Clouds `out` → `out`), with no scratch; scratch between engines, when a rig needs it (parallel paths, the mixer), comes from the rig's own arena, sized to `maxFrames`, never from rig members or the host. **In-place**: `Engine::Process()` keeps allowing each output to be its input (left with left, right with right), so every engine must read `in[i]` before writing `out[i]`, read both channels before writing either when it mixes them, and never read `in` again after writing `out`; every engine has a check that in-place and separate buffers give byte-identical output (Clouds has it only in `BlockAdapterCheck`; `wf_clouds_engine_check` gets one, the looper has one from its first step). A feedback loop between engines (a one-block delay) would depend on the host's block size; give it a fixed internal block when it comes (milestone 5). |
| 2026-10-25 | Looper design (milestone 2). Stratus, `wf::stratus::Engine`: a plain engine at the host's rate (no `Module`, no adapters), depending only on Core. Parameters, as in the header (permanent once first committed): 0 Play (Toggle: on plays from the start, off stops), 1 Record (Toggle: the first take when empty, else overdub), 2 Clear (Trigger), 3 Loop Level (dB, default 0 dB), 4 Thru (Toggle, default on). The engine sets Record and Play itself, read back by `ParamValue()`: empty, Record on starts the first take and Play on is refused; in the first take, Record off or the maximum length fixes the length and plays from 0, Play off ends the take stopped; playing, Record turns overdub on and off, Play off stops and turns Record off; stopped, Play on plays from 0 and Record on plays from 0 overdubbing; Clear empties from anywhere. Output = input × Thru + loop × Loop Level; recording takes the input before Thru; overdub adds at 1.0 (feedback is milestone 5). Stereo float buffer, 30 s while testing, 60 s the intended length; a rate change re-prepares and clears it. About 5 ms ramps on punch-in and out and on play start and stop; the seam crossfades a tail recorded past the first take's end into the head. Free length until Transport (milestone 4); events land at callback starts, so use 64-128-frame buffers on desktop meanwhile. One-button behaviour belongs to a front panel or `MidiMap`, not the engine. Out of milestone 2: undo, reverse, half speed, feedback, several loops. |
| 2026-10-26 | Engines and rigs take proper names, not functions (Conventions): the looper is Stratus in code too. Style A kept: `wf::<name>::Engine`. Rigs never take a board's name (the handover's `BelaRig`/`SeedRig` go), so a rig can move between boards. |
| 2026-10-24 | Bela hardware controls follow Clouds' front panel: knobs A0-A6 (POSITION, SIZE, PITCH, DENSITY, TEXTURE, BLEND on the selected parameter, IN GAIN), A7 spare; FREEZE (a press toggles), B (press: show or select the BLEND parameter; 1 s: quality; 4 s: mode; presses there select the next) and TRIG (press: trigger, held: gate; our addition, the module has a jack). Feedback is the OLED; no LEDs yet. Everything on 3.3 V (the digital pins are not 5 V tolerant, and the 5 V pins depend on the power source). Unlike the firmware, which feeds the ADC straight to the DSP, a knob sends nothing until moved past a threshold and goes quiet at rest, since every published change opens an OLED popup and reaches the page; the last control moved wins (absolute, as the module's pots); at boot the knobs are sent once, so the hardware is the truth. BLEND keeps `CvScaler::UpdateBlendParameters()`'s skewed catch-up. Conditioning and the front-panel logic live below Platforms (`Ui/Controls`, `Ui/Clouds`), checked on desktop; the host only reads pins and runs them in `render()` (the audio thread is the one mutator, so no queue). Thresholds come from the probe, not from assumptions. |

## Open questions

- Core contracts deferred to the milestone that needs them (2026-10-18 Core
  review); design each before building that milestone:
  - Looper + Clouds (2): decided 2026-10-25 (Decisions); the first
    multi-engine rig checks those rules in code.
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
- OLED burn-in care, a nice-to-have (low risk for sessions of a few hours;
  matters for long runs): in the host, for every rig, dim the panel
  (`u8g2_SetContrast`) after about a minute without changes and blank it
  after about ten, waking on any change; perhaps a lower brightness
  overall. Pixel shift is unlikely: the layout uses the full width.
- Clouds' OLED screen, later: a picture per mode (stretch, looping delay and
  spectral read the knobs differently; the cloud is the granular picture in
  every mode), and real grain data from the engine (a per-block snapshot of
  the grains and the buffer's loudness, through a MutableDSP patch).
- Clouds deferred work: band-limited resampling, make-up gain (fully dry is
  about −9 dB), stretch and spectral cost on Bela, the sanitizer findings in
  MutableDSP `PATCHES.md`.
