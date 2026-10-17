# Bela + Daisy Seed3 + PO-33 Modular Music System

## Context

I am building/exploring a modular embedded music system focused on creating music and sound design rather than reproducing one particular commercial synthesizer.

Musical directions include:

- dark ambient
- evolving textures
- granular sound design
- experimental electronic music
- lo-fi beats
- hip-hop
- G-funk
- synth-oriented music

I am a C/C++ software engineer with DSP/audio experience, so I am interested both in making music and in designing the underlying embedded architecture myself.

The goal is not necessarily to build one giant synthesizer. A better direction is a collection of complementary engines that communicate with each other.

---

# Hardware I Already Have

## Bela / BeagleBone Black

Bela running on a BeagleBone Black.

Important hardware problem:

**The BBB microSD card reader is physically broken.**

The Bela system currently boots successfully from internal eMMC.

I previously managed to recover this BBB despite the broken SD reader using low-level USB/U-Boot recovery and ultimately got Bela working from eMMC.

Therefore the board is usable, but normal SD-card-based recovery is unavailable.

Bela should remain an important part of the system despite this limitation.

---

## ROLI Blocks

I have:

**ROLI Lightpad Block**

and

**Developer Control Block**

The Developer Control Block is the older developer version with numbered buttons:

```text
0 1 2 3 4 5 6 7
      - +
```

The intended initial connection is:

```text
Developer Control Block
        │
       DNA
        │
        ▼
   Lightpad Block
        │
       USB
        │
        ▼
      Bela
```

Bela acts as USB host.

The first integration strategy is **standard MIDI**, rather than trying to port the old ROLI BLOCKS SDK.

ROLI controls can initially provide:

```text
Lightpad:
- XY control
- faders
- continuous MIDI CC
- eventually pressure/MPE

Control Block:
- mode switching
- record
- overdub
- freeze
- reverse
- loop selection
- scene selection
```

Later, direct BLOCKS SDK integration might be interesting for LED feedback and deeper interaction, but it is not the first priority.

---

## Daisy Seed3

I am considering/using Daisy Seed3 as the other embedded DSP platform.

The conceptual distinction we arrived at is:

**Bela = flexible real-time Linux audio computer**

**Seed3 = dedicated embedded DSP/synth module**

The two should complement rather than duplicate each other.

Seed3 is best used for deterministic dedicated synthesis engines such as:

```text
wavetable
virtual analog
FM
physical modelling
resonators
drum synthesis
specialized effects
```

Bela is better suited to:

```text
USB MIDI/MPE
routing
sequencing
sample management
granular processing
looping
recording
presets
filesystem
networking
experimental algorithms
```

---

## Teenage Engineering PO-33 KO!

I also have a PO-33.

The PO-33 should **not** be treated as something Bela/Seed3 need to reproduce.

Its value is its limitations and immediacy.

Its role is:

```text
sampling
chopping
beats
one-shots
resampling
lo-fi character
quick sequencing
performance effects
```

This gives the system three very different sound philosophies:

```text
Seed3 = synthesized sound

PO-33 = sampled / captured sound

Bela = transformed / looped / processed sound
```

This is considered a desirable separation.

---

# Overall Architecture

The current preferred architecture is:

```text
        ROLI Lightpad / Control Block
                    │
                 USB MIDI
                    │
                    ▼
            ┌──────────────┐
            │     BELA     │
            │              │
            │ MIDI Router  │
            │ Clock        │
            │ Sequencer    │
            │ Scenes       │
            │ Modulation   │
            │ Looper       │
            │ Granular     │
            │ Recorder     │
            │ Master FX    │
            └──────┬───────┘
                   │
             MIDI / UART
                   │
                   ▼
            ┌──────────────┐
            │    SEED3     │
            │              │
            │ Dedicated    │
            │ synth engine │
            └──────┬───────┘
                   │
                 AUDIO
                   │
                   ▼

              mixer / Bela

PO-33 ───────────────► audio system
```

A useful mental model is:

> Seed3 generates.  
> PO-33 samples and grooves.  
> Bela coordinates, captures, loops, transforms and records.

---

# Bela's Intended Role

Bela should increasingly become the **central brain and transformation layer**, rather than simply another synthesizer.

Its main modules could eventually look like:

```text
BelaEngine
│
├── MidiRouter
├── Transport
├── Sequencer
├── SceneManager
├── ModulationEngine
│
├── LoopEngine
├── GranularEngine
├── SampleManager
│
├── Mixer
├── Delay
├── Reverb
├── FeedbackProcessor
│
└── Recorder
```

It could also communicate with Seed3 through either:

```text
USB MIDI during prototyping
```

or later:

```text
high-speed UART
```

with a custom binary/control protocol.

For example:

```text
NOTE_ON
NOTE_OFF
PARAM
SCENE
CLOCK
ENGINE
```

This would eventually be preferable to traditional 31.25 kbit/s DIN MIDI for internal communication between the two processors.

---

# Daisy Seed3 Role

Seed3 should be treated as a **replaceable DSP personality**.

Rather than being "the Daisy synth," different firmware/engines could implement:

```text
WavetableEngine
FMEngine
PhysicalModelEngine
ResonatorEngine
DrumSynthEngine
DroneEngine
EffectEngine
```

Ideally these would expose a similar conceptual API:

```cpp
noteOn()
noteOff()
setParameter()
process()
```

This allows Bela to interact with different Seed3 engines through the same higher-level control architecture.

---

# Best First Seed3 Engine

The first recommended engine is a **polyphonic Tone Engine**.

Potential architecture:

```text
MIDI
 │
 ▼
Voice Manager
 │
 ├───────────────┐
 ▼               ▼
Wavetable       OSC B
                VA/FM/noise
 │               │
 └───────┬───────┘
         ▼
       Mixer
         │
       Filter
         │
   Drive / Saturation
         │
   Chorus / Delay
         │
       Output
```

This can cover a wide musical range.

For dark ambient:

```text
slow envelopes
wavetable motion
noise
detuning
slow modulation
```

For G-funk:

```text
saw/pulse
portamento
resonant filter
pitch modulation
chorus
```

For lo-fi:

```text
simple oscillator
detune
low-pass
saturation
wow/flutter
```

For synthwave:

```text
detuned saws
filter envelopes
chorus
```

A second Seed3 engine should probably be a **physical modelling / resonator engine**, rather than simply another subtractive synth.

---

# Bela Texture Engine

Bela can specialize in sample-heavy transformation.

Proposed parameters:

```text
sample position
grain size
density
pitch
pitch spread
position randomness
reverse probability
stereo spread
feedback
freeze
degradation
```

The Lightpad is particularly suitable for this.

Example:

```text
Y = grain density / feedback
X = sample position
Pressure = freeze or effect amount
```

---

# Bela Looper

Bela should also function as a serious creative looper.

The key architectural decision is:

**Active loops live in RAM.**

Do NOT depend on disk access from the real-time audio thread.

Possible loop engine:

```text
Loop A
Loop B
Loop C
Loop D
```

Potential operations:

```text
record
play
overdub
undo
clear

reverse
half-speed
double-speed
pitch
feedback

granular
freeze
scatter
randomization
```

Possible Control Block mapping:

```text
0 = Record A
1 = Record B
2 = Record C
3 = Record D

4 = Clear
5 = Overdub
6 = Reverse
7 = Freeze

- = Previous loop
+ = Next loop
```

Or alternatively buttons 0–7 could switch global modes and +/- move between scenes.

---

# Bela Recorder

Recording should be separate from looping.

Architecture:

```text
REAL-TIME AUDIO THREAD
        │
        ▼
preallocated lock-free FIFO
        │
        ▼
non-real-time writer thread
        │
        ▼
USB storage
```

Never perform filesystem writes directly from Bela's `render()` callback.

The recorder could eventually support different tap points:

```text
dry external input
Seed3 output
loop bus
processed Bela output
stereo master
```

Active loop data stays in RAM.

Long-term recording goes to external storage.

---

# Broken SD Reader Strategy

The SD reader is broken, but this does not invalidate Bela as a looper/recorder.

Storage architecture should instead be:

```text
eMMC:
- Bela/Linux
- application binaries
- configuration
- small presets

USB storage:
- samples
- field recordings
- loops
- saved scenes
- long recordings
```

Example filesystem:

```text
/mnt/usb/
│
├── samples/
│   ├── drums/
│   ├── textures/
│   ├── field-recordings/
│   ├── chords/
│   └── one-shots/
│
├── wavetables/
├── loops/
├── scenes/
└── recordings/
```

Because the BBB only has one USB host connector and multiple USB devices may eventually be involved, the likely future topology is:

```text
BBB USB-A
   │
   ▼
powered USB hub
   │
   ├── ROLI Lightpad
   ├── USB storage
   └── possibly Seed3 USB
```

A powered hub is preferred rather than asking the BBB to power everything.

---

# Sample Streaming Architecture

Small samples should simply be loaded into RAM.

Example:

```text
USB storage
     │
     │ startup / preset change
     ▼
    RAM
     │
     ▼
real-time DSP
```

Examples:

```text
drum samples
one-shots
wavetables
short textures
impulses
```

Long material could later use:

```text
USB
 │
 ▼
background reader
 │
 ▼
circular RAM buffer
 │
 ▼
real-time DSP
```

This may eventually allow long field recordings to feed granular processing without storing the entire file in RAM.

This is a later-stage feature.

---

# Saving Loops / Scenes

An interesting future feature is complete session recall.

For example:

```text
/mnt/usb/scenes/forest_003/

scene.json
loop_A.wav
loop_B.wav
loop_C.wav
```

`scene.json` could include:

```text
Seed3 engine
Seed3 parameters

Bela effect parameters

loop settings

sequencer patterns

modulation routing

tempo

ROLI mappings
```

Then:

```text
LOAD forest_003
```

could restore the whole instrument state.

---

# Sequencing Philosophy

Rather than immediately building a conventional DAW-like sequencer, the system should probably use several independent event lanes.

Example:

```text
Drums     | x---x-x---x--x--
Bass      | x-------x-------
Chord     | ----x-------x---
Texture   | ------x---------
Random    | ?    ?      ?
```

Capabilities worth implementing:

```text
probability
swing
ratchets
humanization
polyrhythm
random pitch
random velocity
conditional triggers
```

This allows the same engine to move between:

```text
hip-hop
G-funk
lo-fi
ambient
generative music
experimental music
```

without changing the system architecture.

---

# Scenes Instead of Genres

The system should remain genre-independent.

A "scene" changes parameter configuration rather than architecture.

Example G-funk scene:

```text
Seed3:
- VA lead
- saw/pulse
- glide
- resonant filter
- chorus

PO-33:
- kick
- clap/snare
- hats
- vocal chops

Bela:
- clock/sequencer
- short delay
- reverb
```

Example lo-fi scene:

```text
Seed3:
- Rhodes-like/wavetable patch
- tremolo
- filtering

PO-33:
- dusty drums
- chopped sample

Bela:
- texture/noise
- tape-like modulation
- granular imperfections
- delay
```

Example dark ambient scene:

```text
Seed3:
- resonator / physical model

PO-33:
- foley / one-shot material

Bela:
- field recording
- granular
- long loops
- probabilistic events
- feedback
- reverb
```

---

# PO-33 Integration

The PO-33 should remain intentionally tactile.

Useful workflows:

## Seed3 → PO-33

```text
Seed3 patch
   │
   ▼
sample into PO-33
   │
   ▼
pitch/chop/sequence
```

This can turn pristine synthesis into something resembling an old sampled artifact.

Very useful for lo-fi and hip-hop.

## PO-33 → Bela

```text
PO-33 beat/sample
       │
       ▼
      Bela
       │
       ├── granular
       ├── reverse
       ├── freeze
       ├── delay
       └── reverb
```

This is useful for converting beat-oriented material into ambient textures.

## Resampling loop

Eventually:

```text
PO-33
  ↓
Bela processing
  ↓
record result back into PO-33
  ↓
repeat
```

This can produce progressively transformed sound generations.

---

# PO-33 Synchronization

The PO-33 has no conventional MIDI input.

It uses analog/audio sync pulses.

Bela should eventually act as master clock and generate PO-compatible sync.

Concept:

```text
Bela Transport
      │
      ▼
sync pulse output
      │
      ▼
PO-33
```

PO sync/audio share the PO's 3.5 mm connectors, so routing needs to account for the PO's SY modes.

This is another reason a small mixer may eventually be useful, but no additional hardware needs to be purchased now.

---

# System-Wide Modulation

A major long-term concept is **modulation as a shared resource**.

Bela could produce:

```text
LFO 1
LFO 2
random
envelope follower
sequencer lanes
Lightpad XY
velocity
aftertouch
probability
```

and route them to either Bela or Seed3 parameters.

Example:

```text
LFO1
 ├── Seed3 filter cutoff
 └── Bela grain position

Random
 └── Seed3 wavetable index

Lightpad X
 └── Seed3 FM amount

Lightpad Y
 └── Bela grain density

Velocity
 ├── Seed3 amplitude
 └── Bela reverb send
```

This is one of the concepts that could make the system feel genuinely modular.

---

# Shared C++ Architecture

DSP code should ideally remain independent from platform-specific code.

Potential project structure:

```text
Core/
│
├── dsp/
│   ├── Oscillator
│   ├── Filter
│   ├── Envelope
│   ├── Delay
│   ├── Granulator
│   └── Resonator
│
├── synth/
│   ├── Voice
│   ├── VoiceManager
│   └── ModMatrix
│
├── control/
│   ├── ParameterState
│   └── Mapping
│
└── utilities/

Platforms/
│
├── Bela/
│
├── Daisy/
│
└── Desktop/
```

Ideally the actual DSP algorithms can run on:

```text
desktop
Bela
Seed3
```

with only the hardware/audio/MIDI adaptation changing.

---

# Immediate Development Priorities

Do not try to implement the entire ecosystem at once.

Recommended development sequence:

### Stage 1 — ROLI → Bela MIDI

Connect:

```text
Control Block
    ↓ DNA
Lightpad
    ↓ USB
Bela
```

Use:

```bash
lsusb
amidi -l
amidi -p hw:X,Y,Z -d
```

Identify exactly what each control sends.

Then implement a small `RoliController` / `MidiController` layer.

### Stage 2 — Bela Looper

Implement one RAM loop:

```text
record
play
overdub
clear
```

Then add:

```text
reverse
half-speed
feedback
```

Only after that, expand to multiple loops.

### Stage 3 — Recorder

Implement:

```text
audio callback
   ↓
lock-free SPSC FIFO
   ↓
AuxiliaryTask / writer thread
   ↓
USB WAV recording
```

### Stage 4 — Granular loop manipulation

Allow a loop itself to become the grain source.

For example:

```text
PO-33 beat
  ↓
record loop
  ↓
granularize loop
  ↓
ambient texture
```

### Stage 5 — Seed3 communication

Initially MIDI or USB.

Later potentially high-speed UART with a custom protocol.

### Stage 6 — Seed3 Tone Engine

Start with:

```text
polyphonic wavetable / VA
filter
envelopes
LFO
modulation
simple effects
```

Then explore FM and physical modelling.

---

# Important Design Principles

1. **Do not make Bela and Seed3 duplicate each other.**

2. **Do not put filesystem operations in Bela's real-time audio callback.**

3. **Active loops should stay in RAM.**

4. **Persistent recording should go to USB storage asynchronously.**

5. **Treat the PO-33 as an instrument, not as a problem that should be reimplemented in C++.**

6. **Build musical engines first, infrastructure second only where required.**

7. **Prefer engines + scenes over building one giant synthesizer.**

8. **Design platform-independent DSP code where practical.**

9. **Use the ROLI Blocks as the temporary control surface before building custom hardware controls.**

10. **The first meaningful milestone should be something playable, not a complete architecture.**

---

# Current Concept in One Diagram

```text
                   ┌─────────────────────┐
                   │     ROLI BLOCKS     │
                   │ Lightpad + Control  │
                   └──────────┬──────────┘
                              │ USB MIDI
                              ▼

                    ┌───────────────────┐
                    │       BELA        │
                    │                   │
                    │ Transport         │
                    │ MIDI routing      │
                    │ Sequencer         │
                    │ Modulation        │
                    │ Scenes            │
                    │                   │
                    │ Loop Engine       │
                    │ Granular Engine   │
                    │ Mixer             │
                    │ Delay/Reverb      │
                    │ Recorder          │
                    └───┬───────────┬───┘
                        │           │
                  MIDI/UART         │ USB
                        │           ▼
                        │      ┌──────────┐
                        │      │ Storage  │
                        │      │ Samples  │
                        │      │ Loops    │
                        │      │ Sessions │
                        │      └──────────┘
                        ▼
                 ┌───────────────┐
                 │    SEED3      │
                 │               │
                 │ Tone Engine   │
                 │ FM            │
                 │ Wavetable     │
                 │ Resonator     │
                 │ Physical      │
                 │ modelling     │
                 └──────┬────────┘
                        │
                       AUDIO
                        │
                        ▼

             ┌────────────────────────┐
             │       AUDIO WORLD      │
             │                        │
PO-33 ──────►│ sampling / beats       │
Seed3 ──────►│ synthesized voices     │
Bela ───────►│ loops/textures/FX      │
             └────────────┬───────────┘
                          │
                          ▼
                       OUTPUT
```

# Where to Continue Next Conversation

The most useful next discussion is probably one of these:

**A. Design and implement the first Bela RAM looper in C++.**

**B. Design the complete Bela software architecture before coding.**

**C. Connect the ROLI Blocks to Bela and inspect their MIDI messages.**

**D. Design the Bela recorder + USB-storage FIFO architecture.**

**E. Design the Seed3 Tone Engine and the shared Bela↔Seed control protocol.**

**F. Design the first actually playable instrument/scene using Bela + ROLI + PO-33 before adding Seed3.**

The current strongest recommendation is to start with **C → A → D**:

```text
ROLI integration
       ↓
one working looper
       ↓
USB recording
       ↓
granular manipulation
       ↓
Seed3 integration
```

That sequence gets to something musically playable quickly while still establishing the architecture needed for the larger modular system.