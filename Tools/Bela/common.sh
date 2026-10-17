# Shared configuration for the Bela tools. Sourced by the scripts, not run directly.
# Must stay compatible with macOS's /bin/bash 3.2.

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLS_DIR="$REPO_ROOT/Tools/Bela"

# Downloaded and pulled files live outside the repository so IDEs do not index
# them and they cannot be committed by accident.
TOOLCHAIN_DIR="${BELA_TOOLCHAIN_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/weatherfields/bela-toolchain}"
DOWNLOAD_DIR="$TOOLCHAIN_DIR/downloads"
DEBIAN_SYSROOT="$TOOLCHAIN_DIR/debian9-armhf-sysroot"
BOARD_DIR="$TOOLCHAIN_DIR/board"

# Board connection: same variable names and defaults as Bela's own scripts.
BBB_ADDRESS="${BBB_ADDRESS:-root@192.168.7.2}"
BBB_BELA_HOME="${BBB_BELA_HOME:-/root/Bela}"

# --- Pinned downloads ---------------------------------------------------------

# Official LLVM macOS x86_64 release; runs on Apple Silicon through Rosetta 2.
# The board's Debian 9 image ships Clang 3.9 (3.9.1 bug-fix release). The SHA-256
# was recorded from the first verified download.
CLANG_VERSION=3.9.0
CLANG_ARCHIVE="clang+llvm-$CLANG_VERSION-x86_64-apple-darwin.tar.xz"
CLANG_URL="https://releases.llvm.org/$CLANG_VERSION/$CLANG_ARCHIVE"
CLANG_SHA256=bd689aaeafa19832016d5a4917405a73b21bc5281846c0cb816e9545cf99e82b
CLANG_DIR="$TOOLCHAIN_DIR/clang-$CLANG_VERSION"

# Debian 9 "stretch" armhf headers. Versions and SHA-256 values come from
# $DEBIAN_MIRROR/dists/stretch/main/binary-armhf/Packages.xz.
DEBIAN_MIRROR=https://archive.debian.org/debian
DEBIAN_PACKAGES=(
    "pool/main/g/gcc-6/libstdc++-6-dev_6.3.0-18+deb9u1_armhf.deb 54933bdadc77af75179909d417bd038f2a3dca5c0447e9b7bfe22944d8da64ff"
    "pool/main/g/glibc/libc6-dev_2.24-11+deb9u4_armhf.deb 8a82224c7fb069c0cbfee092c446a16392c986ea969d8dbcd8ccfced516ffc1a"
    "pool/main/l/linux/linux-libc-dev_4.9.228-1_armhf.deb 8fdb967bc5887bc67e188fff93e4e6701036334b4134c13b35080e60d34a426b"
)
LIBSTDCXX_MAJOR=6

# --- Bela build flags -----------------------------------------------------------

# Mirrors Bela's Makefile for a Xenomai 3 board using clang: ARCH_FLAGS,
# DEFAULT_COMMON_FLAGS and the project compile rule. The xeno-config part of
# DEFAULT_XENOMAI_CFLAGS can only be read on the board (pull-board-headers.sh).
BELA_COMMON_FLAGS=(
    -DXENOMAI_SKIN_posix -DXENOMAI_MAJOR=3
    -O3 -g
    -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=hard
    -ftree-vectorize -ffast-math -DNDEBUG -DBELA_USE_RTDM -DENABLE_PRU_UIO=1
)
BELA_CXX_DEFAULT_STD=c++11
BELA_C_DEFAULT_STD=gnu11
BELA_RULE_FLAGS=(-Wall -fmessage-length=0 -U_FORTIFY_SOURCE)

# Project flags: what the Bela build must receive as CPPFLAGS. The last -std
# wins, overriding Bela's default -std=c++11. Code below Platforms targets
# C++14 so the board's Clang 3.9 can build it (Clang 3.9 spells C++17
# "c++1z"). Include directories and defines such as TEST=1 come from the
# source manifests; PROJECT_DEFINES holds extra ones given on the command line.
PROJECT_CXX_STD="${BELA_CXX_STD:-c++14}"
PROJECT_DEFINES=()

# --- Source manifests -------------------------------------------------------------

# CMake writes one manifest per build that runs outside CMake (wf_add_manifest
# in cmake/WeatherFields.cmake): lines "source <file>", "include <dir>" and
# "define <macro>". The manifests of this preset's build tree are used.
WF_PRESET="${WF_PRESET:-dev}"
MANIFEST_DIR="$REPO_ROOT/build/$WF_PRESET/manifests"

# --- Helpers --------------------------------------------------------------------

die() {
    echo "error: $*" >&2
    exit 1
}

sha256_of() {
    shasum -a 256 "$1" | cut -d' ' -f1
}

require_clang39_host() {
    [ "$(uname -s)" = Darwin ] || die "these tools run the macOS build of Clang $CLANG_VERSION and support macOS only"
    if [ "$(uname -m)" = arm64 ] && ! arch -x86_64 /usr/bin/true 2>/dev/null; then
        die "Rosetta 2 is needed to run the x86_64 Clang $CLANG_VERSION. Install it with: softwareupdate --install-rosetta --agree-to-license"
    fi
}

# Prints the command prefix that runs the x86_64 Clang: Rosetta 2 on Apple Silicon.
clang39_prefix() {
    if [ "$(uname -m)" = arm64 ]; then echo "arch -x86_64"; fi
}

# Configures the preset's build tree, which rewrites the manifests from the
# CMake targets, so they never lag behind a CMakeLists.txt change.
refresh_manifests() {
    command -v cmake >/dev/null || die "cmake is not on PATH"
    (cd "$REPO_ROOT" && cmake --preset "$WF_PRESET" >/dev/null) \
        || die "configuring the CMake preset '$WF_PRESET' failed"
}

manifest_file() {
    echo "$MANIFEST_DIR/$1.txt"
}

# Prints the name of every manifest, one per line.
manifest_names() {
    local file
    for file in "$MANIFEST_DIR"/*.txt; do
        [ -f "$file" ] && basename "$file" .txt
    done
    return 0
}

require_manifest() {
    [ -f "$(manifest_file "$1")" ] || die "no manifest '$1' in $MANIFEST_DIR (known: $(manifest_names | tr '\n' ' '))"
}

# Prints the values of kind $2 (source, include or define) in manifest $1.
manifest_values() {
    sed -n "s/^$2 \(..*\)$/\1/p" "$(manifest_file "$1")"
}
