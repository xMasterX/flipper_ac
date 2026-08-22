#include "neoclima_ir_protocol.h"
#include <stdio.h>

static int fails;

static uint8_t ref_sum(const uint8_t* b) {
    uint8_t s = 0;
    for(int i = 0; i < 11; i++) s += b[i];
    return s;
}

static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    size_t i = 0;
    if(t[i++] != 6112 || t[i++] != 7391) { printf("bad header\n"); return 0; }
    for(int byte = 0; byte < 12; byte++) {
        uint8_t v = 0;
        for(int b = 0; b < 8; b++) {
            if(t[i++] != 537) { printf("bad bit mark\n"); return 0; }
            uint32_t sp = t[i++];
            if(sp == 1651) v |= 1 << b;
            else if(sp != 571) { printf("bad space %u\n", sp); return 0; }
        }
        st[byte] = v;
    }
    if(t[i++] != 537 || t[i++] != 7391) { printf("bad footer\n"); return 0; }
    if(t[i++] != 537) { printf("bad extra mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    return 1;
}

int main(void) {
    uint32_t t[NEOCLIMA_IR_MAX_TIMINGS];
    size_t n = 0;
    uint8_t st[12];

    // IRNeoclimaAc::stateReset literal, byte 11 recomputed (the library's is stale).
    uint8_t reset[12] = {0,0,0,0,0,0,0,0x6A,0,0x2A,0xA5,0};
    reset[11] = ref_sum(reset);
    printf("library reset: byte7=%02X byte9=%02X byte10=%02X sum=%02X\n",
           reset[7], reset[9], reset[10], reset[11]);
    printf("  power bit set: %s\n", ((reset[7] >> 1) & 1) ? "ok" : "FAIL");
    printf("  swingV field = %u (2 = off)  %s\n", (reset[7] >> 2) & 3,
           ((reset[7] >> 2) & 3) == 2 ? "ok" : "FAIL");
    printf("  temp %u -> %uC, mode %u\n", reset[9] & 0x1F, (reset[9] & 0x1F) + 16, reset[9] >> 5);
    if(!((reset[7] >> 1) & 1)) fails++;
    if(((reset[7] >> 2) & 3) != 2) fails++;

    int checked = 0;
    for(int m = NeoclimaModeCool; m < NeoclimaModeCount; m++) {
        for(int f = 0; f < NeoclimaFanCount; f++) {
            for(int temp = NEOCLIMA_TEMP_MIN; temp <= NEOCLIMA_TEMP_MAX; temp++) {
                NeoclimaRequest r = {(NeoclimaMode)m, (NeoclimaFan)f, (uint8_t)temp, 0, 0};
                if(!neoclima_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 197) { printf("FAIL len %zu != 197\n", n); fails++; continue; }
                if(!decode(t, n, st)) { fails++; continue; }

                if(st[10] != 0xA5) { printf("FAIL byte10 marker\n"); fails++; }
                if(((st[7] >> 1) & 1) != 1) { printf("FAIL power bit\n"); fails++; }
                if((st[9] & 0x1F) != (unsigned)(temp - 16)) { printf("FAIL temp %d\n", temp); fails++; }
                if((st[5] & 0x1F) != 0x01) { printf("FAIL button field\n"); fails++; }
                if(st[11] != ref_sum(st)) { printf("FAIL checksum\n"); fails++; }
                checked++;
            }
        }
    }
    printf("state frames verified: %d\n", checked);

    struct { NeoclimaMode m; unsigned want; const char* nm; } modes[] = {
        {NeoclimaModeAuto, 0, "auto"}, {NeoclimaModeCool, 1, "cool"}, {NeoclimaModeDry, 2, "dry"},
        {NeoclimaModeFan, 3, "fan"}, {NeoclimaModeHeat, 4, "heat"}};
    for(size_t k = 0; k < 5; k++) {
        NeoclimaRequest r = {modes[k].m, NeoclimaFanAuto, 24, 0, 0};
        neoclima_ir_encode_state(&r, t, &n); decode(t, n, st);
        unsigned got = st[9] >> 5;
        printf("mode %-5s: got %u want %u  %s\n", modes[k].nm, got, modes[k].want,
               got == modes[k].want ? "ok" : "FAIL");
        if(got != modes[k].want) fails++;
    }

    unsigned fanwant[4] = {0, 3, 2, 1};
    for(int f = 0; f < NeoclimaFanCount; f++) {
        NeoclimaRequest r = {NeoclimaModeCool, (NeoclimaFan)f, 24, 0, 0};
        neoclima_ir_encode_state(&r, t, &n); decode(t, n, st);
        if(((st[7] >> 5) & 3) != fanwant[f]) { printf("FAIL fan %d\n", f); fails++; }
    }
    printf("fan codes ok (auto 0, low 3, med 2, high 1)\n");

    // Feature bits at their union positions
    struct { NeoclimaToggle tg; int byte; int bit; const char* nm; } bits[] = {
        {NeoclimaToggleIon, 1, 2, "ion"}, {NeoclimaToggleLight, 3, 0, "light"},
        {NeoclimaToggleTurbo, 3, 3, "turbo"}, {NeoclimaToggleEcono, 3, 4, "econo"}};
    for(size_t k = 0; k < 4; k++) {
        NeoclimaRequest r = {NeoclimaModeCool, NeoclimaFanAuto, 24, 1u << bits[k].tg, 0};
        if(!neoclima_ir_encode_state(&r, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(!((st[bits[k].byte] >> bits[k].bit) & 1)) { printf("FAIL %s bit\n", bits[k].nm); fails++; }
        if(st[11] != ref_sum(st)) { printf("FAIL checksum with %s\n", bits[k].nm); fails++; }
    }
    printf("feature bits ok\n");

    // Each button press stamps its own code into byte 5
    unsigned btnwant[6] = {0x00, 0x04, 0x0A, 0x0B, 0x0D, 0x14};
    NeoclimaRequest r = {NeoclimaModeCool, NeoclimaFanAuto, 24, 0, 0};
    for(int k = 0; k < NeoclimaToggleCount; k++) {
        if(!neoclima_ir_encode_toggle(&r, (NeoclimaToggle)k, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if((st[5] & 0x1F) != btnwant[k]) {
            printf("FAIL button code %d: got %02X want %02X\n", k, st[5] & 0x1F, btnwant[k]); fails++;
        }
    }
    printf("button codes ok\n");

    // Power off clears bit 1 of byte 7
    if(neoclima_ir_encode_toggle(&r, NeoclimaTogglePowerOff, t, &n) && decode(t, n, st)) {
        if((st[7] >> 1) & 1) { printf("FAIL power-off bit\n"); fails++; }
        if(st[11] != ref_sum(st)) { printf("FAIL power-off checksum\n"); fails++; }
        printf("power-off frame ok\n");
    } else fails++;

    for(int e = 0; e < NeoclimaExtraCount; e++) {
        if(!neoclima_ir_encode_extra(&r, (NeoclimaExtra)e, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(st[11] != ref_sum(st)) { printf("FAIL extra checksum e=%d\n", e); fails++; }
    }
    printf("extra frames ok\n");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
