#!/usr/bin/env bash
# Measures the Bela build's callback cost at several block sizes. Builds the
# profile project (deploy.sh --profile), runs it once per block size, and
# prints each run's report (Apps/Bela/Profiler.h) followed by the audio
# thread's line from /proc/xenomai/sched/stat, sampled while it ran.
set -euo pipefail
source "$(dirname "$0")/common.sh"

# Still CloudsEngine's script: it stages Apps/Bela and reads source lists that
# no longer exist. It is adapted with the Bela host (PROJECT_GUIDE.md, next
# actions), to stage a Bela app from its manifest.
die "$(basename "$0") is not adapted to WeatherFields yet; it comes with the Bela host"

PROJECT_NAME="${BELA_PROJECT_NAME:-CloudsEngineProfile}"
BOARD_PROJECT_DIR="$BBB_BELA_HOME/projects/$PROJECT_NAME"

usage() {
    cat <<EOF
Usage: Tools/Bela/profile.sh [options] [period...]

Builds $PROJECT_NAME with -DCLOUDS_PROFILE=1 and runs it on $BBB_ADDRESS
once per period (Bela's block size in frames; default: 32, the app's
default). Each run takes about 5 minutes, with the audio output muted. To
keep the results: Tools/Bela/profile.sh | tee profile.txt

Options:
  --no-build   run the program already on the board
  -h, --help   show this help
EOF
}

build=1
periods=()
while [ $# -gt 0 ]; do
    case $1 in
        --no-build) build=0 ;;
        -h|--help) usage; exit 0 ;;
        -*) usage >&2; die "unknown option: $1" ;;
        *) periods+=("$1") ;;
    esac
    shift
done
[ ${#periods[@]} -gt 0 ] || periods=(32)

# Bela runs one program at a time; this never stops one it did not start.
if ssh "$BBB_ADDRESS" "grep -q bela-audio /proc/xenomai/sched/stat"; then
    die "a Bela program is running on the board; stop it first (Tools/Bela/deploy.sh --stop)"
fi

if [ $build -eq 1 ]; then
    "$TOOLS_DIR/deploy.sh" --profile
    echo
fi

for period in "${periods[@]}"; do
    echo "=== Block size $period"
    # Samples the audio thread's Xenomai statistics every two seconds while
    # the program runs and keeps the last sample, then prints the report.
    ssh "$BBB_ADDRESS" "cd $BOARD_PROJECT_DIR && \
        ./$PROJECT_NAME -p $period > /tmp/clouds-profile.log 2>&1 & pid=\$!; \
        header=\$(head -1 /proc/xenomai/sched/stat); last=''; \
        while kill -0 \$pid 2>/dev/null; do \
            line=\$(grep bela-audio /proc/xenomai/sched/stat || true); \
            [ -z \"\$line\" ] || last=\$line; sleep 2; \
        done; \
        cat /tmp/clouds-profile.log; \
        echo; echo 'Xenomai, last sample while running (MSW: mode switches; 1 is the one at start):'; \
        echo \"\$header\"; echo \"\$last\""
    echo
done
