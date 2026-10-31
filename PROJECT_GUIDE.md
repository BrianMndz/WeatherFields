# WeatherFields Project Guide

Last updated: 2026-10-31.

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
| Core | Header-only `wf::core`, all compiling for the board with Clang 3.9: `wf/Dsp.h` (`IsFinite`, checked as built and under `-O3 -ffast-math`; `ApplyGainRamp`, JUCE's `applyGainRamp` semantics; `PeakFollower`, the Clouds module's meter, 10 ms attack and 250 ms release, NaN-safe); `wf/Params.h` (`ParamId`, `ParamKind`, `ParamSpec` with static choice labels, `ParamTable`, `ConstrainValue`, which rejects non-finite values, and `IsValidParamTable`, which also requires a label per Choice index; `ParamKind::Readout` (2026-10-31): read-only, reported by `ParamValue()` within its range, never set: `ConstrainValue` refuses it, so the rig drops its events, and hosts publish it but never send, replay or store it); `wf/Events.h` (8-byte POD `ControlEvent`); `wf/SpscQueue.h` (lock-free, power-of-two capacity, trivially copyable items, `PopAvailable()` for the consumer; its own header because the recorder will use it too). `wf/ControlOutbox.h` (`ControlOutbox<N>`: a producer that must not lose events, the GUI's on both hosts; events wait in order when the input is full, only Continuous values coalesce; checked with both review cases and a mutation that restores the old bug). `wf/ControlInput.h` (`ControlInput<N>`: one producer's queue; `Drain(rig, max)` takes at most `max` and never more than was queued when it began, so a refilling producer cannot stretch the callback; an overload calls `onTaken(event)` for each event taken, dropped ones too (2026-10-31, for the desktop host's pending state); `ControlOutbox::Send()` returns whether it replaced a waiting Continuous value; overflow and dropped counters; `Discard()` for rig swaps). The queue's two-thread check also runs clean under ThreadSanitizer, which flags a copy with relaxed ordering. `wf/Engine.h` (`wf::Engine`: `Name`, `Params`, `MemoryBytes`, `Prepare`, `Handle`, `ParamValue` (audio-thread readback, including changes the engine makes itself), `Process`; silent and deaf until a successful `Prepare()`; input buffers never null; `MemoryBlock`, `PrepareStatus`, `CheckMemory`, 16-byte `kMemoryAlignment`); `wf/Rig.h` (`ConstrainEvent`, `wf::Rig` with a const `Slot()` and a private `MutableSlot()` that only its non-virtual `Handle()` uses, so every change is validated; the rig-swap procedure; `SoloRig<E>`), checked with a gain engine (readback, lifecycle) and compiled for the board with `-fno-exceptions -fno-rtti`. |
| Engines/Clouds | `wf::clouds`: CloudsEngine's `Engine/` from `e7738d9`, now in namespace `wf::clouds` with its `Engine` class renamed `Module` (`Module.h`, `Module.cpp`; macro `WF_CLOUDS_NODISCARD`; MI code written `::clouds::`). Otherwise unchanged: the 16 render hashes still match and `check.sh` passes. Its five checks pass in Debug and Release; their only other change is `std::byte` → `unsigned char` for the test arenas, as checks compile as C++14. `wf::clouds::Engine` implements `wf::Engine` around it: 15 parameters (IDs 0-8 the knobs in module order, 9 IN GAIN, 10 Freeze, 11 Trigger, 12 Gate, 13 Quality, 14 Mode) with JUCE's ranges and defaults, applied from the table in `Prepare()`; labels for quality ("Stereo 16-bit" ...) and mode ("Granular", "Stretch", "Looping Delay", "Spectral"); `ParamValue()` reads quality and mode from the Module; one 184,192-byte arena for both Module arenas; 32/44.1/48 kHz through `SampleRateAdapter` with `ServicePolicy::AfterEachBlock`; quality and mode changes in event order with `Service()` at once, releasing freeze when they erase; a second `Prepare()` rebuilds the Module in place. `wf_clouds_engine_check`: all 16 quality × mode renders through the rig are byte-identical to the Module's own, 32 frames later (the seven that use randomness fail with another seed), plus defaults, clamping at the rig, the freeze rules, readback (a quality change reads back with freeze released) and IN GAIN at 48 kHz, and in place (2026-10-31): every mode at 48 kHz in uneven callbacks up to 64 frames, freeze and pitch changing midway, bit for bit equal to separate buffers. |
| Engines/Stratus | Stratus, the looper (`wf::stratus::Engine`), in the desktop menu as a solo rig with its own panel (`StratusPanel`, Desktop host) and OLED screen (`Ui/Stratus`). Engine without ramps listened 2026-10-30: works; 1.1 ramps and 1.2 seam crossfade implemented 2026-10-30 (user-authorized; pre-roll since the review), listened on desktop 2026-10-31: sounds great. Target `wf::stratus`, manifest `stratus`, C++14 on `wf::core`. Parameters: 0 Play, 1 Record, 2 Clear, 3 Loop Level (-60 to 0 dB, -60 means silence), 4 Thru (default on); Play/Record readback follows logical state immediately. Readouts (2026-10-31), in seconds, 0 to 30: 5 Position, the main playhead (while the first take records, the time recorded; a stopped loop keeps it until Play) and 6 Length, 0 until the first take ends; clamped to 30 s, since a full take is `ceil(rate × 30)` frames (30.000002 s at 1 kHz). A UI tells the first take by Record on with Length 0. `MemoryBytes()`: aligned stereo float loop for the full 30 s, plus four aligned stereo fade buffers of `ceil(rate × 0.005)` frames (overwrite scratch, input ring, two pre-roll slots; 7,680 extra bytes at 48 kHz); 0 for unsupported rates. `Prepare()`: positive rates up to 192 kHz, validation before carving, the loop never cleared (the ring and pre-roll slots are); resets all gains, tail metadata and state. Silent/deaf until prepared, including after failure. Thru, overdub input and loop gain ramp per frame in 5 ms (rounded up); integer positions and `AdvanceRamp()` give exact endpoints. Loop Level affects output only (`loopLevelGain_`). First takes write full input and start playback at full gain. The seam blends the loop's last `min(ceil(rate × 0.005), length - 1)` frames into the pre-roll (the input before Record, copied from an always-on ring), ending exactly on frame 0's predecessor; frame 0 always plays as recorded; none for a one-frame loop. Overdub reads the blended loop before adding input × inputGain to the loop and, in the seam, to the matching pre-roll sample, preserving unity overdub gain through the seam; Record off writes the fade-out while Playing. Stop moves playback to `Tail` (position, copied length, fade position), keeps the punch fading on its moving head, and finishes at zero. Play/Record during Stop wait for zero, then restart from frame zero and fade in. Clear marks the tail discarded and permanently cancels its input writes; the main length resets immediately while the tail uses its copied length; Empty rejects Play. Record after Clear starts immediately; its position/length survive tail completion. A take finishing early waits for the tail, then plays at full gain; it uses the other pre-roll slot, so the tail keeps its own. Only one playback head sounds at a time. Tail scratch preserves old samples incrementally before a new take overwrites them, including repeated Clear and short-loop wrapping; at most one stereo sample saved per frame, Clear remains O(1). Read both inputs before either output (in-place). Checks: transitions/duplicates, lifecycle, maximum 30,000 frames at 1 kHz, first-take/overdub content, Thru/Loop Level, six rates (100 Hz, 1/8.2/44.1/48/192 kHz), all four zero-gain cases, partial fades, pending overdub, repeated Clear, short finished/stopped new takes, tail overwrite preservation, re-Prepare mid-punch/tail, byte-identical in-place output including Clear/new takes, seam envelopes and repeated wraps at all six rates with the downbeat exact, short/one-frame loops, a Stop tail's punch inside the seam, pre-roll slots during Clear then Record, the ring after Prepare, and the readouts (`CheckReadouts` at 1 kHz: first take, end, playback and wrap, overdub, Stop, Clear, a new take, re-Prepare, events dropped; Position and Length at capacity in `CheckMaximumLength`; four mutants caught, a control survives). All 17 dev checks, desktop build, Bela Clang 3.9 syntax check, optimized `-O3 -ffast-math` and ASan/UBSan Stratus checks pass. Six targeted tail mutants caught; two review mutants caught by permanent checks for tail Loop Level and Clear after an unheard take finishes Playing. Seam: 20 of 21 mutants caught (one equivalent). |
| Tools/Render | `wf_render_clouds`: CloudsEngine's CLI (`e7738d9`), only its include and name changed. CTest `wf_render_clouds_references` renders all 16 quality × mode combinations and compares them with `clouds_references.sha256`, taken from CloudsEngine's `clouds_cli` built from `e7738d9`: byte-identical in Debug and Release, and equal to CloudsEngine's recorded table. The references hold for macOS arm64 with Apple Clang. |
| Desktop host | `wf_desktop` (`WeatherFields.app`, `desktop` preset): rig menu (Clouds' and Stratus' `SoloRig`s), audio settings saved between runs, status line (rate, block, CPU, a rig's failed `Prepare()`, GUI overflow and dropped counts), and the rig's view (`RigView`): its own if `RigEntry::makeView` gives one, else `RigPanel` generated from `ParamSpec` (knobs, label menus, latching toggles, trigger buttons). `RigPanel` shows a Readout as text with its unit; restarts replay no Readouts. Pending GUI values (review finding 2026-10-31, confirmed with the reviewer's harness): `LatestValue()` used to see only events still waiting in the outbox, so once an event was in the audio queue a second click built on the stale published value (THRU on, two clicks before a callback: off, off; the same for Clouds' quality and mode presses, and REC, PLAY or FREEZE could be reset by a panel sync in that gap). Now the host counts per parameter the GUI events sent (a coalesced value replaces one already counted) and those the audio thread took, released after `Publish()`; until they match, `IsPending()` is true and `LatestValue()` gives the last value sent. Checked with the reviewer's harness (fixed: two quick clicks leave THRU on, as sent; the old binary leaves it off) and the scratch harness. Stratus' view is `Stratus/StratusPanel` (2026-10-31, proposal C of four mockups drawn with the real panel code, sheets in `References/StratusPanel/`): Clouds' look and size (416 × 440, same scaling), REC (lit red) and PLAY (lit green) toggles following the published state, CLEAR, THRU with its LED, a large LOOP LEVEL knob, and 16 Clouds LEDs for the playhead: with no loop and Record on, they fill red over 30 s (the unlit ones are the time left); while a loop plays, one light crossfades between the two LEDs nearest the head, wrapping, green or amber while overdubbing; dark when stopped or empty. Under the row: 0 and the take's limit (30 s), or the loop's length once there is one (the mockup said "loop"). Checked in a scratch harness (the real `AudioHost` on a fake device, snapshots of both panels in five states, no Readout replayed on restart, a Readout from the GUI dropped by the rig). Not yet looked at live. The panels' shared look lives in `Panel/` (2026-10-31, at its second use; namespace `panel`, was `clouds_panel` in `Clouds/`): `PanelControls` (colours, `PanelLookAndFeel`, `PanelKnob`, `LitButton` with a colour and core (FREEZE, REC, PLAY), `PanelPushButton`, `PanelLed` with tooltips, and `PanelFont`, `DrawLabel`, `DrawPill`, `DrawScrewHole`, `DrawOrnament`) and `PanelView`, a `RigView` base holding the plate (reference size, scaling 0.8-2.5 with its aspect, centring, shaded plate, corner screws, `HeightFor()`, `Scaled()`, `panel::Circle`); a panel implements `PaintPanel()` and `LayOut()`. Both panels pixel-identical through the move (30 snapshots, five window sizes, before and after). One `juce::TooltipWindow` per main window (in `MainComponent`; each panel had its own, which would show every tip twice with two panels). `Stratocumulus/StratocumulusView`: `CloudsPanel` (slot 0) and `StratusPanel` (slot 1) in signal order, side by side when both fit at their smallest scale (`PanelView::MinimumWidth()`), else stacked and scrolling (the default 640-pixel window); snapshots of both through the real host after recording a loop. Clouds' view is `Clouds/CloudsPanel`, CloudsEngine's front panel (`e7738d9`) ported to the host: same drawing, B/C/TRIG behaviour, LED modes and mode tooltips, but it sends events and shows published values, so engine-made changes appear; its meter is the host's input level times IN GAIN, or the output level while frozen. Tested 2026-10-18: behaves as in CloudsEngine. `AudioHost` runs the rig in JUCE's callback: GUI `ControlInput` drained (at most 32 per callback), then `ParamValue()` published to atomics, then `Process()` from scratch buffers, with `PeakFollower`s on the rig's input and output in pieces of at most the prepared block; silent input when there is none, mono input to both sides. The rig is prepared in `audioDeviceAboutToStart()`; a device restart replays the published values as events, Choices first. While a `Prepare()` has failed, the callback is silent and neither drains nor publishes, so queued events wait and the last valid values survive. Rig swaps detach the callback, discard queued events, destroy the old rig before making the new one. When the GUI's input is full, its events wait in order; only a Continuous parameter's newer value replaces its waiting one (moving to the back), while Choice, Toggle and Trigger events are all kept, since they can act on transitions; the panel shows only published values. Builds clean with `-Wall -Wextra`. Generic panel listened 2026-10-18: works. "OLED..." opens the emulated screen (`OledView`, in its own window): a panel-less `wf::oled::Display` drawing the current rig's screen (`RigEntry::makeScreen`; Clouds' `OledScreen`, else the test pattern) from the host's published values and levels at the board's 25 frames a second, shown at the largest integer scale with nearest-neighbour scaling; lit pixels blue as on the board's panel, a click cycles blue, white, yellow. Test pattern looked at 2026-10-18: opens and animates. Clouds' screen looked at 2026-10-18: works, follows the panel. The panel's quality and mode displays set the host's view focus (`AudioHost::SetViewFocus()`, message thread, cleared by a rig swap and the panel's destructor), which the OLED's readout passes to the screen. |
| Bela host | `Platforms/Bela/Host`: `BelaHost.cpp` (Bela's `setup()`/`render()`/`cleanup()` around the app's `wf::bela::AppRig()` and `AppScreen()` (its OLED screen, or null for the test pattern), chosen at link time; 32-frame default period; rig memory allocated in `setup()`; failure refuses to start; `render()` drains the panel's `ControlInput` (at most 32 events), publishes `ParamValue()`s, processes Bela's buffers directly, meters input and output) and `GuiLink` (the browser panel over Bela's Gui library, generic by slot and ID: `wf-hello`/`wf-state`, `wf-param`, `wf-trigger`, `wf-focus` (what the page's display presents, into `Published::focus` with its time; the page repeats it every second while it lasts and the OLED ignores one older than 2.5 s, so a closed page leaves none; the focus has an owner, a random id per page: a new selection takes it, a repeat only refreshes its owner's or takes a lapsed one, only the owner clears it, so two open pages do not fight (review finding, confirmed with the reviewer's harness, adapted for page ids, and the real `sketch.js` in Node)), and `wf-update` about 30 times a second with levels and changed values; the WebSocket thread and the sender thread share one `ControlOutbox` under a mutex; only the sender thread sends; it is never destroyed, and `Stop()` closes it under the mutex, since Bela's Gui cannot stop its WebSocket server synchronously). App `bela_clouds` (`Apps/Clouds`: `CloudsApp.cpp`, and `sketch.js`, CloudsEngine's panel ported onto `wf-*` messages, with the playback mode display added; it keeps the board's latest report per control and shows it once the control is not held (500 ms after a local edit, or while dragging), and re-sends TRIG's gate on every connection). Built and run on the board 2026-10-18: starts at 44.1 kHz, 32 frames, audio thread MSW 1; the protocol checked end to end over a tunnel (state, updates, clamping at the rig, freeze released by a quality change, invalid messages ignored). Not yet listened to through the browser panel. The Bela review fixes (below) are checked locally only and not yet deployed. The OLED (`OledLink`, `I2cPanel`): every app drives it from `setup()` to `cleanup()` on an ordinary Linux thread (never `render()`), drawing the app's screen from the published values and levels (`bela_clouds`: Clouds' `OledScreen`) and sending it whole at up to `WF_OLED_FPS` (default 25) on I2C bus `WF_OLED_I2C_BUS` (1) at `WF_OLED_I2C_ADDRESS` (0x3C), all overridable with deploy's `-D`; it prints fps and draw/send times (mean, max) every 5 s. `I2cPanel` is our u8x8 glue: one `write()` per u8x8 transfer, state per display through u8x8's user pointer (defined on Linux), errors kept for the frame to check. A bus that cannot be opened, or a panel that does not answer, is printed once and never stops audio; the thread retries once a second and re-initialises the panel; `cleanup()` blanks it and joins. Run on the board 2026-10-18 with Clouds: the panel is at 0x3C on bus 1 (`i2cdetect`), the pattern shows as on the desktop; at a 30 fps cap it sent back to back at 27.2-28.2 fps, send 35.1-36.8 ms mean (40-51 ms max), draw 0.34-0.41 ms mean (up to 2.1 ms). About 1.16 KB a frame (1 KB of data in 24-byte transfers, plus addresses and commands), so the bus runs at 400 kHz with about 9 ms of per-transfer overhead. At the 25 fps default: a steady 24.9-25.0 fps, send 35.6-35.8 ms mean (up to 45 ms after start-up), draw about 0.33 ms mean. The outline reaches all four edges of the panel (SH1106 driver and column offset right), and Clouds sounds the same with the OLED running; the CPU cost with and without it is not yet measured. Clouds' screen run on the board 2026-10-18: follows the browser panel as the desktop one follows its panel. With nothing playing the cloud showed grains, which read as activity; it now thins with the output level, checked on both hosts 2026-10-18. |
| Bela controls | Wired 2026-10-24 (USB power): six 10k linear pots (Bojack, marked P103) on A0-A5 (POSITION, SIZE, PITCH, DENSITY, TEXTURE, BLEND), wiper to the input, legs on 3.3 V and GND; tactile buttons FREEZE on D0 and B on D1, from 3.3 V with a 10k pull-down (high when pressed). IN GAIN (A6) and TRIG (D2) not wired yet. Probed 2026-10-24 on USB power: every pot reads 0.00000 to 0.8275-0.8285 (3.39 V against the ADC's 4.096 V; an earlier 0.848 was likely the start-up reading, as the first callbacks read full scale on every wired input). Noise is proportional to the reading: at rest, raw peak-to-peak about 2 steps (of 1/65536) at the bottom, 100-130 at 0.4, 200-300 at the top; the callback mean's 0, 10-25 and 20-65 (at most 0.12% of the knob's range); resting values drift by about 20 steps, and all channels quieten together in some windows. A second run with the 3.3 V rail wired to A7 ruled out rail ripple: A7 itself (no pot) is as noisy as a pot at the top, and the ratio to it is slightly noisier than the direct reading (A3 at the top: 200-260 / 16-25 direct, 270-350 / 25-60 as a ratio in the same units) and drifts no less; so the noise is broadband (the ADC's reference, or rail noise too fast to share between inputs converted microseconds apart), for averaging and filtering, not division. The rail itself is stable (A7 0.82760-0.82840 over 90 s) and every pot's top matches it. With A7 driven, the floating A6 reads about 0.19 (charge from the input converted before it). No crosstalk between wired inputs (moving one leaves the others' noise unchanged; the floating A6 and A7 follow anything). Buttons: mostly one edge per press at the 22.7 us digital rate; two presses in the second run bounced (3 edges over 0.54 and 0.36 ms); presses 91-170 ms, the quickest tap 27 ms. `Platforms/Bela/Probe/PanelProbe.cpp` (manifest `bela_probe`): a standalone Bela program (no rig, no host) printing every 2 s, per analog input, the mean, the peak-to-peak noise in ADC steps (raw, and of each callback's mean over its analog frames) and the range since start; per button, presses, releases, glitches (bursts ending where they began), bounce (edges less than 10 ms apart: count and span; 30 ms merged a quick tap) and the shortest press. It skips the first 100 ms, and with the 3.3 V rail wired to A7 also prints each input's ratio to it (per sample, and per callback as the sums' ratio). `render()` hands each window to a printing thread through `wf::SpscQueue`. Passes `check.sh`; both versions run on the board 2026-10-24. The A7 jumper goes again: A7 stays spare. |
| Ui/Oled | `wf::oled` (C++14): `Display`, a 128x64 SH1106 in u8g2 full-buffer mode with its own 1 KB buffer; the default constructor has no panel (drawing only), the other takes the byte and GPIO callbacks; `Buffer()` and `Pixel(x, y)` read the panel layout. `Fonts.h` names the compiled fonts (`kFontTiny` 4x6, `kFontSmall` 5x7, `kFontText` 6x10, upstream X11 misc-fixed, public domain) and `src/Fonts.c` holds their data, copied unchanged; any other upstream font fails to link. `Screen.h`: `Screen` (`Draw(u8g2, ms, readout)`, one frame from one thread, so it may keep state) and `RigReadout` (published value by slot and ID, input and output levels, and `ViewFocus()`: a parameter the rig's view presents for selection, none by default), which each host implements over its published values. `Widgets.h` (2026-10-31, from Clouds' screen at their second use): `DrawReversed` (a tag with its text cut out) and `DrawMeter` (Clouds' -48 to 0 dB meter, any top and height); Clouds' frames are identical through them (a hash over 200 frames of levels, freeze, popups and choices, before and after). `DrawTestPattern(u8g2, ms)`: outline, title, running time, a 1-pixel checkerboard, a scrolling sine, a bouncing ball, timed by milliseconds rather than frames; `TestPatternScreen` shows it for rigs without a screen. `wf_oled_display_check`: buffer layout, fonts, the pattern's determinism and motion, and that `u8g2_SendBuffer()` reaches the byte callback with the whole buffer. Manifest `oled` passes `check.sh`. |
| Ui/Clouds | `wf::clouds_ui` (C++14, links `wf::oled` and `wf::clouds`): `OledScreen(slot)`, the main screen chosen 2026-10-18 from mockups. Top: mode (GRANULAR, STRETCH, LOOP DELAY, SPECTRAL), quality (16b ST ... 8b MO), FRZ reversed while frozen. Middle: the grain cloud, a picture of the grain settings, not the grains (the engine reports none): DENSITY as the module's overlap (none from 0.47 to 0.53, evenly placed grains swelling in turn below noon, random ones above), SIZE the grains' life and length, POSITION the cloud's place along a dotted floor with a marker, PITCH its height, TEXTURE its spread, stippled above 0.75 (where the module diffuses); the output level thins it (no grains below -48 dB, all from -24 dB, each grain keeping a fixed draw so the same ones stay), so silence shows an empty cloud; freeze stops its clock and frames it. Right: IN (after IN GAIN) and OUT meters, -48 to 0 dB in 11 segments. Bottom: MIX, SPR, FBK, REV bars. Popups for 1.5 s after a published value changes, from any source (the first frame only learns the values; a newer change replaces the popup). POSITION, SIZE, PITCH, DENSITY, TEXTURE and IN GAIN, which the screen shows no number for, take the header (style A, chosen 2026-10-18 from four mockups over a box on the cloud the user found too invasive): name left, value right (percent, ±st, ±dB; DENSITY also regular, random or no grains), the rule a 2-row bar from its origin (the middle for PITCH and DENSITY, 0 dB for IN GAIN, marked) to the value; the cloud, meters and BLEND bars stay. The BLEND values, freeze, TRIG and gate open none, as the screen already shows them. Quality or mode take the whole screen, after a change and also for as long as the view presents them (its focus: the panels' quality and mode displays, entered by holding B, changed or not): the options in `kFontText`, the current one reversed and, when the change erased the recording (quality, or mode to or from spectral), "recording erased, freeze off" in `kFontTiny`, kept with that choice's popup until no popup shows. `wf_clouds_oled_screen_check`: determinism, the FRZ tag and frame, a still frozen cloud that moves again on release, no grains at noon, position and pitch moving the cloud, no grains in silence and fewer below -24 dB, popups (none on the first frame, timing, bar halves and origin mark, the BLEND values, freeze and gate opening none, choice rows, the erased note per change, replacement, the view's focus: shown unchanged, kept while focused, a knob over it, 1.5 s after it ends, another slot or a knob ignored, no stale note from another choice), bar fills, meters and IN GAIN. Manifest `clouds_ui` passes `check.sh`. Looked at on desktop and board 2026-10-18: works. Mockups (the user's sketches, the proposals, the four popup styles) and frames drawn by the real code are in `References/CloudsOled/` (untracked). |
| Ui/Stratus | `wf::stratus_ui` (C++14, links `wf::oled` and `wf::stratus`): `OledScreen(slot)`, proposal D of the first batch of mockups (2026-10-31; a Clouds-aligned batch of a layered cloud over Clouds' floor was drawn too, the user kept D). Top: the state (EMPTY, RECORDING, PLAYING, OVERDUB, STOPPED; Length 0 with Record on is the first take), reversed while recording or overdubbing; on the right "17.6s left" during the first take, else "position/length" ("2.3/6.0s"). Middle: 16 cells, the desktop panel's LED row: during the first take they fill from the bottom over 30 s (the empty ones are the time left); with a loop, the cell under the playhead stands 2 pixels taller above and below while playing or overdubbing, the cells passed this lap stippled while overdubbing, plain frames when stopped; dots when empty; under them 0 and 30 s or the loop's length. Right: IN and OUT meters (the rig's input and output). Bottom: LVL (Loop Level over its range, empty at silence) and THRU, reversed while on, plain "thru" when off. No popups and no animation (`ms` unused): everything shows all the time. `wf_stratus_oled_screen_check`: determinism and no motion with time, each state's header, cells and playhead (fill from the bottom, the downbeat, a position at the length on the last cell), stipple, LVL at 0, -30 and -60 dB, THRU, meters; nine mutants caught, a control survives. Manifest `stratus_ui` passes `check.sh`. Desktop: Stratus' `RigEntry::makeScreen`. Not yet looked at live. Mockups and the real frames beside D: `References/StratusOled/`. |
| Rigs | `SoloRig<E>`, and `Rigs/Stratocumulus` (2026-10-31, written by Claude, authorized): `wf::stratocumulus::Rig`, Clouds (slot 0) into Stratus (slot 1), resampling: Stratus records, overdubs and plays over Clouds' output; its Thru passes Clouds on, off leaves only the loop. The chain runs in place through the output buffer (Clouds in → out, Stratus out → out), no scratch. `MemoryBytes()`: Clouds' part then Stratus', each `AlignMemory`ed (11.7 MB at 48 kHz); `Prepare()` checks the whole block before splitting (a block smaller than Clouds' part would otherwise wrap Stratus' size), prepares Clouds then Stratus and returns the first failure; Clouds' rates (32, 44.1, 48 kHz) are the rig's. Silent and deaf after a failed `Prepare()` by the rig's own flag (`MutableSlot()` null, `Process()` zeros), as an engine may keep an earlier preparation (Stratus did, with a loop playing). `wf_stratocumulus_rig_check`: slots, memory sum and refusals (short, tiny, misaligned), failed `Prepare()` with a loop playing, routing (slot 2 and readouts dropped, NaN constrained), and a 2 s timeline (Clouds wetter, a loop of its output, freeze, overdub, Loop Level, Thru off) bit for bit equal to the engines run by hand with separate buffers, in place or not, the loop audible and different from Clouds alone. Mutation: 9 mutants plus a control; 8 caught, one equivalent (Stratus' failure is unreachable once Clouds accepted the rate and the block is whole; kept as a guard); the failed-Prepare check was strengthened after `silence_off` survived. Manifest `stratocumulus` passes `check.sh`. Desktop: in the menu with `StratocumulusView` and Stratus' OLED screen (slot 1). Not yet listened to. Host metering is the rig's input and output only, so Clouds' frozen meter shows the rig output (with the loop) and the Stratus screen's IN meter shows the rig input, not Clouds' output. |
| Manifests | `wf_add_manifest(<name> TARGET <t> [SOURCES] [INCLUDES] [RESOURCES])` writes `build/<preset>/manifests/<name>.txt` (sources, include dirs, defines of the target's link closure, plus files only the external build compiles and resources it ships). `TARGET` takes several targets (their link closures are merged). A header-only root contributes its include directories and defines (an interface library used to add nothing unless something else linked it). Eight: `clouds` (`wf::clouds`, 14 translation units), `stratus` (`wf::stratus`), `stratus_ui` (`wf::stratus_ui`), `stratocumulus` (the rig with both engines), `bela_clouds` (the Bela host, `WF_BELA_HOST_TARGETS` (`wf::oled`) and the Clouds app with `wf::clouds_ui`, 46 units, `sketch.js`), `oled` (`wf::oled` and u8g2, 26 units), `clouds_ui` (41 units, with `Widgets.cpp`) and `bela_probe` (the panel probe on `wf::core`, 1 unit). |
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
      **Done** (2026-10-31): 1.1 ramps and 1.2 seam implemented, checked
      and listened on desktop by the user: sounds great; committed as
      `7f02838`. Small cleanup: six lines in `EngineCheck.cpp` exceed 100
      columns (101-107). The engine before ramps was listened to
      with the PO-33 on 2026-10-30: works; percussion hid the boundary clicks.
      Many overdub layers can exceed 1.0 and will clip in Clouds later.
      The gains move once per frame, before use, by 1/fadeFrames. Thru
      follows its toggle; overdub input targets 1 only during active
      Overdubbing, 0 while a tail drains. First takes bypass the input ramp.
      Loop Level affects output, never stored audio. Finishing a first take
      sets loop gain straight to 1, and the downbeat always plays as
      recorded: the seam blends the loop's end into a pre-roll (1.2 below).
      Stop moves playback into the tail record; both output and pending
      overdub input fade while its playhead moves. Play or Record during
      Stop waits for zero, then restarts from the downbeat and fades in.
      Clear keeps the old audio tail but permanently cancels its input
      writes, since the loop is discarded. The four zero-gain cases:
      - Stopped after Stop: position resets to 0, silence until Play.
      - Playing (or pending Overdubbing): reset to 0, then fade in.
      - Empty after Clear: stays Empty; length is already 0, Play refused.
      - New take after Clear: old tail ends without touching its position,
        length or state, including a take already finished or stopped.
      A new take records immediately during Clear's tail. If it finishes
      before the tail, it waits for that one playback head, then plays at
      full gain. The tail has its own position, copied length, gain and a
      discarded flag distinguishing its ownership from a new take's.
      Shared-buffer detail: a short old loop can wrap into a prefix the
      new take has overwritten. Host-owned scratch stores the original
      stereo samples just before each overwrite, bounded by fadeFrames;
      savedFrames marks the valid prefix. This costs 1,920 extra bytes at
      48 kHz, no buffer clearing, and at most one stereo save per frame.
      Clear remains O(1), including repeated Clear/new-take sequences.
      Validation: all 17 dev checks; desktop build; Bela Clang 3.9 syntax;
      optimized -O3 -ffast-math and ASan/UBSan Stratus runs. Six targeted
      mutants caught: omitted Stop tail, cut input punch, ignored waiting
      tail, rewound new take, overwritten tail samples, stale tail after
      Prepare. The earlier Prepare/input-punch reset regression is retained.
      Review (25 mutants plus a control, 2026-10-30) identified two test
      gaps now covered permanently: Stop at -6.02 dB must preserve Loop
      Level through its tail (including later level changes), and
      Clear → Record → Record off → Clear must keep the original tail
      when the new take finishes Playing but has never been heard.
      Both mutants fail their respective checks under -O3 -ffast-math;
      the unmodified code passes. The other nine survivors were equivalent.
      Simplified the redundant state: Clear now resets `length_` at once,
      since the tail owns its copied length. This supersedes the earlier
      deferred-length-reset design. Tail completion no longer has an Empty
      branch or redundant loop/input gain resets; Play/Record from Stopped
      always reset the main position (the tail playhead is independent).
      Kept the discarded-tail punch guard, tail record reset, and save bound
      against the tail length to document intent or avoid unnecessary work.
      Decided 2026-10-30 (the user): Play from Stopped keeps its 5 ms
      fade-in at the downbeat; only Recording → Playing starts at full.
      Re-reviewed 2026-10-30: both gap mutants now caught, all checks and
      `check.sh` pass. Removed the redundant tail-end position reset;
      Play and Record from Stopped reset the main position.
      Remember: `PlayOn()` describes panel state, not whether a loop sounds.
      `CheckMaximumLength` still assumes 30 s of logical loop length.
      Lessons so far: Clear is O(1) by an invariant (the first take
      overwrites, overdub adds, read heads own recorded lengths); `Handle()` is
      real-time too; validate before computing; Bela's 32-bit `size_t`;
      `prepared_` false first so a failed `Prepare()` never leaves pointers
      into a freed block live; the key function puts the vtable in its
      translation unit, so every virtual needs a definition (stubs) before
      the class can be used; readback state is not processing state;
      overdub reads the old loop before adding; check the state per frame
      when the engine can change it mid-block; mutation testing shows what a
      check cannot see (rewind and gain were invisible until `Process()`
      made sound).
   2. **Seam crossfade: done (pre-roll, 2026-10-30; listened on desktop
      2026-10-31: sounds great).** The user's first version blended a post-roll
      into the head, which faded every downbeat in (a one-frame click on
      frame 0 was never heard); replaced at the user's request ("correct
      everything"). Now: an input ring of fadeFrames frames is written in
      every state; Record from Empty copies it, oldest first, into a
      pre-roll slot (bounded, once per take, in `Handle()`). With
      S = min(ceil(rate × 0.005), L - 1), the loop's last S frames blend
      linearly into the pre-roll's last S frames, `x + (pre - x) ×
      (k+1)/S`, the last frame exactly the pre-roll sample: the true
      predecessor of frame 0, so the wrap is continuous and frame 0 always
      plays as recorded, on every lap. L = 1 has no seam. Overdubs in the
      seam add to the loop and the pre-roll alike (unity sum), the tail's
      fading punch too. Two pre-roll slots: a take recorded during Clear's
      tail takes the other one, so the tail keeps blending into its own.
      `Prepare()` zeroes the ring and slots (a take right after it blends
      into silence). Memory: the loop plus four fade buffers per channel
      (tail scratch, ring, two slots); no post-roll room. Checks:
      `StartTake()` feeds a take's own last frames as its pre-roll so
      other checks see a transparent seam; `CheckSeam` (six rates: frame 0
      exact on four laps, the last frame exactly the pre-roll, bounded
      steps, values whose naive blend misses the endpoint) and
      `CheckSeamEdges` (the downbeat click, one- and two-frame loops, a
      Stop tail's punch inside the seam, the slots during Clear then
      Record, the ring after `Prepare()`). Mutation-tested: 20 of 21
      caught (Clear zeroing `seamFrames_` is equivalent); the earlier
      review mutants (a, f, G, hard Stop/Clear, scratch) still caught.
      All 17 dev checks, desktop build, `check.sh`, ASan/UBSan and
      `-O3 -ffast-math` pass. The seam costs the last 5 ms before Record
      off (replaced by the 5 ms before Record on).
   3. **Desktop panel with the playhead: implemented (2026-10-31), brought
      ahead of the rig at the user's request; written by Claude (authorized),
      not a tutoring step. Resume here: look and listen on desktop** (Stratus
      in the rig menu): the first take filling red, Record off at a few
      seconds, the walking light per lap, amber overdub, Stop, Clear, PLAY
      refused while empty, THRU, LOOP LEVEL; also that Clouds' panel looks
      unchanged (FREEZE now a `LitButton`). Then review. `ParamKind::Readout`
      in Core, readouts 5 Position and 6 Length in Stratus, `StratusPanel`
      (Status). Bela: `GuiLink` already sends readouts in `wf-state` and
      `wf-update` (only Triggers are skipped) and a page's `wf-param` for one
      is dropped at the rig; a Stratus page comes with step 6.
   4. **Stratus' OLED screen: implemented (2026-10-31), written by Claude
      (authorized). Resume here, with 3: look at "OLED..." on desktop**
      while recording, playing, overdubbing, stopping and clearing; check
      Clouds' screen looks unchanged (its meters and FRZ now come from
      `wf::oled` widgets). Then review and commit 3 and 4. `Ui/Stratus`
      (Status); Bela gets it with the Stratus app (6).
   5. **Stratocumulus, Clouds → Stratus: implemented (2026-10-31), written
      by Claude (authorized). Resume here: listen on desktop with the PO-33**
      (milestone 2's sound): Clouds textures recorded into a loop, overdubs
      of Clouds as it changes, Thru off to hear only the loop, freeze into
      the loop; 64-128-frame buffers. Check the view both ways (widen the
      window) and the OLED. Then review and commit. Rig, checks, view
      (Status). Stacked overdubs of Clouds can exceed 1.0 at the output (no
      gain after the loop; a later concern).
   6. Bela app: `sketch.js`, listen, profile.

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
| 2026-10-30 | Stratus seam: the loop's last 5 ms blend into a pre-roll (the 5 ms before Record, from an always-on input ring) so every downbeat plays as recorded; a post-roll blended into the head faded each downbeat in. |
| 2026-10-30 | Stratus fades: 5 ms linear ramps on dry, overdub input and loop gain; the state sets targets. The first take's edges are content (written at full, Recording → Playing at full gain); Play from Stopped fades the downbeat in (the user's choice after listening). Stop and Clear fade out through a tail record with its own position, length and gain; Clear empties the loop at once. |
| 2026-10-31 | Engine state for UIs is a read-only parameter kind, `ParamKind::Readout`, rather than a telemetry interface: it reuses `ParamValue()`, both hosts' publishing, `RigReadout` and `wf-update` unchanged; the rig drops events for it, and hosts never send, replay or (later) store it in scenes. Stratus reports Position and Length in seconds; its state follows from Play, Record and Length. Stratus' desktop panel is proposal C (a row of 16 Clouds LEDs, no numbers) over a ring of ornament dots, a tape window and large digits; the OLED screen follows as its own step, and the panel comes before the Stratus + Clouds rig. |
| 2026-10-31 | Stratus' OLED screen is the first batch's proposal D (cells, as the panel's LED row), chosen over a Clouds-aligned layered cloud; it shares Clouds' header, rule, meters and reversed tags through `wf::oled` widgets (moved there at their second use) and shows everything all the time, with no popups. |
| 2026-10-31 | The first multi-engine rig is Stratocumulus, Clouds → Stratus (resampling: the loop records Clouds' output), the user's choice over the 2026-10-25 order (looper into Clouds); slots follow the signal. A rig's order is its code: there is no routing parameter (parameters belong to engines); the other order would be its own rig (Nimbostratus suggested), and flexible routing belongs to the mixer (milestone 5). A rig keeps its own prepared flag, so it is silent and deaf after a failed `Prepare()` whatever its engines kept. Its desktop view composes the engines' panels; one tooltip window per main window. |

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
