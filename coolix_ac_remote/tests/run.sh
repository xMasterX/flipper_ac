#!/usr/bin/env bash
# Host-side test for the protocol encoder. Runs on the Mac with plain cc -
# coolix_ir_protocol.c deliberately pulls in nothing from the Flipper SDK, so
# it compiles and runs natively. Catch encoding bugs here, not on the device.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -I.. -o "$OUT/test" test_coolix_protocol.c ../coolix_ir_protocol.c
"$OUT/test"

# Regenerate Coolix_AC.ir from the same encoder
cc -std=gnu11 -Wall -Wextra -Werror -I.. -o "$OUT/gen" gen_ir.c ../coolix_ir_protocol.c
"$OUT/gen" ../Coolix_AC.ir
echo "regenerated ../Coolix_AC.ir ($(grep -c '^name:' ../Coolix_AC.ir) signals)"
