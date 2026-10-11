#!/bin/bash
# Compatibility entry point: the image builder supplies sector padding.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
make build/bootloader/layout.inc
