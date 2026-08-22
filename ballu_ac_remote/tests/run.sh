#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -I.. -o "$OUT/test" test_ballu_protocol.c ../ballu_ir_protocol.c
"$OUT/test"
