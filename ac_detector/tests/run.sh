#!/usr/bin/env bash
# Host-side test. Links the detector against the protocol encoders from the
# fifteen remote apps in this workspace, so the waveforms under test are the
# ones that were checked on real hardware.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

W=../..
APPS=(ballu carrier coolix daikin delonghi fujitsu gree haier kelon kelvinator
      lg midea mitsubishi mitsubishi_heavy neoclima panasonic samsung tcl
      toshiba)

INCS=(-I..)
SRCS=(test_ac_decode.c ../ac_decode.c ../ac_protocol_db.c)
for a in "${APPS[@]}"; do
    INCS+=("-I$W/${a}_ac_remote")
    SRCS+=("$W/${a}_ac_remote/${a}_ir_protocol.c")
done

OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -O1 "${INCS[@]}" -o "$OUT/test" "${SRCS[@]}"
"$OUT/test"
