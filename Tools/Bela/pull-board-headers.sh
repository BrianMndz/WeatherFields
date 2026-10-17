#!/usr/bin/env bash
# Copies the board's own headers and build facts so check.sh matches the board
# exactly instead of approximating it with Debian archive packages: /usr/include,
# the Xenomai headers and xeno-config flags, Bela's include and libraries folders,
# and Bela's Makefile. Re-run after updating the board's image or Bela core.
#
# Board address and Bela location use Bela's own variables, for example:
#   BBB_ADDRESS=root@bela.local Tools/Bela/pull-board-headers.sh
set -euo pipefail
source "$(dirname "$0")/common.sh"

bela_rel=${BBB_BELA_HOME#/}
staging="$BOARD_DIR.partial"
rm -rf "$staging"
mkdir -p "$staging"

echo "Copying headers from $BBB_ADDRESS (Bela at $BBB_BELA_HOME)..."
# Only paths that exist are archived, so an absent optional folder is not an error.
ssh "$BBB_ADDRESS" "cd / && tar -cf - \$(ls -d usr/include usr/xenomai/include \
    $bela_rel/include $bela_rel/libraries $bela_rel/Makefile 2>/dev/null)" \
    | tar -xf - -C "$staging"

[ -d "$staging/usr/include/c++/$LIBSTDCXX_MAJOR" ] \
    || die "the board has no /usr/include/c++/$LIBSTDCXX_MAJOR; update LIBSTDCXX_MAJOR in common.sh"
[ -f "$staging/$bela_rel/include/Bela.h" ] \
    || die "no Bela.h under $BBB_BELA_HOME/include on the board; set BBB_BELA_HOME"

echo "Reading compiler and Xenomai facts..."
# Bela's Makefile drops these two flags from xeno-config's output as well.
ssh "$BBB_ADDRESS" "/usr/xenomai/bin/xeno-config --skin=posix --cflags" \
    | tr ' ' '\n' | grep -v -x -e '' -e '-no-pie' -e '-fno-pie' \
    > "$staging/xenomai-cflags.txt"

ssh "$BBB_ADDRESS" sh -s > "$staging/board-info.txt" <<EOF
echo "debian:   \$(cat /etc/debian_version)"
echo "clang:    \$(clang --version 2>/dev/null | head -1)"
echo "gcc:      \$(gcc --version 2>/dev/null | head -1)"
echo "bela git: \$(git -C $BBB_BELA_HOME rev-parse --short HEAD 2>/dev/null || echo unknown)"
EOF
date '+%Y-%m-%d %H:%M' > "$staging/pulled-at"

rm -rf "$BOARD_DIR"
mv "$staging" "$BOARD_DIR"

echo
sed 's/^/  /' "$BOARD_DIR/board-info.txt"
echo "  xenomai:  $(tr '\n' ' ' < "$BOARD_DIR/xenomai-cflags.txt")"
echo
echo "Flags in the board's Bela Makefile (compare with BELA_COMMON_FLAGS in common.sh):"
grep -E '^(ARCH_FLAGS|DEFAULT_COMMON_FLAGS|DEFAULT_CPPFLAGS) *[:?]?=' "$BOARD_DIR/$bela_rel/Makefile" | sed 's/^/  /'
echo
echo "Saved to $BOARD_DIR; check.sh now uses the board's headers."
