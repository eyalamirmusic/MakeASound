#!/usr/bin/env bash
# Builds the test suite and runs the system output capture cases. Usage: scripts/test-system-output-capture.sh [host]
# With a host, runs on it over ssh in ~/projects/MakeASound at this checkout's branch,
# which must already be pushed.
set -euo pipefail

if [[ $# -ge 1 ]]; then
    host=$1
    branch=$(git rev-parse --abbrev-ref HEAD)
    ssh "$host" "bash -lc 'set -e; mkdir -p ~/projects; cd ~/projects;
        [ -d MakeASound ] || git clone https://github.com/eyalamirmusic/MakeASound.git;
        cd MakeASound; git fetch origin; git checkout -B $branch origin/$branch;
        scripts/test-system-output-capture.sh'"
    exit
fi

cd "$(dirname "$0")/.."
cmake -S . -B build-system-capture -G Ninja -DCMAKE_BUILD_TYPE=Release -DMAKEASOUND_BUILD_APPS=OFF \
    -DMAKEASOUND_BUILD_EXAMPLES=OFF
cmake --build build-system-capture --target MakeASoundTests
ctest --test-dir build-system-capture -R SystemOutputCapture --output-on-failure -V
