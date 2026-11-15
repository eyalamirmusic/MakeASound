#!/usr/bin/env bash
# Builds the test suite and runs the system output capture cases. Usage: scripts/test-system-output-capture.sh [host]
# With a host, runs on it over ssh in ~/projects/MakeASound at this checkout's branch,
# fetched from the remote the branch tracks, so push it first.
set -euo pipefail

if [[ $# -ge 1 ]]; then
    host=$1
    branch=$(git rev-parse --abbrev-ref HEAD)
    remote_url=$(git remote get-url "$(git config "branch.$branch.remote")")
    ssh "$host" "bash -lc 'set -e; mkdir -p ~/projects; cd ~/projects;
        [ -d MakeASound ] || git clone https://github.com/eyalamirmusic/MakeASound.git;
        cd MakeASound; git fetch $remote_url $branch; git checkout -B $branch FETCH_HEAD;
        scripts/test-system-output-capture.sh'"
    exit
fi

cd "$(dirname "$0")/.."
cmake -S . -B build-system-capture -G Ninja -DCMAKE_BUILD_TYPE=Release -DMAKEASOUND_BUILD_APPS=OFF \
    -DMAKEASOUND_BUILD_EXAMPLES=OFF
cmake --build build-system-capture --target MakeASoundTests
ctest --test-dir build-system-capture -R SystemOutputCapture --output-on-failure -V \
    -E recordsAToneThisProcessPlays

tests=build-system-capture/Tests/MakeASoundTests
tone_case=SystemOutputCapture/recordsAToneThisProcessPlays

if [[ $(uname) != Darwin ]]; then
    "$tests" --test "$tone_case"
    exit
fi

# macOS gives system audio recording to the process that is responsible for the
# recorder: a terminal's child records silence unless the terminal app holds the
# permission. Started by launchd, the test binary is its own responsible process and
# asks with the usage description embedded in it.
label=makeasound.system-output-capture
log=$(mktemp)
launchctl remove "$label" 2>/dev/null || true
launchctl submit -l "$label" -o "$log" -e "$log" -- "$PWD/$tests" --test "$tone_case"

while launchctl list "$label" 2>/dev/null | grep -q '"PID"'; do sleep 1; done

launchctl remove "$label"
cat "$log"
grep -q "1 passed" "$log"
