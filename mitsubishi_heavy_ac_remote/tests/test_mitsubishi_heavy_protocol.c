#include "mitsubishi_heavy_ir_protocol.h"
#include <stdio.h>

static int fails;

// IRremoteESP8266's checkInvertedBytePairs, over raw[4..18].
static int inverted_pairs_ok(const uint8_t* st) {
    for(int i = 4; i < 19; i += 2) {
        if(st[i] != (uint8_t)~st[i - 1]) {
            printf("FAIL pair %d/%d: %02X vs ~%02X\n", i - 1, i, st[i], st[i - 1]);
            return 0;
        }
    }
    return 1;
}

static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    size_t i = 0;
    if(t[i++] != 3140 || t[i++] != 1630) { printf("bad header\n"); return 0; }
    for(int byte = 0; byte < 19; byte++) {
        uint8_t v = 0;
        for(int b = 0; b < 8; b++) {
            if(t[i++] != 370) { printf("bad bit mark\n"); return 0; }
            uint32_t sp = t[i++];
            // Reversed: a one is the SHORT space here
            if(sp == 420) v |= 1 << b;
            else if(sp != 1220) { printf("bad space %u\n", sp); return 0; }
        }
        st[byte] = v;
    }
    if(t[i++] != 370) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    return 1;
}

int main(void) {
    uint32_t t[MITSUBISHI_HEAVY_IR_MAX_TIMINGS];
    size_t n = 0;
    uint8_t st[19];

    // The signature's own last two bytes are already an inverted pair.
    uint8_t sig[5] = {0xAD, 0x51, 0x3C, 0xE5, 0x1A};
    printf("signature E5/1A is an inverted pair: %s\n",
           (uint8_t)~sig[3] == sig[4] ? "ok" : "FAIL");
    if((uint8_t)~sig[3] != sig[4]) fails++;

    int checked = 0;
    for(int m = MitsubishiHeavyModeCool; m < MitsubishiHeavyModeCount; m++) {
        for(int f = 0; f < MitsubishiHeavyFanCount; f++) {
            for(int temp = MITSUBISHI_HEAVY_TEMP_MIN; temp <= MITSUBISHI_HEAVY_TEMP_MAX; temp++) {
                MitsubishiHeavyRequest r = {
                    (MitsubishiHeavyMode)m, (MitsubishiHeavyFan)f, (uint8_t)temp, 0, 0};
                if(!mitsubishi_heavy_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 307) { printf("FAIL len %zu != 307\n", n); fails++; continue; }
                if(!decode(t, n, st)) { fails++; continue; }

                for(int k = 0; k < 5; k++)
                    if(st[k] != sig[k]) { printf("FAIL signature byte %d\n", k); fails++; break; }
                if(((st[5] >> 3) & 1) != 1) { printf("FAIL power bit\n"); fails++; }
                if((st[7] & 0x0F) != (unsigned)(temp - 17)) { printf("FAIL temp %d\n", temp); fails++; }
                if(st[17] != 0x80) { printf("FAIL byte17\n"); fails++; }
                if(!inverted_pairs_ok(st)) fails++;
                checked++;
            }
        }
    }
    printf("state frames verified: %d\n", checked);

    struct { MitsubishiHeavyMode m; unsigned want; const char* nm; } modes[] = {
        {MitsubishiHeavyModeAuto, 0, "auto"}, {MitsubishiHeavyModeCool, 1, "cool"},
        {MitsubishiHeavyModeDry, 2, "dry"}, {MitsubishiHeavyModeFan, 3, "fan"},
        {MitsubishiHeavyModeHeat, 4, "heat"}};
    for(size_t k = 0; k < 5; k++) {
        MitsubishiHeavyRequest r = {modes[k].m, MitsubishiHeavyFanAuto, 24, 0, 0};
        mitsubishi_heavy_ir_encode_state(&r, t, &n); decode(t, n, st);
        unsigned got = st[5] & 7;
        printf("mode %-5s: got %u want %u  %s\n", modes[k].nm, got, modes[k].want,
               got == modes[k].want ? "ok" : "FAIL");
        if(got != modes[k].want) fails++;
    }

    for(int f = 0; f < MitsubishiHeavyFanCount; f++) {
        MitsubishiHeavyRequest r = {MitsubishiHeavyModeCool, (MitsubishiHeavyFan)f, 24, 0, 0};
        mitsubishi_heavy_ir_encode_state(&r, t, &n); decode(t, n, st);
        if((st[9] & 0x0F) != (unsigned)f) { printf("FAIL fan %d\n", f); fails++; }
    }
    printf("fan codes ok\n");

    struct { MitsubishiHeavyToggle tg; int byte; int bit; const char* nm; } bits[] = {
        {MitsubishiHeavyToggleClean, 5, 5, "clean"}, {MitsubishiHeavyToggleFilter, 5, 6, "filter"},
        {MitsubishiHeavyToggleNight, 15, 6, "night"}, {MitsubishiHeavyToggleSilent, 15, 7, "silent"}};
    for(size_t k = 0; k < 4; k++) {
        MitsubishiHeavyRequest r = {
            MitsubishiHeavyModeCool, MitsubishiHeavyFanAuto, 24, 1u << bits[k].tg, 0};
        if(!mitsubishi_heavy_ir_encode_state(&r, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(!((st[bits[k].byte] >> bits[k].bit) & 1)) { printf("FAIL %s bit\n", bits[k].nm); fails++; }
        if(!inverted_pairs_ok(st)) { printf("  (with %s)\n", bits[k].nm); fails++; }
    }
    printf("feature bits ok\n");

    // Swing: auto (0) when on, off (6) when off
    MitsubishiHeavyRequest r = {MitsubishiHeavyModeCool, MitsubishiHeavyFanAuto, 24, 0, 0};
    mitsubishi_heavy_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[11] >> 5) != 6) { printf("FAIL swingV off\n"); fails++; }
    r.toggle_bits = 1u << MitsubishiHeavyToggleSwing;
    mitsubishi_heavy_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[11] >> 5) != 0) { printf("FAIL swingV auto\n"); fails++; }
    printf("swing field ok\n");

    r.toggle_bits = 0;
    if(mitsubishi_heavy_ir_encode_toggle(&r, MitsubishiHeavyTogglePowerOff, t, &n) && decode(t, n, st)) {
        if((st[5] >> 3) & 1) { printf("FAIL power-off bit\n"); fails++; }
        if(!inverted_pairs_ok(st)) fails++;
        printf("power-off frame ok\n");
    } else fails++;

    for(int e = 0; e < MitsubishiHeavyExtraCount; e++) {
        if(!mitsubishi_heavy_ir_encode_extra(&r, (MitsubishiHeavyExtra)e, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(!inverted_pairs_ok(st)) { printf("  (extra %d)\n", e); fails++; }
    }
    printf("extra frames ok\n");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
