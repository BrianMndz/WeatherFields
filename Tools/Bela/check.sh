#!/usr/bin/env bash
# Checks on the Mac whether the Bela build's sources will compile on the board,
# without sending anything to it. Each file is syntax-checked by the board's
# compiler version (Clang 3.9, run through Rosetta 2) against the board's
# Debian 9 armhf headers and Bela's headers, with the flags Bela's Makefile uses.
# Nothing is code-generated or linked, so link errors are out of scope.
set -euo pipefail
source "$(dirname "$0")/common.sh"

usage() {
    cat <<EOF
Usage: Tools/Bela/check.sh [options] [file...]

Syntax-checks sources the way the Bela board's compiler would. With no files,
checks everything in Apps/Bela plus the Engine and CloudsDSP library sources.

Options:
  --std=STD      C++ standard for project code (default: $PROJECT_CXX_STD; or set BELA_CXX_STD)
  --host-clang   use the Mac's clang instead of Clang $CLANG_VERSION: still catches missing
                 library features, but accepts language features the board rejects
  --self-test    check that the toolchain rejects known board-incompatible code
  -DNAME[=VALUE] define a macro for project code, e.g. -DCLOUDS_PROFILE=1
  -v, --verbose  also print warnings for files that pass
  -h, --help     show this help

Environment:
  BELA_CHECKOUT  local Bela checkout whose headers to use until the board's are
                 pulled with Tools/Bela/pull-board-headers.sh
EOF
}

use_host_clang=0
self_test=0
verbose=0
files=()
while [ $# -gt 0 ]; do
    case $1 in
        --std=*) PROJECT_CXX_STD=${1#--std=} ;;
        --host-clang) use_host_clang=1 ;;
        --self-test) self_test=1 ;;
        -v|--verbose) verbose=1 ;;
        -D*) PROJECT_DEFINES+=("$1") ;;
        -h|--help) usage; exit 0 ;;
        -*) usage >&2; die "unknown option: $1" ;;
        *) files+=("$1") ;;
    esac
    shift
done

# --- Toolchain ------------------------------------------------------------------

if [ $use_host_clang -eq 1 ]; then
    CXX=("${HOST_CXX:-clang++}")
    CC=("${HOST_CC:-clang}")
    compiler_desc="$("${CXX[@]}" --version | head -1) (host; not the board's language level)"
else
    require_clang39_host
    [ -x "$CLANG_DIR/bin/clang" ] || die "Clang $CLANG_VERSION is missing; run Tools/Bela/fetch-toolchain.sh"
    CXX=($(clang39_prefix) "$CLANG_DIR/bin/clang++")
    CC=($(clang39_prefix) "$CLANG_DIR/bin/clang")
    compiler_desc="Clang $CLANG_VERSION"
fi

if [ -d "$BOARD_DIR/usr/include" ]; then
    sysroot=$BOARD_DIR
    sysroot_desc="board, pulled $(cat "$BOARD_DIR/pulled-at")"
elif [ -d "$DEBIAN_SYSROOT/usr/include" ]; then
    sysroot=$DEBIAN_SYSROOT
    sysroot_desc="Debian 9 armhf packages (pull-board-headers.sh gives the board's own)"
else
    die "no target headers; run Tools/Bela/fetch-toolchain.sh"
fi

if [ -f "$BOARD_DIR$BBB_BELA_HOME/include/Bela.h" ]; then
    bela_root=$BOARD_DIR$BBB_BELA_HOME
    bela_desc="board"
elif [ -n "${BELA_CHECKOUT:-}" ]; then
    [ -f "$BELA_CHECKOUT/include/Bela.h" ] || die "no include/Bela.h under BELA_CHECKOUT=$BELA_CHECKOUT"
    bela_root=$BELA_CHECKOUT
    bela_desc="$BELA_CHECKOUT (may differ from the board's Bela version)"
else
    die "no Bela headers; run Tools/Bela/pull-board-headers.sh or set BELA_CHECKOUT to a local Bela checkout"
fi

# xeno-config's flags hold absolute board paths; point them at the pulled copy.
xenomai_flags=()
if [ -f "$BOARD_DIR/xenomai-cflags.txt" ]; then
    for flag in $(cat "$BOARD_DIR/xenomai-cflags.txt"); do
        case $flag in
            -I/*) flag="-I$BOARD_DIR${flag#-I}" ;;
        esac
        xenomai_flags+=("$flag")
    done
    xenomai_desc="board xeno-config"
else
    xenomai_desc="not included until the board's headers are pulled"
fi

# --- Flags, in the order of Bela's compile rule: INCLUDES, DEFAULT_CPPFLAGS,
# --- the rule's own flags, then the project's CPPFLAGS. -------------------------

libstdcxx="$sysroot/usr/include/c++/$LIBSTDCXX_MAJOR"
target_flags=(--target=arm-linux-gnueabihf "--sysroot=$sysroot")
# Without a full GCC installation in the sysroot, clang cannot discover
# libstdc++ by itself, so its three include directories are given explicitly.
cxx_stdlib_flags=(
    -nostdinc++
    -isystem "$libstdcxx"
    -isystem "$sysroot/usr/include/arm-linux-gnueabihf/c++/$LIBSTDCXX_MAJOR"
    -isystem "$libstdcxx/backward"
)
bela_includes=(-I"$REPO_ROOT/Apps/Bela" -I"$bela_root/include/legacy" -I"$bela_root/include" -I"$bela_root")
project_flags=(-std="$PROJECT_CXX_STD" "${PROJECT_DEFINES[@]}")
for dir in "${PROJECT_INCLUDE_DIRS[@]}"; do
    project_flags+=(-I"$REPO_ROOT/$dir")
done

CXX_FLAGS=(
    "${target_flags[@]}" "${cxx_stdlib_flags[@]}" "${bela_includes[@]}"
    ${xenomai_flags[@]+"${xenomai_flags[@]}"} "${BELA_COMMON_FLAGS[@]}" -std="$BELA_CXX_DEFAULT_STD"
    "${BELA_RULE_FLAGS[@]}" "${project_flags[@]}"
)
C_FLAGS=(
    "${target_flags[@]}" "${bela_includes[@]}"
    ${xenomai_flags[@]+"${xenomai_flags[@]}"} "${BELA_COMMON_FLAGS[@]}" -std="$BELA_C_DEFAULT_STD"
    "${BELA_RULE_FLAGS[@]}"
)

# Runs the matching compiler on file $1 with the extra flags that follow it.
compile() {
    local file=$1
    shift
    case $file in
        *.c) "${CC[@]}" "${C_FLAGS[@]}" "$@" -x c "$file" ;;
        *)   "${CXX[@]}" "${CXX_FLAGS[@]}" "$@" -x c++ "$file" ;;
    esac
}

relative() {
    echo "${1#$REPO_ROOT/}"
}

print_header() {
    echo "$1"
    echo "  compiler  $compiler_desc"
    echo "  headers   $sysroot_desc"
    echo "  Bela      $bela_desc"
    echo "  Xenomai   $xenomai_desc"
    echo "  standard  -std=$PROJECT_CXX_STD"
    echo
}

# --- Self-test --------------------------------------------------------------------

run_self_test() {
    print_header "Bela toolchain self-test"

    echo "What the compiler reports about itself and the headers it reads:"
    printf '#include <cstddef>\n#include <features.h>\n' \
        | "${CXX[@]}" "${CXX_FLAGS[@]}" -E -dM -x c++ - \
        | grep -E '^#define (__clang_version__|__GLIBCXX__|__GLIBC__|__GLIBC_MINOR__|__ARM_ARCH|__ARM_NEON|__ARM_PCS_VFP|__cplusplus) ' \
        | sed 's/^#define /  /'
    echo

    local failures=0 file name expected out compiled
    for file in "$TOOLS_DIR"/selftest/*.cpp; do
        name=$(basename "$file" .cpp)
        expected=$(sed -n 's|^// expect-error: ||p' "$file")
        if out=$(compile "$file" -fsyntax-only 2>&1); then compiled=1; else compiled=0; fi

        if [ -z "$expected" ] && [ $compiled -eq 1 ]; then
            echo "PASS $name (compiles)"
        elif [ -z "$expected" ]; then
            echo "FAIL $name: should compile but does not"
            echo "$out"
            failures=$((failures + 1))
        elif [ $compiled -eq 1 ]; then
            echo "FAIL $name: accepted, but the board's compiler rejects it"
            failures=$((failures + 1))
        elif grep -qF -- "$expected" <<<"$out"; then
            echo "PASS $name (rejected: $expected)"
        else
            echo "FAIL $name: rejected for a different reason than '$expected'"
            echo "$out"
            failures=$((failures + 1))
        fi
    done

    echo
    if [ $failures -eq 0 ]; then
        echo "Self-test passed."
    else
        echo "Self-test failed: $failures case(s)."
        exit 1
    fi
}

if [ $self_test -eq 1 ]; then
    run_self_test
    exit 0
fi

# --- Project check ------------------------------------------------------------------

if [ ${#files[@]} -eq 0 ]; then
    while IFS= read -r file; do
        files+=("$file")
    done < <(bela_sources)
    [ ${#files[@]} -gt 0 ] || die "found no sources to check"
fi

print_header "Bela compile check"

skipped=$(find "$REPO_ROOT/Apps/Bela" -type f \( -name '*.cc' -o -name '*.cxx' \))
if [ -n "$skipped" ]; then
    echo "warning: Bela's Makefile compiles only .cpp, .c and .S files, so it will skip:"
    echo "$skipped" | sed "s|^$REPO_ROOT/|  |"
    echo
fi

failed=0
for file in "${files[@]}"; do
    [ -f "$file" ] || die "no such file: $file"
    if out=$(compile "$file" -fsyntax-only 2>&1); then
        warnings=$(grep -c 'warning:' <<<"$out" || true)
        if [ "$warnings" -gt 0 ]; then
            echo "OK   $(relative "$file") ($warnings warnings)"
        else
            echo "OK   $(relative "$file")"
        fi
        if [ $verbose -eq 1 ] && [ -n "$out" ]; then
            echo "$out"
        fi
    else
        echo "FAIL $(relative "$file")"
        echo "$out"
        failed=$((failed + 1))
    fi
done

if [ ${#files[@]} -eq 1 ]; then noun=file; else noun=files; fi
echo
if [ $failed -eq 0 ]; then
    echo "${#files[@]} $noun checked; all would compile on the board."
else
    echo "${#files[@]} $noun checked; $failed would fail to compile on the board."
    exit 1
fi
