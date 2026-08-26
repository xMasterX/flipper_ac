#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -I.. -o "$OUT/test" test_kelvinator_protocol.c ../kelvinator_ir_protocol.c
"$OUT/test"
