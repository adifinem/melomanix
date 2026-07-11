#!/bin/bash
# SessionStart hook for Claude Code on the web: installs the JUCE Linux
# build dependencies plus headless-UI tooling (Xvfb, screenshot, input
# simulation), then configures and pre-builds the test + Standalone
# targets so the agent can verify changes immediately. The container is
# cached after the first run, so subsequent sessions skip the slow parts.
set -euo pipefail

# Local (CLI/desktop) sessions manage their own environment.
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

export DEBIAN_FRONTEND=noninteractive

# apt update can exit nonzero if an unrelated PPA is unreachable through
# the proxy; the Ubuntu main repos still refresh, so don't fail on it.
sudo apt-get update -qq || true

sudo apt-get install -y -qq \
  libasound2-dev libjack-jackd2-dev libfontconfig1-dev libfreetype-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev \
  xvfb x11-apps imagemagick xdotool

cd "$CLAUDE_PROJECT_DIR"

# Configure fetches JUCE via FetchContent (network through the proxy).
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMELOMANIX_COPY_PLUGIN=OFF

# Pre-build the two targets used for verification: headless engine tests
# and the Standalone app (run under Xvfb for UI screenshots).
cmake --build build --target MelomanixTests Melomanix_Standalone -j"$(nproc)"

echo "session-start: Melomanix build environment ready"
