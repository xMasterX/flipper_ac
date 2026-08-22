#include "haier_ir_protocol.h"
#include <stdio.h>

static int fails;

static uint8_t ref_sum(const uint8_t* b) {
    uint8_t s = 0;
    for(int i = 0; i < 13; i++) s += b[i];
    return s;
}

static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    size_t i = 0;
    if(t[i++] != 3000 || t[i++] != 3000) { printf("bad lead-in pair\n"); return 0; }
    if(t[i++] != 3000 || t[i++] != 4300) { printf("bad header\n"); return 0; }
    for(int byte = 0; byte < 14; byte++) {
        uint8_t v = 0;
        for(int b = 0; b < 8; b++) {
            if(t[i++] != 520) { printf("bad bit mark\n"); return 0; }
            uint32_t sp = t[i++];
            if(sp == 1650) v = (v << 1) | 1;
            else if(sp == 650) v = (v << 1);
            else { printf("bad space %u\n", sp); return 0; }
        }
        st[byte] = v;
    }
    if(t[i++] != 520) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    return 1;
}

int main(void) {
    uint32_t t[HAIER_IR_MAX_TIMINGS];
    size_t n = 0;
    uint8_t st[14];

    int checked = 0;
    for(int m = HaierModeCool; m < HaierModeCount; m++) {
        for(int f = 0; f < HaierFanCount; f++) {
            for(int temp = HAIER_TEMP_MIN; temp <= HAIER_TEMP_MAX; temp++) {
                HaierRequest r = {(HaierMode)m, (HaierFan)f, (uint8_t)temp, 0, 0};
                if(!haier_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 229) { printf("FAIL len %zu != 229\n", n); fails++; continue; }
                if(!decode(t, n, st)) { fails++; continue; }

                if(st[0] != 0xA6) { printf("FAIL model byte\n"); fails++; }
                if(((st[4] >> 6) & 1) != 1) { printf("FAIL power bit\n"); fails++; }
                if((st[1] >> 4) != (unsigned)(temp - 16)) { printf("FAIL temp %d\n", temp); fails++; }
                if((st[12] & 0x1F) != 0b00110) { printf("FAIL button field\n"); fails++; }
                if(st[13] != ref_sum(st)) { printf("FAIL checksum\n"); fails++; }
                checked++;
            }
        }
    }
    printf("state frames verified: %d\n", checked);

    struct { HaierMode m; unsigned want; const char* nm; } modes[] = {
        {HaierModeAuto, 0b000, "auto"}, {HaierModeCool, 0b001, "cool"}, {HaierModeDry, 0b010, "dry"},
        {HaierModeHeat, 0b100, "heat"}, {HaierModeFan, 0b110, "fan"}};
    for(size_t k = 0; k < 5; k++) {
        HaierRequest r = {modes[k].m, HaierFanAuto, 24, 0, 0};
        haier_ir_encode_state(&r, t, &n); decode(t, n, st);
        unsigned got = st[7] >> 5;
        printf("mode %-5s: got %u want %u  %s\n", modes[k].nm, got, modes[k].want,
               got == modes[k].want ? "ok" : "FAIL");
        if(got != modes[k].want) fails++;
    }

    unsigned fanwant[4] = {0b101, 0b011, 0b010, 0b001};
    for(int f = 0; f < HaierFanCount; f++) {
        HaierRequest r = {HaierModeCool, (HaierFan)f, 24, 0, 0};
        haier_ir_encode_state(&r, t, &n); decode(t, n, st);
        if((st[5] >> 5) != fanwant[f]) {
            printf("FAIL fan %d: got %u want %u\n", f, st[5] >> 5, fanwant[f]); fails++;
        }
    }
    printf("fan codes ok (auto 5, low 3, med 2, high 1)\n");

    struct { HaierToggle tg; int byte; int bit; const char* nm; } bits[] = {
        {HaierToggleHealth, 3, 1, "health"}, {HaierToggleTurbo, 6, 6, "turbo"},
        {HaierToggleQuiet, 6, 7, "quiet"}, {HaierToggleSleep, 8, 7, "sleep"}};
    for(size_t k = 0; k < 4; k++) {
        HaierRequest r = {HaierModeCool, HaierFanAuto, 24, 1u << bits[k].tg, 0};
        if(!haier_ir_encode_state(&r, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(!((st[bits[k].byte] >> bits[k].bit) & 1)) { printf("FAIL %s bit\n", bits[k].nm); fails++; }
        if(st[13] != ref_sum(st)) { printf("FAIL checksum with %s\n", bits[k].nm); fails++; }
    }
    printf("feature bits ok\n");

    // SwingV: auto (0xC) when on, off (0x0) when off
    HaierRequest r = {HaierModeCool, HaierFanAuto, 24, 0, 0};
    haier_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[1] & 0x0F) != 0x0) { printf("FAIL swingV off\n"); fails++; }
    r.toggle_bits = 1u << HaierToggleSwing;
    haier_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[1] & 0x0F) != 0xC) { printf("FAIL swingV auto\n"); fails++; }
    printf("swing field ok\n");

    r.toggle_bits = 0;
    if(haier_ir_encode_toggle(&r, HaierTogglePowerOff, t, &n) && decode(t, n, st)) {
        if((st[4] >> 6) & 1) { printf("FAIL power-off bit\n"); fails++; }
        if((st[12] & 0x1F) != 0b00101) { printf("FAIL power button code\n"); fails++; }
        if(st[13] != ref_sum(st)) { printf("FAIL power-off checksum\n"); fails++; }
        printf("power-off frame ok\n");
    } else fails++;

    for(int e = 0; e < HaierExtraCount; e++) {
        if(!haier_ir_encode_extra(&r, (HaierExtra)e, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(st[13] != ref_sum(st)) { printf("FAIL extra checksum e=%d\n", e); fails++; }
    }
    printf("extra frames ok\n");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
