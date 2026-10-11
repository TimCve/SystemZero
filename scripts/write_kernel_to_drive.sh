#!/bin/bash
# Compatibility entry point: update the kernel through the checked image builder.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
make all
