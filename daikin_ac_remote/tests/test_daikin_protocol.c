#include "daikin_ir_protocol.h"
#include <stdio.h>

static int fails;

// IRDaikinESP::checksum(): three independent sums, one per section.
static int checksums_ok(const uint8_t* st) {
    uint8_t s = 0;
    for(int i = 0; i < 7; i++) s += st[i];
    if(st[7] != s) { printf("FAIL sum1 %02X vs %02X\n", st[7], s); return 0; }
    s = 0;
    for(int i = 8; i < 15; i++) s += st[i];
    if(st[15] != s) { printf("FAIL sum2 %02X vs %02X\n", st[15], s); return 0; }
    s = 0;
    for(int i = 16; i < 34; i++) s += st[i];
    if(st[34] != s) { printf("FAIL sum3 %02X vs %02X\n", st[34], s); return 0; }
    return 1;
}

static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    size_t i = 0;
    // 5-bit zero preamble, no header
    for(int b = 0; b < 5; b++) {
        if(t[i++] != 428) { printf("bad preamble mark\n"); return 0; }
        if(t[i++] != 428) { printf("FAIL preamble bit %d is not zero\n", b); return 0; }
    }
    if(t[i++] != 428 || t[i++] != 428 + 29000) { printf("bad preamble footer\n"); return 0; }

    int idx = 0;
    const int seclen[3] = {8, 8, 19};
    for(int sec = 0; sec < 3; sec++) {
        if(t[i++] != 3650 || t[i++] != 1623) { printf("bad header sec%d\n", sec); return 0; }
        for(int byte = 0; byte < seclen[sec]; byte++) {
            uint8_t v = 0;
            for(int b = 0; b < 8; b++) {
                if(t[i++] != 428) { printf("bad bit mark\n"); return 0; }
                uint32_t sp = t[i++];
                if(sp == 1280) v |= 1 << b;
                else if(sp != 428) { printf("bad space %u\n", sp); return 0; }
            }
            st[idx++] = v;
        }
        if(sec < 2) {
            if(t[i++] != 428 || t[i++] != 428 + 29000) { printf("bad section gap %d\n", sec); return 0; }
        }
    }
    if(t[i++] != 428) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    return 1;
}

int main(void) {
    uint32_t t[DAIKIN_IR_MAX_TIMINGS];
    size_t n = 0;
    uint8_t st[35];

    int checked = 0;
    for(int m = DaikinModeCool; m < DaikinModeCount; m++) {
        for(int f = 0; f < DaikinFanCount; f++) {
            for(int temp = DAIKIN_TEMP_MIN; temp <= DAIKIN_TEMP_MAX; temp++) {
                DaikinRequest r = {(DaikinMode)m, (DaikinFan)f, (uint8_t)temp, 0, 0};
                if(!daikin_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 583) { printf("FAIL len %zu != 583\n", n); fails++; continue; }
                if(!decode(t, n, st)) { fails++; continue; }

                // The three section preambles must all survive
                if(st[0] != 0x11 || st[1] != 0xDA || st[2] != 0x27) { printf("FAIL sec1 preamble\n"); fails++; }
                if(st[8] != 0x11 || st[9] != 0xDA || st[10] != 0x27) { printf("FAIL sec2 preamble\n"); fails++; }
                if(st[16] != 0x11 || st[17] != 0xDA || st[18] != 0x27) { printf("FAIL sec3 preamble\n"); fails++; }
                if((st[21] & 1) != 1) { printf("FAIL power bit\n"); fails++; }
                if(((st[21] >> 3) & 1) != 1) { printf("FAIL always-1 bit\n"); fails++; }
                if(st[22] != (unsigned)(temp * 2)) { printf("FAIL temp %d -> %u\n", temp, st[22]); fails++; }
                if(!checksums_ok(st)) fails++;
                checked++;
            }
        }
    }
    printf("state frames verified: %d\n", checked);

    struct { DaikinMode m; unsigned want; const char* nm; } modes[] = {
        {DaikinModeAuto, 0b000, "auto"}, {DaikinModeDry, 0b010, "dry"},
        {DaikinModeCool, 0b011, "cool"}, {DaikinModeHeat, 0b100, "heat"},
        {DaikinModeFan, 0b110, "fan"}};
    for(size_t k = 0; k < 5; k++) {
        DaikinRequest r = {modes[k].m, DaikinFanAuto, 24, 0, 0};
        daikin_ir_encode_state(&r, t, &n); decode(t, n, st);
        unsigned got = (st[21] >> 4) & 7;
        printf("mode %-5s: got %u want %u  %s\n", modes[k].nm, got, modes[k].want,
               got == modes[k].want ? "ok" : "FAIL");
        if(got != modes[k].want) fails++;
    }

    // Fan speeds 1..5 are stored as 2 + speed; auto is 0b1010
    unsigned fanwant[4] = {0b1010, 3, 5, 7};
    for(int f = 0; f < DaikinFanCount; f++) {
        DaikinRequest r = {DaikinModeCool, (DaikinFan)f, 24, 0, 0};
        daikin_ir_encode_state(&r, t, &n); decode(t, n, st);
        if((st[24] >> 4) != fanwant[f]) {
            printf("FAIL fan %d: got %u want %u\n", f, st[24] >> 4, fanwant[f]); fails++;
        }
    }
    printf("fan codes ok (auto 0b1010, speeds stored as 2 + speed)\n");

    // Quiet also rewrites the fan nibble to the quiet code
    DaikinRequest r = {DaikinModeCool, DaikinFanHigh, 24, 1u << DaikinToggleQuiet, 0};
    daikin_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[24] >> 4) != 0b1011) { printf("FAIL quiet fan code\n"); fails++; }
    if(((st[29] >> 5) & 1) != 1) { printf("FAIL quiet bit\n"); fails++; }
    printf("quiet overrides the fan nibble: ok\n");

    struct { DaikinToggle tg; int byte; int bit; const char* nm; } bits[] = {
        {DaikinTogglePowerful, 29, 0, "powerful"}, {DaikinToggleEcono, 32, 2, "econo"},
        {DaikinToggleMold, 33, 1, "mold"}};
    for(size_t k = 0; k < 3; k++) {
        DaikinRequest rr = {DaikinModeCool, DaikinFanAuto, 24, 1u << bits[k].tg, 0};
        if(!daikin_ir_encode_state(&rr, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(!((st[bits[k].byte] >> bits[k].bit) & 1)) { printf("FAIL %s bit\n", bits[k].nm); fails++; }
        if(!checksums_ok(st)) { printf("  (with %s)\n", bits[k].nm); fails++; }
    }
    printf("feature bits ok\n");

    // Vane: 0xF on, 0x0 off
    r.toggle_bits = 1u << DaikinToggleSwing;
    daikin_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[24] & 0x0F) != 0xF) { printf("FAIL swingV on\n"); fails++; }
    r.toggle_bits = 0;
    daikin_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[24] & 0x0F) != 0x0) { printf("FAIL swingV off\n"); fails++; }
    printf("swing field ok\n");

    if(daikin_ir_encode_toggle(&r, DaikinTogglePowerOff, t, &n) && decode(t, n, st)) {
        if(st[21] & 1) { printf("FAIL power-off bit\n"); fails++; }
        if(!checksums_ok(st)) fails++;
        printf("power-off frame ok\n");
    } else fails++;

    for(int e = 0; e < DaikinExtraCount; e++) {
        if(!daikin_ir_encode_extra(&r, (DaikinExtra)e, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(!checksums_ok(st)) { printf("  (extra %d)\n", e); fails++; }
    }
    printf("extra frames ok\n");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
