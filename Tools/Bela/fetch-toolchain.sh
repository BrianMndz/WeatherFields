#!/usr/bin/env bash
# Downloads, verifies and unpacks what check.sh needs: the macOS build of
# Clang 3.9 and the Debian 9 armhf C and C++ headers. Safe to re-run.
set -euo pipefail
source "$(dirname "$0")/common.sh"

require_clang39_host
mkdir -p "$DOWNLOAD_DIR"

# Downloads $1 to $2 unless a file with SHA-256 $3 is already there.
download_verified() {
    local url=$1 dest=$2 expected=$3 actual
    if [ -f "$dest" ] && [ "$(sha256_of "$dest")" = "$expected" ]; then
        echo "  cached    $(basename "$dest")"
        return
    fi
    echo "  fetching  $(basename "$dest")"
    curl -fL --retry 3 --progress-bar -o "$dest.part" "$url"
    actual=$(sha256_of "$dest.part")
    if [ "$actual" != "$expected" ]; then
        rm -f "$dest.part"
        die "checksum mismatch for $url (expected $expected, got $actual)"
    fi
    mv "$dest.part" "$dest"
}

echo "Clang $CLANG_VERSION"
if [ "$(cat "$CLANG_DIR/.sha256" 2>/dev/null)" = "$CLANG_SHA256" ]; then
    echo "  installed $CLANG_DIR"
else
    download_verified "$CLANG_URL" "$DOWNLOAD_DIR/$CLANG_ARCHIVE" "$CLANG_SHA256"
    staging="$CLANG_DIR.partial"
    rm -rf "$staging"
    mkdir -p "$staging"
    top=${CLANG_ARCHIVE%.tar.xz}
    # Only the driver and its builtin headers (stddef.h, arm_neon.h, ...) are needed.
    tar -xJf "$DOWNLOAD_DIR/$CLANG_ARCHIVE" -C "$staging" --strip-components 1 \
        "$top/bin/clang-${CLANG_VERSION%.*}" "$top/bin/clang" "$top/bin/clang++" "$top/lib/clang"
    echo "$CLANG_SHA256" > "$staging/.sha256"
    rm -rf "$CLANG_DIR"
    mv "$staging" "$CLANG_DIR"
    echo "  installed $CLANG_DIR"
fi

echo "Debian 9 armhf headers"
stamp=$(printf '%s\n' "${DEBIAN_PACKAGES[@]}")
if [ "$(cat "$DEBIAN_SYSROOT/.packages" 2>/dev/null)" = "$stamp" ]; then
    echo "  installed $DEBIAN_SYSROOT"
else
    staging="$DEBIAN_SYSROOT.partial"
    unpack="$DOWNLOAD_DIR/unpack"
    rm -rf "$staging"
    mkdir -p "$staging"
    for entry in "${DEBIAN_PACKAGES[@]}"; do
        path=${entry% *}
        deb="$DOWNLOAD_DIR/$(basename "$path")"
        download_verified "$DEBIAN_MIRROR/$path" "$deb" "${entry#* }"
        # A .deb is an ar archive whose installed files are in data.tar.*.
        rm -rf "$unpack"
        mkdir -p "$unpack"
        (cd "$unpack" && ar x "$deb")
        tar -xf "$unpack"/data.tar.* -C "$staging"
    done
    rm -rf "$unpack"
    printf '%s\n' "$stamp" > "$staging/.packages"
    rm -rf "$DEBIAN_SYSROOT"
    mv "$staging" "$DEBIAN_SYSROOT"
    echo "  installed $DEBIAN_SYSROOT"
fi

echo "Running Clang $CLANG_VERSION:"
$(clang39_prefix) "$CLANG_DIR/bin/clang" --version | head -1 | sed 's/^/  /'
echo "Next: Tools/Bela/check.sh --self-test"
