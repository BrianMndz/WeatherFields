# Bela tools

Checks on the Mac whether the Bela build will compile, then builds and runs it
on the board.

> `deploy.sh` and `profile.sh` are still CloudsEngine's and stop with a message:
> they are adapted with the Bela host, to stage a Bela app from its manifest.
> Their sections below describe CloudsEngine's behaviour until then.

Nothing here builds code for the board on the Mac: the board still compiles,
links and runs the project. `check.sh` runs the board's compiler version against
the board's headers with Bela's flags in syntax-only mode, so compile errors show
up in about two seconds instead of after an upload and an on-board build.
`deploy.sh` runs that check, then builds on the board with Bela's own Makefile.

## Setup (once)

```sh
Tools/Bela/fetch-toolchain.sh      # about 30 s; about 270 MB in ~/.cache/weatherfields/bela-toolchain
Tools/Bela/pull-board-headers.sh   # with the board connected; uses BBB_ADDRESS (default root@192.168.7.2)
Tools/Bela/check.sh --self-test
```

Requires macOS. On Apple Silicon it also needs Rosetta 2:
`softwareupdate --install-rosetta --agree-to-license`.

Until the board's headers are pulled, `check.sh` can use a local Bela checkout's
headers instead: `BELA_CHECKOUT=~/path/to/Bela Tools/Bela/check.sh`.

## Use

```sh
Tools/Bela/check.sh                                   # every source of every manifest
Tools/Bela/check.sh --manifest clouds                 # one manifest
Tools/Bela/check.sh --manifest clouds path/to/file.cpp  # just these files, with its flags
Tools/Bela/check.sh --std=c++14                       # try another language standard
Tools/Bela/check.sh -DWF_PROFILE=1                    # check with a macro defined
```

The file lists come from CMake. `wf_add_manifest(<name> TARGET <target>)`
(`cmake/WeatherFields.cmake`) writes `build/<preset>/manifests/<name>.txt` with
every source, include directory and define needed to build the target and
everything it links. `check.sh` first configures the `dev` preset (`WF_PRESET`
picks another; `--no-configure` skips it), so the lists always follow the
CMake targets, then checks each manifest's sources with that manifest's flags.

## Build and run on the board

```sh
Tools/Bela/deploy.sh          # check, copy changed files, build on the board
Tools/Bela/deploy.sh --run    # the same, then run it; Ctrl-C stops it
Tools/Bela/deploy.sh --stop   # stop whatever Bela program is running
Tools/Bela/deploy.sh --clean  # full rebuild
```

The board project is `/root/Bela/projects/CloudsEngine`. Its `build/` folder,
program binary and the Bela IDE's `settings.json` are never deleted.

`deploy.sh` stages the project for Bela's Makefile, which compiles every `.cpp`
and `.c` file under the project folder but tracks header dependencies only for
files at its top level. So every compiled file is staged at the top level: the
app's own files, copies of the engine sources, and one-line `dsp_*.cpp` wrappers
that `#include` each CloudsDSP `.cc` file, which Bela would otherwise skip.
Headers keep their include paths below the project folder, so the only extra
flags the board needs are `-std=c++14 -DTEST=1`. Only files whose contents
changed are copied, so the board rebuilds only what changed.

## Measure the callback cost

```sh
Tools/Bela/profile.sh              # the default block size, 32; about 5 minutes
Tools/Bela/profile.sh 16 32 64     # these block sizes, one run each
Tools/Bela/profile.sh | tee profile.txt
Tools/Bela/deploy.sh --profile     # only build the measurement program
```

`deploy.sh --profile` builds the app with `-DCLOUDS_PROFILE=1` as a separate
board project, `CloudsEngineProfile`, because Bela's Makefile does not rebuild
objects when the flags change. That build replaces the input with a fixed
test signal and the panel with presets, which it runs at each audio quality,
then changes the quality 40 times; it mutes the output, times the engine's
work in every callback, stops itself and prints a report: mean,
percentiles and maximum against the callback deadline, Bela's underrun count
and its CPU monitoring of the audio thread (`Apps/Bela/Profiler.h`).
`profile.sh` runs it once per block size and adds the audio thread's line
from `/proc/xenomai/sched/stat`, whose MSW column should stay at 1, the mode
switch at start. It refuses to start while another Bela program runs.

Code running in `render()` must call Xenomai's versions of POSIX services,
such as `__wrap_clock_gettime()`: Bela's Makefile removes Xenomai's POSIX
wrappers, so the plain calls are Linux calls that switch the audio thread out
of real-time mode.

## How it matches the board

| Piece | Source | Catches |
|---|---|---|
| Clang 3.9.0 | official LLVM macOS release, run through Rosetta 2 | newer language features, e.g. structured bindings |
| libstdc++ 6.3, glibc 2.24 | Debian 9 armhf packages, or the board's own `/usr/include` | missing library features, e.g. `std::byte` |
| Bela headers and Xenomai flags | the board, via `pull-board-headers.sh` | Bela API misuse |
| `-march=armv7-a -mfpu=neon -mfloat-abi=hard -O3 -ffast-math -DNDEBUG ...` | Bela's Makefile | ARM-only code paths and macros |

Every Clang binary can parse code for any target it supports, so the Mac-hosted
Clang 3.9 parses for ARM Linux by the same rules as the board's Clang 3.9.
`-fsyntax-only` stops before code generation, so no ARM libraries or linker are
needed.

`check.sh --self-test` shows that this holds. It prints the versions and target
macros the compiler reports, and it requires each sample in `selftest/` to be
accepted or rejected with the board's error message. When the board rejects
something this check missed, add a `reject_*.cpp` sample for it.

## Limits

- **`check.sh` does not check linking.** Link errors appear in the on-board
  build that `deploy.sh` runs.
- **Runtime and code generation are not checked.**
- **Clang 3.9.0 vs. 3.9.1:** the board probably runs 3.9.1, a bug-fix release
  with the same language support. `pull-board-headers.sh` prints the board's version.
- **Rosetta 2:** Apple has announced that general Rosetta support ends after
  macOS 27. Without it, `--host-clang` still catches missing library features
  but accepts newer language features the board rejects.

## Files and settings

- `common.sh`: pinned versions and checksums, Bela's flags, reading the
  manifests. Shared by the other scripts.
- `fetch-toolchain.sh`: downloads, verifies and unpacks Clang 3.9 and the
  Debian 9 headers.
- `pull-board-headers.sh`: copies the board's headers, Xenomai flags and Bela
  Makefile, and prints the board's compiler versions.
- `check.sh`: the check itself.
- `deploy.sh`: check, stage, copy and build on the board, and optionally run.
- `profile.sh`: build the measurement program and run it at several block sizes.
- `selftest/`: samples the toolchain must accept or reject.

| Variable | Default | Meaning |
|---|---|---|
| `BELA_TOOLCHAIN_DIR` | `~/.cache/weatherfields/bela-toolchain` | where downloads and pulled headers live |
| `WF_PRESET` | `dev` | CMake preset whose build tree holds the manifests |
| `BBB_ADDRESS` | `root@192.168.7.2` | the board, as in Bela's scripts (`root@bela.local` also works) |
| `BBB_BELA_HOME` | `/root/Bela` | Bela's folder on the board |
| `BELA_CHECKOUT` | unset | local Bela checkout to use before the board's headers are pulled |
| `BELA_CXX_STD` | `c++14` | default for `--std` |
| `BELA_PROJECT_NAME` | `CloudsEngine` | project folder name on the board |
