// Rebuilds the 48-bit word from the timings and checks each field against
// IRremoteESP8266's KelonProtocol union, transcribed independently.

#include "kelon_ir_protocol.h"
#include <stdio.h>
#include <string.h>

static int fails;

#define FAILF(...)           \
    do {                     \
        printf("  FAIL: ");  \
        printf(__VA_ARGS__); \
        printf("\n");        \
        fails++;             \
    } while(0)

static int decode(const uint32_t* t, size_t n, uint64_t* raw) {
    if(n < 4 || t[0] != 9000 || t[1] != 4600) {
        FAILF("bad header");
        return 0;
    }
    size_t i = 2;
    uint64_t v = 0;
    for(int b = 0; b < 48; b++) {
        if(i + 1 >= n) {
            FAILF("ran out of timings at bit %d", b);
            return 0;
        }
        if(t[i] != 560) {
            FAILF("bad bit mark %u at bit %d", t[i], b);
            return 0;
        }
        uint32_t sp = t[i + 1];
        if(sp == 1680) {
            v |= 1ULL << b; // least significant bit first
        } else if(sp != 600) {
            FAILF("bad space %u at bit %d", sp, b);
            return 0;
        }
        i += 2;
    }
    if(i != n - 1 || t[i] != 560) {
        FAILF("expected a trailing bit mark, stopped at %zu of %zu", i, n);
        return 0;
    }
    *raw = v;
    return 1;
}

static uint64_t field(uint64_t raw, int pos, int width) {
    return (raw >> pos) & ((1ULL << width) - 1);
}

static void check(KelonMode mode, KelonFan fan, uint8_t temp, uint32_t toggles) {
    KelonRequest req = {mode, fan, temp, toggles, 0};
    uint32_t t[KELON_IR_MAX_TIMINGS];
    size_t n = 0;

    if(!kelon_ir_encode_state(&req, t, &n)) {
        FAILF("encode failed for mode %d fan %d temp %u", mode, fan, temp);
        return;
    }
    uint64_t raw = 0;
    if(!decode(t, n, &raw)) return;

    if(field(raw, 0, 8) != 0x83) FAILF("preamble byte 0 is %02X", (unsigned)field(raw, 0, 8));
    if(field(raw, 8, 8) != 0x06) FAILF("preamble byte 1 is %02X", (unsigned)field(raw, 8, 8));

    static const uint8_t MODE_WIRE[KelonModeCount] = {2, 2, 1, 3, 0, 4};
    if(field(raw, 24, 3) != MODE_WIRE[mode])
        FAILF("mode %d encoded as %u", mode, (unsigned)field(raw, 24, 3));

    // IRKelonAc::setFan maps 0,1..3 to 0,3..1 - the wire runs backwards.
    static const uint8_t FAN_WIRE[KelonFanCount] = {0, 3, 2, 1};
    if(field(raw, 16, 2) != FAN_WIRE[fan])
        FAILF("fan %d encoded as %u, wanted %u", fan, (unsigned)field(raw, 16, 2), FAN_WIRE[fan]);

    uint8_t want_temp = temp;
    if(mode == KelonModeSmart) want_temp = 26;
    if(mode == KelonModeDry || mode == KelonModeFan) want_temp = 25;
    if(want_temp < 18) want_temp = 18;
    if(want_temp > 32) want_temp = 32;
    if(field(raw, 28, 4) != (uint64_t)(want_temp - 18))
        FAILF("temp %u in mode %d encoded as %u", temp, mode, (unsigned)field(raw, 28, 4) + 18);

    if(field(raw, 39, 1) != (mode == KelonModeSmart ? 1u : 0u)) FAILF("smart bit wrong");

    // A settings change must never flip the power.
    if(field(raw, 18, 1)) FAILF("power toggle set on a plain state frame");

    if(field(raw, 19, 1) != (uint64_t)((toggles >> KelonToggleSleep) & 1)) FAILF("sleep bit wrong");

    bool want_super = ((toggles >> KelonToggleSuperCool) & 1) && mode == KelonModeCool;
    if(field(raw, 44, 1) != (uint64_t)want_super) FAILF("super cool copy 1 wrong");
    if(field(raw, 47, 1) != (uint64_t)want_super) FAILF("super cool copy 2 wrong");
}

static void test_toggles(void) {
    KelonRequest req = {KelonModeCool, KelonFanLow, 24, 0, 0};
    uint32_t t[KELON_IR_MAX_TIMINGS];
    size_t n = 0;
    uint64_t raw = 0;

    if(!kelon_ir_encode_toggle(&req, KelonTogglePowerOff, t, &n) || !decode(t, n, &raw)) {
        FAILF("power toggle encode failed");
    } else {
        if(!field(raw, 18, 1)) FAILF("power toggle bit not set");
        if(field(raw, 23, 1)) FAILF("swing toggle set on a power press");
    }

    if(!kelon_ir_encode_toggle(&req, KelonToggleSwingV, t, &n) || !decode(t, n, &raw)) {
        FAILF("swing toggle encode failed");
    } else {
        if(!field(raw, 23, 1)) FAILF("swing toggle bit not set");
        if(field(raw, 18, 1)) FAILF("power toggle set on a swing press");
    }

    if(!kelon_ir_toggle_is_momentary(KelonTogglePowerOff)) FAILF("power should be momentary");
    if(!kelon_ir_toggle_is_momentary(KelonToggleSwingV)) FAILF("swing should be momentary");
    if(kelon_ir_toggle_is_momentary(KelonToggleSleep)) FAILF("sleep should latch");
}

static void test_extras(void) {
    KelonRequest req = {KelonModeDry, KelonFanLow, 24, 0, 0};
    for(int e = 0; e < KelonExtraCount; e++) {
        uint32_t t[KELON_IR_MAX_TIMINGS];
        size_t n = 0;
        uint64_t raw = 0;
        if(!kelon_ir_encode_extra(&req, (KelonExtra)e, t, &n) || !decode(t, n, &raw)) {
            FAILF("extra %d encode failed", e);
            continue;
        }
        if(field(raw, 20, 3) != (uint64_t)e)
            FAILF("extra %d set grade %u", e, (unsigned)field(raw, 20, 3));
    }
}

int main(void) {
    printf("kelon protocol\n");

    for(int m = KelonModeCool; m < KelonModeCount; m++) {
        for(int f = 0; f < KelonFanCount; f++) {
            for(uint8_t temp = 18; temp <= 32; temp++) {
                check((KelonMode)m, (KelonFan)f, temp, 0);
            }
        }
    }
    for(int t = KelonToggleSwingV; t < KelonToggleCount; t++) {
        check(KelonModeCool, KelonFanLow, 24, 1u << t);
        check(KelonModeHeat, KelonFanHigh, 28, 1u << t);
    }

    test_toggles();
    test_extras();

    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ok", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
