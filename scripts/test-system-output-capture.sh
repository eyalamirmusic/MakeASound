#!/usr/bin/env bash
# Builds MakeASoundSystemCaptureTests and runs the system output capture cases. Usage: scripts/test-system-output-capture.sh [host]
# With a host, runs on it over ssh in ~/projects/MakeASound-system-capture at this checkout's branch,
# fetched from the remote the branch tracks, so push it first.
set -euo pipefail

if [[ $# -ge 1 ]]; then
    host=$1
    branch=$(git rev-parse --abbrev-ref HEAD)
    remote_url=$(git remote get-url "$(git config "branch.$branch.remote")")
    ssh "$host" "bash -lc 'set -e; mkdir -p ~/projects; cd ~/projects;
        [ -d MakeASound-system-capture ] || git clone https://github.com/eyalamirmusic/MakeASound.git MakeASound-system-capture;
        cd MakeASound-system-capture; git fetch $remote_url $branch; git checkout -B $branch FETCH_HEAD;
        scripts/test-system-output-capture.sh'"
    exit
fi

cd "$(dirname "$0")/.."
cmake_args=()
build_type=Release

# A Linux box whose toolchain comes from nix has no system ALSA headers, and its
# binaries do not search /usr/lib, so ALSA and the PulseAudio client miniaudio loads
# come from nix too.
if [[ $(uname) == Linux && ! -e /usr/include/alsa/asoundlib.h ]] && command -v nix >/dev/null; then
    alsa_dev=$(nix build --no-link --print-out-paths nixpkgs#alsa-lib.dev)
    alsa_lib=$(nix build --no-link --print-out-paths nixpkgs#alsa-lib)
    pulse_lib=$(nix build --no-link --print-out-paths nixpkgs#libpulseaudio)
    cmake_args+=(-DALSA_INCLUDE_DIR="$alsa_dev/include"
                 -DALSA_LIBRARY="$alsa_lib/lib/libasound.so")
    # Release links with LTO, and nix's clang emits bitcode its GNU ld cannot read.
    build_type=Debug
    export LD_LIBRARY_PATH="$pulse_lib/lib:$alsa_lib/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

cmake -S . -B build-system-capture -G Ninja -DCMAKE_BUILD_TYPE=$build_type -DMAKEASOUND_BUILD_APPS=OFF \
    -DMAKEASOUND_BUILD_EXAMPLES=OFF -DMAKEASOUND_BUILD_PLUGIN=OFF \
    -DMAKEASOUND_BUILD_HOSTING=OFF ${cmake_args[@]+"${cmake_args[@]}"}
cmake --build build-system-capture --target MakeASoundSystemCaptureTests
ctest --test-dir build-system-capture -R SystemOutputCapture --output-on-failure -V \
    -E recordsAToneThisProcessPlays

tests=build-system-capture/Tests/MakeASoundSystemCaptureTests
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
