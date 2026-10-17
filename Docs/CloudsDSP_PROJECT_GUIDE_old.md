# CloudsEngine Project Guide

Last updated: 2026-10-10.

Read this at the start of a session. Keep current contracts, decisions, status
and next steps here; replace superseded status instead of appending another
account. Detailed exercises, reviews, render hashes and measurement tables are
preserved in the [history archive](docs/PROJECT_GUIDE_HISTORY_2026-10-06.md).
The archive is a historical snapshot and contains superseded plans.

## Goal and working style

Learn embedded and real-time audio design by building a faithful, portable
Mutable Instruments Clouds engine. Preserve its 32 kHz sound and internal
16 kHz/8-bit mu-law character. Hosts are CLI, JUCE Standalone, original full Bela
(BeagleBone Black + Cape), and later a 64 MB Daisy Seed.

Default to tutoring: introduce a small problem, let the user propose and
implement it, then explain, review, listen and measure together. Do not implement
production code unless the user authorizes it. Requested documentation edits
are allowed. Testing supports learning and listening; strict TDD is not required.

## Current status and next steps

| Area | Status |
| --- | --- |
| DSP fork and portable engine | Implemented; explicit initialization and C++14 portability established. |
| CLI | Deterministic renders for all four modes and qualities; Debug/Release references recorded in the archive. |
| Block/rate adapters | Arbitrary callback sizes and 32/44.1/48 kHz implemented; band-limited resampling deferred. |
| JUCE Standalone | Full panel, blend selection, meter, quality and mode controls implemented. User listened to all four modes on 2026-10-06; they sound right. |
| Bela audio | Granular engine audible on the board; all four qualities selectable from the panel (step 9.2), manual service policy. Defaults to 32-frame callbacks (step 8.2). Costs measured 2026-10-07 for all four qualities and for quality changes (steps 8.1, 9.1): everything fits at 32 frames. |
| Bela panel, step 7 | 7.1 messages/controls and 7.2 appearance committed (`56e93d4`); 7.3 B/LEDs committed (`c93ee31`); 9.2 quality display and switching committed (`f699420`); 9.3 level meter and IN GAIN implemented, checked offscreen and live with a WebSocket client, not yet committed. |
| Daisy / AU / VST3 | Deferred. Plugin formats require resolving process-global randomness. |

Next actions, in order:

1. Validate the panel in a browser and on a phone with the restarted Bela
   program: state after reload (including the blend selection and quality),
   mouse/touch drag, FREEZE, held/released/cancelled TRIG, B presses and holds,
   LED displays, the three-second timeout, and listening to each quality.
2. Listen to step 9.3 on the board: the meter with a real signal, freeze
   switching it to the output, and IN GAIN's effect on the sound.
3. Before enabling them on Bela, measure stretch and looping delay's per-block
   `Service()` and a spectral hop with the profiler.

## Architecture and source policy

```text
CLI / JUCE / Bela / future Daisy
              |
      SampleRateAdapter
              |
         BlockAdapter
              |
            Engine
              |
          CloudsDSP
```

The native CLI reference calls `Engine` directly. Hosts own lifecycle, I/O and
control publication; the portable stack owns conversion, buffering,
parameter/event semantics and DSP scheduling. The core contains no JUCE, Bela,
Daisy, GUI, filesystem or OS APIs.

- `Modules/CloudsDSP` is a curated Git submodule exposing `CloudsDSP::clouds_dsp`.
  Import only the required source/header closure; preserve license notices.
  Provenance and patches live in its `UPSTREAM.md` and `PATCHES.md`.
- Upstream: Mutable Instruments Eurorack commit
  `08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4`. The ignored `eurorack/` checkout is
  reference material, not a build dependency.
- `Engine/` exposes `CloudsEngine::core`. Sources compile as C++14 for Bela's
  Clang 3.9; public headers also compile in C++17 consumers. JUCE/CLI remain
  C++17. `TEST=1` must be consistent in DSP and consumer translation units.
- JUCE uses `DONT_SET_USING_JUCE_NAMESPACE=1` to avoid the `juce::Random` /
  `stmlib::Random` collision.

## Engine contracts

- Hosts own writable, non-overlapping arenas of exactly **118,784** and
  **65,408 bytes**, aligned to at least four bytes. They outlive the engine;
  initialization occurs outside real-time processing.
- `Engine` directly owns the processor, borrows the arenas, allocates no heap
  storage, and is neither copyable nor movable. Keep its roughly 9 KiB state
  and scratch blocks in persistent storage, not on the callback stack.
- `Initialize(memory, quality, seed)` validates before mutation, returns a
  status, selects granular mode and performs the first preparation. Failed
  initialization can be retried. Before success, processing outputs silence and
  service does nothing, with additional Debug assertions.
- `ProcessBlock()` consumes/produces exactly **32 stereo signed-int16 frames at
  32 kHz**, including low fidelity. Mono recording and 16 kHz mu-law conversion
  occur inside CloudsDSP. Copy public frames field-by-field into DSP scratch;
  do not reinterpret unrelated types.
- Each block takes a complete `ControlSnapshot`: nine continuous controls plus
  persistent `freeze` and `gate`. Normalized controls require `[0, 1]`; engine
  pitch requires `[-48, 48]` semitones, while host knobs use `[-24, 24]`.
  Hosts validate inputs; processing does not silently clamp them. The engine
  snapshot defaults to dry output; JUCE/Bela start at dry/wet `0.5`.
- `BlockEvents::trigger` is a one-block pulse, cleared after use. The engine
  applies `controls.gate || events.trigger` on the block consuming it, preserving
  short presses across split callbacks. Spectral reads gate at hop boundaries;
  a brief pulse can miss a hop, as on the module.
- Seed explicitly (`0x23` for references). Clouds/stmlib randomness is
  process-global: **one active engine instance** is supported. Invalid
  initialization must not reseed it.

Quality order: `Stereo16Bit32k`, `Mono16Bit32k`, `StereoMuLaw16k`, `MonoMuLaw16k`.
Mode order: `Granular`, `Stretch`, `LoopingDelay`, `Spectral`. Reject unknown enum
values before mutation.

Changing quality erases the recording and releases freeze; requesting the
current quality does not. Switching among granular, stretch and looping delay
keeps the recording; switching to/from spectral erases it and releases freeze.
Changed configurations output silence until `Service()` prepares them.

## Adapters, service and concurrency

`BlockAdapter` borrows `Engine`, accepts planar float stereo with arbitrary frame
counts, saturates input conversion, supports in-place processing and adds
32 engine frames of fixed latency. Both adapters retain triggers until a native
block consumes them.

`SampleRateAdapter` supports 32/44.1/48 kHz with persistent integer-ratio
scheduling, a four-frame output FIFO and in-place operation. Unsupported
configuration fails closed; valid reconfiguration resets stream state. Linear
interpolation is an audible scaffold without anti-alias/imaging filters;
band-limited conversion is required before fidelity sign-off. Adapter latency
is not total device latency.

`Engine::Service()` wraps firmware `Prepare()`: stretch correlation, spectral
hops and mode/quality buffer changes.

- CLI/JUCE service on the processing thread after each native block.
  `BlockAdapter::ServicePolicy::AfterEachBlock` is the default. JUCE also services
  immediately after configuration changes to avoid a silent block.
- Bela uses `Manual` and stays granular. `render()` loads the panel's
  requested quality (acquire) before the other controls and, when it differs
  from the engine's, calls `SetQuality()` and `Service()` in the callback, as
  JUCE does; otherwise it does not service. Do not add concurrent `Service()` calls: spectral
  counters and live parameters lack cross-thread synchronization.
- Hosts publish validated controls through verified lock-free atomics into
  callback-local snapshots. Independent knob updates are latest values, not an
  atomic multi-control transaction. TRIG uses a pending bool claimed with
  `exchange(false)`. Keep callback work bounded, without allocation, locking,
  logging or file/network I/O.
- JUCE releases freeze before publishing erasing quality/mode changes with
  release/acquire ordering. The callback loads configuration before controls;
  the editor reflects processor freeze state. Closing the editor clears gate.

Bela granular cost (steps 8.1 and 9.1, raw reports in
[docs/bela-profile-2026-10-07.txt](docs/bela-profile-2026-10-07.txt)): measured
with `Tools/Bela/profile.sh`, a `-DCLOUDS_PROFILE=1` build (`Apps/Bela/Profiler.h`)
that feeds a fixed test signal, steps through seven presets from no grains to
all grains with diffusion, feedback, reverb and pitch extremes at each quality,
then changes quality 40 times, and times the engine's work per callback with
Xenomai's clock. Stereo 16-bit results by block size: One 32-frame engine block
costs up to about 300 µs (1 ms of audio), so the audio thread averages about
17% at JUCE defaults and 32-35% with all grains at any block size, while the
peak per callback depends on how much work one callback can receive:

| Bela block | Deadline | Defaults, max | Heaviest preset, max | Underruns, heavy presets |
| --- | --- | --- | --- | --- |
| 16 frames | 363 µs | 142 µs (39%) | 334 µs (92%) | 60, 1 and 223 in 8 s each |
| 32 frames | 726 µs | 162 µs (22%) | 341 µs (47%) | none |
| 64 frames | 1451 µs | 285 µs (20%) | 649 µs (45%) | none |
| 128 frames | 2903 µs | 422 µs (15%) | 953 µs (33%) | none |

At 16 frames about one callback in 2.76 runs a whole engine block; with
diffusion, feedback and reverb that leaves no room for Bela's own work, and it
underruns although no engine block alone exceeded the deadline. So
`Bela_userSettings()` now asks for 32-frame callbacks (step 8.2); `-p` on the
command line still overrides it, and the board project's IDE `settings.json`
was changed from `-p 16` to `-p 32`.

At 32 frames, the other qualities: mono 16-bit (40 grains) is the heaviest,
peaking at 367 µs (51%); both mu-law qualities peak lower than stereo 16-bit
(at most 316 µs, 44%). A quality change, applied at the start of a callback as
`SetQuality()` plus the erasing `Service()`, peaked at 204-230 µs (28-32%)
including that callback's processing: the re-initialization also resets the
grains, so the change callback is lighter than a heavy steady one. No run at
32 frames underran. Every run kept
the audio thread at one mode switch. The profiler must use
`__wrap_clock_gettime()`: Bela's Makefile removes Xenomai's POSIX wrappers, and
a first attempt with plain `clock_gettime()` caused tens of thousands of mode
switches and constant underruns. These are observed costs, not WCET bounds.

Spectral requires host buffers of **at least 64 frames** at supported rates.
JUCE warns below this threshold but does not block the mode. Recorded Release
measurements found spectral callback-cost overruns at 16 frames/48 kHz;
64 frames stayed below 30% of callback duration in that experiment. These are
observed costs, not device-underrun measurements or WCET bounds. Conditions and
results are in the archive's step 6.4 table.

## Panel behavior and Bela step 7

JUCE uses 416 x 440 reference units: six main knobs plus IN GAIN, FREEZE, TRIG,
B/C and four bicolour LEDs. IN GAIN spans -18 to +6 dB, defaults to 0 dB and
ramps before processing. There are no CV/audio jacks; controls send final values
and the host routes audio. Mono input normalling, firmware pitch quantization
and BLEND end zones are not reproduced.

JUCE's B follows `ui.cc`: short presses show/cycle blend selection; turning BLEND
shows all four values; three seconds idle returns to the meter. Hold B one
second to toggle quality display or four seconds for mode display (firmware uses
four despite `secrets.md` saying five). B+C also opens mode display. Quality is
red, blend selection green, and mode pulses yellow/amber. BLEND jumps to the
selected value rather than reproducing physical-knob catch-up. C load/save is
unimplemented.

The meter follows the firmware peak follower and LED scale: input after IN GAIN
normally, output while frozen; reset on invalid/not-ready/released state.
The editor reads the atomic peak at 30 Hz. Bypass meters scaled input.

Bela's `Apps/Bela/sketch.js` is served at `http://bela.local/gui`. It ports JUCE's
drawing/interaction and caches static artwork per resize. Knobs have a
250-pixel full-range drag, value bubbles and double-click/tap reset: two presses
on the same knob within 400 ms, with no other press and no change of BLEND's
parameter in between. FREEZE
clicks on release over it; TRIG fires on press and holds gate. Mouse and one
tracked touch share press/move/release/cancel logic. Explicit touch handlers
support p5.js 1.0.0 on Firefox Android; blur/touchcancel release TRIG without
clicking FREEZE. Further fingers are ignored.

Current Bela scope: granular, all four qualities, six main knobs, IN GAIN,
FREEZE, TRIG, B and the level meter. The sketch keeps the nine engine values
and IN GAIN (in dB) in `controls`, apart from the knobs
that draw them; BLEND shows and sets the parameter B selected, jumping to its
value as in JUCE, and double-click resets it to that parameter's default.
B ports JUCE's short-press and LED rules: a release within one second shows the
selection (green), then cycles it; turning BLEND shows the four values on the
firmware intensity scale; three seconds idle, not while B is held, returns to
the meter display. Holding B one second (fired while held, or on a release
after one second; dragging off cancels it, as in JUCE's push button) toggles
the quality display (red); presses there select the next quality, which shows
freeze released at once and marks freeze as changed since the hello, so an
older reply cannot re-freeze it. Turning BLEND leaves the quality display
alone. The meter display ports JUCE's `showLevel()` and goes dark when no
level has arrived for 250 ms. On Bela the four-second mode display and B+C are
absent, and C has no action. Alternate
modes and physical controls are deferred.

The WebSocket callback in `Apps/Bela/Render.cpp` handles `clouds-hello` (answered
with `clouds-state`), `clouds-control`, `clouds-freeze`, `clouds-trig` and
`clouds-blend-parameter` and `clouds-quality`. The board keeps the blend
selection (an integer 0-3, reported as `blendParameter`) so it outlives the
page, as JUCE's processor does; only the WebSocket thread touches it and the
engine never reads it. A different quality (0-3, reported as `quality`) first
releases freeze, then is stored with release ordering, so `render()` never
re-freezes the freshly erased buffer; requesting the current quality changes
nothing. Live, 24 changes including 20 at 100 ms intervals gave no underruns or
mode switches. It rejects
invalid types/keys and non-finite controls, clamps host ranges and publishes
atomics; `render()` reads them once per callback. Bela's older library requires
`__atomic_always_lock_free`, `ATOMIC_BOOL_LOCK_FREE` and `ATOMIC_INT_LOCK_FREE` checks.

IN GAIN (step 9.3) is the `clouds-control` key `inputGain`, in dB, clamped to
-18..+6 and reported in `clouds-state`. `render()` applies it while copying
Bela's read-only input into the output buffers, ramped across one callback
like JUCE's `applyGainRamp()`, and the adapters then process those buffers in
place. The meter is the firmware peak follower at Bela's rate, on the input
after IN GAIN, or on the output while frozen, as in JUCE. `render()` only
stores the level in an atomic; an ordinary Linux `std::thread`, started last
in `setup()` and joined in `cleanup()`, sends `{"event": "clouds-meter",
"level": <linear peak>}` every 33 ms, also while the level holds, so a page
can tell when the board has stopped. Under Bela's `-ffast-math`, Clang 3.9
folds `std::isfinite()` to true for NaN (checked on the board), so a NaN or
infinite input stuck in the peak for good; the meter's reset and the
controls' non-finite check now test the exponent bits (`IsFinite()`). Live on 2026-10-10: 30.0 messages a
second (largest gap 34 ms); with nothing plugged in, +6 and -18 dB moved the
noise floor by +6.0 and -18.2 dB; freezing switched the meter to the output;
`bela-audio` stayed at one mode switch. The profiler now times the gain copy
and meter with the engine; a 32-frame rerun
([docs/bela-profile-2026-10-10.txt](docs/bela-profile-2026-10-10.txt)) matched
step 8.1 within run-to-run variation (defaults 162 µs, heaviest mono 16-bit
376 µs or 52%, previously 367 µs), with no underruns and one mode switch.

Replies echo a random hello ID because Bela broadcasts to all pages. Apply only
the latest matching reply; preserve edits made after its hello. `sendHello()`
now flushes pending changes first, fixing the remaining review race where the
board held `0.8` while the page showed `0.5`. The blend selection reconciles like
the knobs. A held, unmoved knob rebases its drag to the returned value. The ID
echo and broadcast were confirmed live with two WebSocket clients; touch and
sync behavior still need validation in a browser and on a phone. Separate pages
are not continuously synchronized. Closing a page with TRIG held can leave gate high
on the board; granular ignores that gate.

## Build and verification

Desktop:

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target CloudsEngine_Standalone clouds_cli
cmake --build build --target clouds_engine_init_check clouds_engine_quality_check clouds_engine_playback_mode_check clouds_engine_block_adapter_check clouds_engine_sample_rate_adapter_check
```

Run the five diagnostics from `build/Engine/`. The app is
`build/Source/CloudsEngine_artefacts/Debug/Standalone/Clouds Engine.app`.
Reconfigure after adding `Source/` files because its target uses a glob.
CLI: `build/Apps/CLI/clouds_cli <quality> --mode <mode>`; qualities: `stereo16`,
`mono16`, `stereo-mulaw`, `mono-mulaw`; modes: `granular`, `stretch`,
`looping-delay`, `spectral`. Output: `clouds_output_<quality>_<mode>.wav`.

Bela: see [Tools/Bela/README.md](Tools/Bela/README.md).

```sh
Tools/Bela/check.sh
Tools/Bela/deploy.sh
Tools/Bela/profile.sh    # callback cost at 16/32/64/128 frames; about 5 min
```

The local check uses Clang 3.9 and pulled board headers in syntax-only mode;
code generation, linking and runtime require the board. Deployment stages
translation units at the project root and wraps DSP `.cc` files in `.cpp`.
Board project: `/root/Bela/projects/CloudsEngine`; IDE Make parameters:
`CPPFLAGS=-std=c++14 -DTEST=1` without quotes. Recorded versions: Debian 9.13,
Clang 3.9.1, GCC/libstdc++ 6, Xenomai Linux 4.14, Bela core `e43b8abe`.
Its `-O3 -ffast-math -DNDEBUG` flags affect comparisons and disable assertions.
The board has no clock battery: after a reboot it starts in 2016 until the
IDE, opened in a browser, sets the time. Deploying from the terminal before
that leaves the old objects looking newer than the copied sources, and make
reports nothing to do. Set the clock first
(`ssh root@192.168.7.2 date -u -s @$(date +%s)`), then touch the changed
sources on the board.

Use listening plus focused ownership, lifecycle, partition-invariance, in-place,
trigger-retention and finite-output checks. Recorded validation covers five
engine diagnostics, 16 Debug/Release renders and panel interactions; it does
not establish every device deadline or complete real-time safety. Use hashes
only for expected bit identity; otherwise use aligned numerical metrics.
Keep observed maxima, callback-cost overruns, device misses/XRUNs, latency and
WCET distinct. Export bounded, preallocated instrumentation after audio stops.

## Open questions and deferred work

- Replace linear resampling before fidelity completion; extend quality listening.
  Short recordings at POSITION `0.5` can reach unwritten space in the roughly
  eight-second mono/mu-law buffer, explaining near-silent reference renders.
- Measure Bela stretch service cost before enabling it; spectral needs a
  scheduling/synchronization design and board measurements. The user prefers
  skipping Bela spectral if the port becomes too difficult. Desktop costs do
  not predict Cortex-A8 deadlines.
- Decide make-up gain by measuring input, dry, live wet and frozen wet levels.
  Fully dry is about -9 dB below input from DSP crossfade/headroom scaling.
  The user's proposed freeze-only gain differs from the module and must follow
  `freeze_lp_` rather than step on/off.
- Save quality, mode and blend values; implement C load/save later.
- Resolve per-instance randomness before multiple engines or AU/VST3; defer
  Daisy memory placement, caches and optimization until hardware work.
- Known sanitizer findings remain in
  [CloudsDSP/PATCHES.md](Modules/CloudsDSP/PATCHES.md), including the full-wet
  crossfade-table read and wrapping shifts/conversions. They are not resolved
  merely because Debug/Release renders agree.
- References: `references/cloudsfrontpanel.png`, `cloudsquality.png`,
  `clouds_quickstart.pdf`, `secrets.md`, and `eurorack/clouds/ui.cc` /
  `drivers/leds.h`. `references/CloudsManual.md` is empty.
