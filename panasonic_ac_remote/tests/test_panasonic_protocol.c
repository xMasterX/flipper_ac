#include "panasonic_ir_protocol.h"
#include <stdio.h>

static int fails;

static uint8_t ref_sum(const uint8_t* b) {
    uint8_t s = 0;
    for(int i = 0; i < 26; i++) s += b[i];
    return s;
}

// Decode both sections, checking each has its own header.
static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    size_t i = 0;
    int idx = 0;
    for(int sec = 0; sec < 2; sec++) {
        int nbytes = sec == 0 ? 8 : 19;
        if(t[i++] != 3456 || t[i++] != 1728) { printf("bad header sec%d\n", sec); return 0; }
        for(int byte = 0; byte < nbytes; byte++) {
            uint8_t v = 0;
            for(int b = 0; b < 8; b++) {
                if(t[i++] != 432) { printf("bad bit mark\n"); return 0; }
                uint32_t sp = t[i++];
                if(sp == 1296) v |= 1 << b;
                else if(sp != 432) { printf("bad space %u\n", sp); return 0; }
            }
            st[idx++] = v;
        }
        if(sec == 0) {
            if(t[i++] != 432 || t[i++] != 10000) { printf("bad section gap\n"); return 0; }
        }
    }
    if(t[i++] != 432) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    return 1;
}

int main(void) {
    uint32_t t[PANASONIC_IR_MAX_TIMINGS];
    size_t n = 0;
    uint8_t st[27];

    // The library's known-good state; its stored checksum is stale (recomputed
    // before sending), so we only check the fixed bytes survive.
    uint8_t good[27] = {0x02,0x20,0xE0,0x04,0x00,0x00,0x00,0x06,0x02,0x20,0xE0,0x04,0x00,0x00,
                        0x00,0x80,0x00,0x00,0x00,0x0E,0xE0,0x00,0x00,0x81,0x00,0x00,0x00};
    printf("known-good state: recomputed sum = %02X, stored = %02X (stale, ignored)\n",
           ref_sum(good), good[26]);

    int checked = 0;
    for(int m = PanasonicModeCool; m < PanasonicModeCount; m++) {
        for(int f = 0; f < PanasonicFanCount; f++) {
            for(int temp = PANASONIC_TEMP_MIN; temp <= PANASONIC_TEMP_MAX; temp++) {
                PanasonicRequest r = {(PanasonicMode)m, (PanasonicFan)f, (uint8_t)temp, 0, 0};
                if(!panasonic_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 439) { printf("FAIL len %zu != 439\n", n); fails++; continue; }
                if(!decode(t, n, st)) { fails++; continue; }

                // Fixed header/model bytes must be untouched
                int fixed[] = {0,1,2,3,7,8,9,10,11,15,19,20,23};
                for(size_t k = 0; k < sizeof(fixed)/sizeof(fixed[0]); k++) {
                    if(st[fixed[k]] != good[fixed[k]]) {
                        printf("FAIL fixed byte %d changed\n", fixed[k]); fails++; break;
                    }
                }
                if((st[13] & 1) != 1) { printf("FAIL power bit\n"); fails++; }
                unsigned want_t = (m == PanasonicModeFan) ? 27u : (unsigned)temp;
                if(((st[14] >> 1) & 0x1F) != want_t) {
                    printf("FAIL temp m=%d T=%d got %u\n", m, temp, (st[14] >> 1) & 0x1F); fails++;
                }
                if(st[26] != ref_sum(st)) { printf("FAIL checksum\n"); fails++; }
                checked++;
            }
        }
    }
    printf("state frames verified: %d\n", checked);

    struct { PanasonicMode m; unsigned want; const char* nm; } modes[] = {
        {PanasonicModeAuto, 0, "auto"}, {PanasonicModeDry, 2, "dry"}, {PanasonicModeCool, 3, "cool"},
        {PanasonicModeHeat, 4, "heat"}, {PanasonicModeFan, 6, "fan"}};
    for(size_t k = 0; k < 5; k++) {
        PanasonicRequest r = {modes[k].m, PanasonicFanAuto, 24, 0, 0};
        panasonic_ir_encode_state(&r, t, &n); decode(t, n, st);
        unsigned got = st[13] >> 4;
        printf("mode %-5s: got %u want %u  %s\n", modes[k].nm, got, modes[k].want,
               got == modes[k].want ? "ok" : "FAIL");
        if(got != modes[k].want) fails++;
    }

    // Fan is stored as speed + 3
    unsigned fanwant[4] = {7 + 3, 1 + 3, 2 + 3, 3 + 3};
    for(int f = 0; f < PanasonicFanCount; f++) {
        PanasonicRequest r = {PanasonicModeCool, (PanasonicFan)f, 24, 0, 0};
        panasonic_ir_encode_state(&r, t, &n); decode(t, n, st);
        if((st[16] >> 4) != fanwant[f]) {
            printf("FAIL fan %d: got %u want %u\n", f, st[16] >> 4, fanwant[f]); fails++;
        }
    }
    printf("fan codes ok (stored as speed + 3)\n");

    // Quiet and Powerful are mutually exclusive; Powerful must win
    PanasonicRequest r = {PanasonicModeCool, PanasonicFanAuto, 24, 0, 0};
    r.toggle_bits = 1u << PanasonicToggleQuiet;
    panasonic_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[21] & 1) != 1) { printf("FAIL quiet bit\n"); fails++; }
    r.toggle_bits = 1u << PanasonicTogglePowerful;
    panasonic_ir_encode_state(&r, t, &n); decode(t, n, st);
    if(((st[21] >> 5) & 1) != 1) { printf("FAIL powerful bit\n"); fails++; }
    r.toggle_bits = (1u << PanasonicToggleQuiet) | (1u << PanasonicTogglePowerful);
    panasonic_ir_encode_state(&r, t, &n); decode(t, n, st);
    if(((st[21] >> 5) & 1) != 1 || (st[21] & 1) != 0) {
        printf("FAIL powerful should win over quiet (byte21=%02X)\n", st[21]); fails++;
    }
    printf("quiet/powerful exclusivity ok\n");

    // Swing: auto (0xF) on, middle (0x3) off
    r.toggle_bits = 0;
    panasonic_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[16] & 0x0F) != 0x3) { printf("FAIL swingV off\n"); fails++; }
    r.toggle_bits = 1u << PanasonicToggleSwing;
    panasonic_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[16] & 0x0F) != 0xF) { printf("FAIL swingV auto\n"); fails++; }
    printf("swing field ok\n");

    r.toggle_bits = 0;
    if(panasonic_ir_encode_toggle(&r, PanasonicTogglePowerOff, t, &n) && decode(t, n, st)) {
        if(st[13] & 1) { printf("FAIL power-off bit\n"); fails++; }
        if(st[26] != ref_sum(st)) { printf("FAIL power-off checksum\n"); fails++; }
        printf("power-off frame ok\n");
    } else fails++;

    for(int e = 0; e < PanasonicExtraCount; e++) {
        if(!panasonic_ir_encode_extra(&r, (PanasonicExtra)e, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(st[26] != ref_sum(st)) { printf("FAIL extra checksum e=%d\n", e); fails++; }
    }
    printf("vane presets ok\n");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
