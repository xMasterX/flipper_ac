#include "ballu_ir_protocol.h"
#include <stdio.h>

static int fails;

static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    size_t i = 0;
    if(t[i++] != 9000 || t[i++] != 4500) { printf("bad header\n"); return 0; }
    for(int byte = 0; byte < 13; byte++) {
        uint8_t v = 0;
        for(int b = 0; b < 8; b++) {
            if(t[i++] != 575) { printf("bad bit mark\n"); return 0; }
            uint32_t sp = t[i++];
            if(sp == 1675) v |= 1 << b;
            else if(sp != 550) { printf("bad space %u\n", sp); return 0; }
        }
        st[byte] = v;
    }
    if(t[i++] != 575) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    return 1;
}

static uint8_t ref_sum(const uint8_t* b) {
    uint8_t s = 0;
    for(int i = 0; i < 12; i++) s += b[i];
    return s;
}

int main(void) {
    uint32_t t[BALLU_IR_MAX_TIMINGS];
    size_t n = 0;
    uint8_t st[13];

    int checked = 0;
    for(int m = BalluModeCool; m < BalluModeCount; m++) {
        for(int f = 0; f < BalluFanCount; f++) {
            for(int temp = BALLU_TEMP_MIN; temp <= BALLU_TEMP_MAX; temp++) {
                BalluRequest r = {(BalluMode)m, (BalluFan)f, (uint8_t)temp, 0, 0};
                if(!ballu_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 211) { printf("FAIL len %zu != 211\n", n); fails++; continue; }
                if(!decode(t, n, st)) { fails++; continue; }

                if(st[0] != 0xC3) { printf("FAIL byte0\n"); fails++; }
                if(st[11] != 0x1E) { printf("FAIL byte11\n"); fails++; }
                if(st[9] != 0x20) { printf("FAIL power byte\n"); fails++; }
                if((st[1] >> 3) != (unsigned)(temp - 8)) { printf("FAIL temp %d\n", temp); fails++; }
                if(st[12] != ref_sum(st)) { printf("FAIL checksum\n"); fails++; }
                checked++;
            }
        }
    }
    printf("state frames verified: %d\n", checked);

    struct { BalluMode m; unsigned want; const char* nm; } modes[] = {
        {BalluModeAuto, 0x00, "auto"}, {BalluModeCool, 0x20, "cool"}, {BalluModeDry, 0x40, "dry"},
        {BalluModeHeat, 0x80, "heat"}, {BalluModeFan, 0xC0, "fan"}};
    for(size_t k = 0; k < 5; k++) {
        BalluRequest r = {modes[k].m, BalluFanAuto, 24, 0, 0};
        ballu_ir_encode_state(&r, t, &n); decode(t, n, st);
        printf("mode %-5s: got %02X want %02X  %s\n", modes[k].nm, st[6], modes[k].want,
               st[6] == modes[k].want ? "ok" : "FAIL");
        if(st[6] != modes[k].want) fails++;
    }

    unsigned fanwant[4] = {0xA0, 0x60, 0x40, 0x20};
    for(int f = 0; f < BalluFanCount; f++) {
        BalluRequest r = {BalluModeCool, (BalluFan)f, 24, 0, 0};
        ballu_ir_encode_state(&r, t, &n); decode(t, n, st);
        if(st[4] != fanwant[f]) { printf("FAIL fan %d: got %02X want %02X\n", f, st[4], fanwant[f]); fails++; }
    }
    printf("fan codes ok\n");

    // The swing fields are inverted: bits SET means not swinging.
    BalluRequest r = {BalluModeCool, BalluFanAuto, 24, 0, 0};
    ballu_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[1] & 0x07) != 0x07) { printf("FAIL swingV-off bits\n"); fails++; }
    if(st[2] != 0xE0) { printf("FAIL swingH-off bits\n"); fails++; }
    r.toggle_bits = (1u << BalluToggleSwingV) | (1u << BalluToggleSwingH);
    ballu_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[1] & 0x07) != 0) { printf("FAIL swingV-on bits\n"); fails++; }
    if(st[2] != 0) { printf("FAIL swingH-on bits\n"); fails++; }
    printf("inverted swing fields ok\n");

    r.toggle_bits = 0;
    if(ballu_ir_encode_toggle(&r, BalluTogglePowerOff, t, &n) && decode(t, n, st)) {
        if(st[9] != 0) { printf("FAIL power-off byte\n"); fails++; }
        if(st[12] != ref_sum(st)) { printf("FAIL power-off checksum\n"); fails++; }
        printf("power-off frame ok\n");
    } else fails++;

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
