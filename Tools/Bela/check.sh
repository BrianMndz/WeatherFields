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
checks every source of every manifest CMake writes (wf_add_manifest), each with
its manifest's include directories and defines. Files given explicitly are
checked with the flags of the chosen manifests.

Options:
  --manifest NAME  use this manifest (repeatable; default: all of them)
  --no-configure   use the manifests as they are, without configuring the
                   '$WF_PRESET' CMake preset first
  --std=STD      C++ standard for project code (default: $PROJECT_CXX_STD; or set BELA_CXX_STD)
  --host-clang   use the Mac's clang instead of Clang $CLANG_VERSION: still catches missing
                 library features, but accepts language features the board rejects
  --self-test    check that the toolchain rejects known board-incompatible code
  -DNAME[=VALUE] define a macro for project code, e.g. -DWF_PROFILE=1
  -v, --verbose  also print warnings for files that pass
  -h, --help     show this help

Environment:
  BELA_CHECKOUT  local Bela checkout whose headers to use until the board's are
                 pulled with Tools/Bela/pull-board-headers.sh
  WF_PRESET      CMake preset whose build tree holds the manifests (default: dev)
EOF
}

use_host_clang=0
self_test=0
verbose=0
configure=1
manifests=()
files=()
while [ $# -gt 0 ]; do
    case $1 in
        --manifest)
            [ $# -gt 1 ] || die "--manifest needs a name"
            manifests+=("$2")
            shift
            ;;
        --manifest=*) manifests+=("${1#--manifest=}") ;;
        --no-configure) configure=0 ;;
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
bela_includes=(-I"$bela_root/include/legacy" -I"$bela_root/include" -I"$bela_root")

# Base flags; compile() appends the project's flags, which come last as
# CPPFLAGS do on the board.
CXX_FLAGS=(
    "${target_flags[@]}" "${cxx_stdlib_flags[@]}" "${bela_includes[@]}"
    ${xenomai_flags[@]+"${xenomai_flags[@]}"} "${BELA_COMMON_FLAGS[@]}" -std="$BELA_CXX_DEFAULT_STD"
    "${BELA_RULE_FLAGS[@]}"
)
C_FLAGS=(
    "${target_flags[@]}" "${bela_includes[@]}"
    ${xenomai_flags[@]+"${xenomai_flags[@]}"} "${BELA_COMMON_FLAGS[@]}" -std="$BELA_C_DEFAULT_STD"
    "${BELA_RULE_FLAGS[@]}"
)

# Project flags of the manifests named in "$@": their defines and include
# directories, then the command line's defines. C files get them without the
# C++ standard.
project_c_flags=()
project_cxx_flags=()
set_project_flags() {
    local name value
    project_c_flags=()
    for name in "$@"; do
        while IFS= read -r value; do
            project_c_flags+=(-D"$value")
        done < <(manifest_values "$name" define)
        while IFS= read -r value; do
            project_c_flags+=(-I"$value")
        done < <(manifest_values "$name" include)
    done
    project_c_flags+=(${PROJECT_DEFINES[@]+"${PROJECT_DEFINES[@]}"})
    project_cxx_flags=(-std="$PROJECT_CXX_STD" ${project_c_flags[@]+"${project_c_flags[@]}"})
}

# Runs the matching compiler on file $1 with the extra flags that follow it.
compile() {
    local file=$1
    shift
    case $file in
        *.c) "${CC[@]}" "${C_FLAGS[@]}" ${project_c_flags[@]+"${project_c_flags[@]}"} "$@" -x c "$file" ;;
        *)   "${CXX[@]}" "${CXX_FLAGS[@]}" "${project_cxx_flags[@]}" "$@" -x c++ "$file" ;;
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
    if [ ${#manifests[@]} -gt 0 ]; then
        echo "  manifests ${manifests[*]} (${MANIFEST_DIR#$REPO_ROOT/})"
    fi
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
    project_cxx_flags=(-std="$PROJECT_CXX_STD")
    run_self_test
    exit 0
fi

# --- Project check ------------------------------------------------------------------

[ $configure -eq 0 ] || refresh_manifests
if [ ${#manifests[@]} -eq 0 ]; then
    while IFS= read -r name; do
        manifests+=("$name")
    done < <(manifest_names)
    [ ${#manifests[@]} -gt 0 ] || die "no manifests in $MANIFEST_DIR"
fi
for name in "${manifests[@]}"; do
    require_manifest "$name"
done

print_header "Bela compile check"

checked=0
failed=0
check_files() {
    local file out warnings
    for file in "$@"; do
        [ -f "$file" ] || die "no such file: $file"
        checked=$((checked + 1))
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
}

if [ ${#files[@]} -gt 0 ]; then
    set_project_flags "${manifests[@]}"
    check_files "${files[@]}"
else
    for name in "${manifests[@]}"; do
        sources=()
        while IFS= read -r file; do
            sources+=("$file")
        done < <(manifest_values "$name" source)
        [ ${#sources[@]} -gt 0 ] || die "manifest '$name' lists no sources"
        echo "Manifest $name:"
        set_project_flags "$name"
        check_files "${sources[@]}"
        echo
    done
fi

if [ $checked -eq 1 ]; then noun=file; else noun=files; fi
[ ${#files[@]} -eq 0 ] || echo
if [ $failed -eq 0 ]; then
    echo "$checked $noun checked; all would compile on the board."
else
    echo "$checked $noun checked; $failed would fail to compile on the board."
    exit 1
fi
