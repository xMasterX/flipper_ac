#include "coolix_ir_protocol.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, uint32_t got, uint32_t want) {
    int ok = got == want;
    if(!ok) fails++;
    printf("%-28s got 0x%06X want 0x%06X  %s\n", what, got, want, ok ? "ok" : "FAIL");
}

// Independent decoder: walk the timings back into a 24-bit word, verifying
// structure and the inverted-byte copies as we go.
static int decode(const uint32_t* t, size_t n, uint32_t* out_first, uint32_t* out_second) {
    size_t i = 0;
    uint32_t frames[2];
    for(int f = 0; f < 2; f++) {
        if(t[i++] != 8 * 560 || t[i++] != 8 * 560) { printf("bad leader f%d\n", f); return 0; }
        uint32_t data = 0;
        for(int b = 0; b < 3; b++) {
            uint8_t byte = 0;
            for(int k = 0; k < 8; k++) {
                if(t[i++] != 560) { printf("bad mark\n"); return 0; }
                uint32_t sp = t[i++];
                if(sp == 3 * 560) byte = (byte << 1) | 1;
                else if(sp == 560) byte = (byte << 1);
                else { printf("bad space %u\n", sp); return 0; }
            }
            for(int k = 0; k < 8; k++) {
                if(t[i++] != 560) { printf("bad inv mark\n"); return 0; }
                uint32_t sp = t[i++];
                int bit = (byte >> (7 - k)) & 1;
                uint32_t want = bit ? 560 : 3 * 560;
                if(sp != want) { printf("inverted copy mismatch byte %d bit %d\n", b, k); return 0; }
            }
            data = (data << 8) | byte;
        }
        if(t[i++] != 560) { printf("bad stop f%d\n", f); return 0; }
        frames[f] = data;
        if(f == 0) {
            if(t[i++] != 10 * 560) { printf("bad gap\n"); return 0; }
        }
    }
    if(i != n) { printf("length mismatch: consumed %zu of %zu\n", i, n); return 0; }
    *out_first = frames[0];
    *out_second = frames[1];
    return 1;
}

int main(void) {
    // State words, cross-checked against ESPHome coolix.cpp / IRremoteESP8266
    check("Auto 25C fan-auto (default)", coolix_ir_build_state(CoolixModeAuto, CoolixFanAuto, 25), 0xB21FC8);
    check("Off",                          coolix_ir_build_state(CoolixModeOff, CoolixFanAuto, 25), 0xB27BE0);
    check("Cool 17C fan-auto",            coolix_ir_build_state(CoolixModeCool, CoolixFanAuto, 17), 0xB2BF00);
    check("Cool 30C fan-high",            coolix_ir_build_state(CoolixModeCool, CoolixFanHigh, 30), 0xB23FB0);
    check("Heat 24C fan-low",             coolix_ir_build_state(CoolixModeHeat, CoolixFanLow, 24),  0xB29F4C);
    check("Dry 22C (fan forced)",         coolix_ir_build_state(CoolixModeDry, CoolixFanHigh, 22),  0xB21F74);
    check("Fan-only med",                 coolix_ir_build_state(CoolixModeFan, CoolixFanMedium, 24),0xB25FE4);
    check("temp clamp low",               coolix_ir_build_state(CoolixModeCool, CoolixFanAuto, 5),  coolix_ir_build_state(CoolixModeCool, CoolixFanAuto, 17));
    check("temp clamp high",              coolix_ir_build_state(CoolixModeCool, CoolixFanAuto, 99), coolix_ir_build_state(CoolixModeCool, CoolixFanAuto, 30));

    check("toggle Swing",  coolix_ir_get_toggle_code(CoolixToggleSwing),  0xB26BE0);
    check("toggle Direct", coolix_ir_get_toggle_code(CoolixToggleDirect), 0xB20FE0);
    check("toggle Turbo",  coolix_ir_get_toggle_code(CoolixToggleTurbo),  0xB5F5A2);
    check("toggle LED",    coolix_ir_get_toggle_code(CoolixToggleLed),    0xB5F5A5);
    check("toggle Sleep",  coolix_ir_get_toggle_code(CoolixToggleSleep),  0xB2E003);
    check("extra Silence", coolix_ir_get_extra_code(CoolixExtraSilence),  0xB5F5B6);
    check("extra Clean",   coolix_ir_get_extra_code(CoolixExtraClean),    0xB5F5AA);

    // Round-trip every reachable state word through the wire format
    uint32_t t[COOLIX_IR_MAX_TIMINGS];
    size_t n = 0;
    int round_trips = 0;
    for(int m = CoolixModeCool; m < CoolixModeCount; m++) {
        for(int f = 0; f < CoolixFanCount; f++) {
            for(int temp = COOLIX_TEMP_MIN; temp <= COOLIX_TEMP_MAX; temp++) {
                memset(t, 0, sizeof(t));
                if(!coolix_ir_encode_state(m, f, temp, t, &n)) { printf("encode failed\n"); fails++; continue; }
                if(n != 199) { printf("FAIL length %zu != 199\n", n); fails++; continue; }
                uint32_t a = 0, b = 0;
                if(!decode(t, n, &a, &b)) { fails++; continue; }
                uint32_t want = coolix_ir_build_state(m, f, temp);
                if(a != want || b != want) {
                    printf("FAIL round-trip m=%d f=%d T=%d: 0x%06X/0x%06X vs 0x%06X\n", m, f, temp, a, b, want);
                    fails++;
                }
                round_trips++;
            }
        }
    }
    printf("\nround-trips verified: %d\n", round_trips);
    printf("encode_state(Off) rejected: %s\n",
           coolix_ir_encode_state(CoolixModeOff, CoolixFanAuto, 24, t, &n) ? "FAIL" : "ok");
    if(coolix_ir_encode_state(CoolixModeOff, CoolixFanAuto, 24, t, &n)) fails++;

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
