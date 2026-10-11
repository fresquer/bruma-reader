#!/usr/bin/env bash
# Run from any directory. The simulated SD defaults to the ignored fs_/ tree.
set -euo pipefail
bruma_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$bruma_root"
if [[ "${1:-}" == "--no-build" ]]; then
  shift
else
  if command -v pio >/dev/null 2>&1; then
    bruma_pio="$(command -v pio)"
  elif [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
    bruma_pio="$HOME/.platformio/penv/bin/pio"
  elif [[ -x "$bruma_root/.venv/bin/pio" ]]; then
    bruma_pio="$bruma_root/.venv/bin/pio"
  else
    echo "PlatformIO is required to build the simulator. See agent-docs/simulator.md." >&2
    exit 1
  fi
  "$bruma_pio" run -e simulator
fi
mkdir -p fs_
exec "$bruma_root/.pio/build/simulator/program" "$@"
