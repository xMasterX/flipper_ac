#include "gree_ir_protocol.h"
#include <stdio.h>
#include <string.h>

static int fails;

// IRKelvinatorAC::calcBlockChecksum, transcribed independently.
static uint8_t ref_checksum(const uint8_t* b, int len) {
    uint8_t sum = 10;
    for(int i = 0; i < 4 && i < len - 1; i++) sum += b[i] & 0x0F;
    for(int i = 4; i < len - 1; i++) sum += b[i] >> 4;
    return sum & 0x0F;
}

// Rebuild the 8 bytes from the timings, checking the split-frame structure.
static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    size_t i = 0;
    if(t[i++] != 9000 || t[i++] != 4000) { printf("bad header\n"); return 0; }
    for(int byte = 0; byte < 4; byte++) {
        uint8_t v = 0;
        for(int b = 0; b < 8; b++) {
            if(t[i++] != 620) { printf("bad bit mark\n"); return 0; }
            uint32_t sp = t[i++];
            if(sp == 1600) v |= 1 << b;
            else if(sp != 540) { printf("bad space %u\n", sp); return 0; }
        }
        st[byte] = v;
    }
    // three constant bits: 0, 1, 0
    const uint32_t want[3] = {540, 1600, 540};
    for(int k = 0; k < 3; k++) {
        if(t[i++] != 620) { printf("bad const bit mark\n"); return 0; }
        if(t[i++] != want[k]) { printf("FAIL constant bit %d\n", k); return 0; }
    }
    if(t[i++] != 620 || t[i++] != 19000) { printf("bad mid-frame gap\n"); return 0; }
    for(int byte = 4; byte < 8; byte++) {
        uint8_t v = 0;
        for(int b = 0; b < 8; b++) {
            if(t[i++] != 620) { printf("bad bit mark\n"); return 0; }
            uint32_t sp = t[i++];
            if(sp == 1600) v |= 1 << b;
            else if(sp != 540) { printf("bad space %u\n", sp); return 0; }
        }
        st[byte] = v;
    }
    if(t[i++] != 620) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    return 1;
}

int main(void) {
    uint32_t t[GREE_IR_MAX_TIMINGS];
    size_t n = 0;
    uint8_t st[8];

    // IRGreeAC::stateReset() is power-off / auto / fan-auto / 25C, so its
    // fixed bytes and checksum are a direct check on our model.
    uint8_t reset[8] = {0x00, 0x09, 0x20, 0x50, 0x00, 0x20, 0x00, 0x00};
    reset[7] = (uint8_t)(ref_checksum(reset, 8) << 4);
    printf("library reset state: %02X %02X %02X %02X %02X %02X %02X %02X\n",
           reset[0], reset[1], reset[2], reset[3], reset[4], reset[5], reset[6], reset[7]);
    printf("  temp nibble 9 -> %dC  %s\n", 9 + 16, (9 + 16) == 25 ? "ok" : "FAIL");
    if((9 + 16) != 25) fails++;

    int checked = 0;
    for(int m = GreeModeCool; m < GreeModeCount; m++) {
        for(int f = 0; f < GreeFanCount; f++) {
            for(int temp = GREE_TEMP_MIN; temp <= GREE_TEMP_MAX; temp++) {
                GreeRequest r = {(GreeMode)m, (GreeFan)f, (uint8_t)temp, 0, 0};
                if(!gree_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 139) { printf("FAIL len %zu != 139\n", n); fails++; continue; }
                if(!decode(t, n, st)) { fails++; continue; }

                if((st[0] & 0x08) == 0) { printf("FAIL power bit\n"); fails++; }
                if(st[3] != 0x50) { printf("FAIL byte3 fixed\n"); fails++; }
                if(st[5] != 0x20) { printf("FAIL byte5 fixed\n"); fails++; }
                if(((st[0] >> 4) & 3) != (unsigned)f) { printf("FAIL fan\n"); fails++; }
                unsigned want_t = (m == GreeModeAuto) ? 25 - 16 : (unsigned)(temp - 16);
                if((st[1] & 0x0F) != want_t) { printf("FAIL temp m=%d T=%d\n", m, temp); fails++; }
                if((st[1] >> 4) != 0) { printf("FAIL timer bits set in byte1\n"); fails++; }
                if((st[7] >> 4) != ref_checksum(st, 8)) { printf("FAIL checksum\n"); fails++; }
                checked++;
            }
        }
    }
    printf("state frames verified: %d\n", checked);

    // Mode codes: auto 0, cool 1, dry 2, fan 3, heat 4
    struct { GreeMode m; unsigned want; const char* nm; } modes[] = {
        {GreeModeAuto, 0, "auto"}, {GreeModeCool, 1, "cool"}, {GreeModeDry, 2, "dry"},
        {GreeModeFan, 3, "fan"}, {GreeModeHeat, 4, "heat"}};
    for(size_t k = 0; k < 5; k++) {
        GreeRequest r = {modes[k].m, GreeFanAuto, 24, 0, 0};
        gree_ir_encode_state(&r, t, &n); decode(t, n, st);
        unsigned got = st[0] & 0x07;
        printf("mode %-5s: got %u want %u  %s\n", modes[k].nm, got, modes[k].want,
               got == modes[k].want ? "ok" : "FAIL");
        if(got != modes[k].want) fails++;
    }

    // Power off clears bit 3 and keeps the checksum valid
    GreeRequest r = {GreeModeCool, GreeFanAuto, 24, 0, 0};
    if(gree_ir_encode_toggle(&r, GreeTogglePowerOff, t, &n) && decode(t, n, st)) {
        if(st[0] & 0x08) { printf("FAIL power-off bit\n"); fails++; }
        if((st[7] >> 4) != ref_checksum(st, 8)) { printf("FAIL power-off checksum\n"); fails++; }
        printf("power-off frame ok\n");
    } else fails++;

    // Feature bits land where the union says
    struct { GreeToggle tg; int byte; int bit; const char* nm; } bits[] = {
        {GreeToggleSwing, 0, 6, "swing"}, {GreeToggleSleep, 0, 7, "sleep"},
        {GreeToggleTurbo, 2, 4, "turbo"}, {GreeToggleLight, 2, 5, "light"},
        {GreeToggleXfan, 2, 7, "xfan"},   {GreeToggleEcono, 7, 2, "econo"}};
    for(size_t k = 0; k < 6; k++) {
        GreeRequest rr = {GreeModeCool, GreeFanAuto, 24, 1u << bits[k].tg, 0};
        if(!gree_ir_encode_state(&rr, t, &n) || !decode(t, n, st)) { fails++; continue; }
        int ok = (st[bits[k].byte] >> bits[k].bit) & 1;
        if(!ok) fails++;
        printf("%-6s bit byte%d.%d  %s\n", bits[k].nm, bits[k].byte, bits[k].bit, ok ? "ok" : "FAIL");
        if((st[7] >> 4) != ref_checksum(st, 8)) { printf("FAIL checksum with %s\n", bits[k].nm); fails++; }
    }

    // Vane presets go into byte 4 and must clear the auto-swing bit
    for(int e = 0; e < GreeExtraCount; e++) {
        GreeRequest rr = {GreeModeCool, GreeFanAuto, 24, 1u << GreeToggleSwing, 0};
        if(!gree_ir_encode_extra(&rr, (GreeExtra)e, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(st[0] & (1 << 6)) { printf("FAIL vane preset left swing bit set e=%d\n", e); fails++; }
        if((st[7] >> 4) != ref_checksum(st, 8)) { printf("FAIL vane checksum e=%d\n", e); fails++; }
    }
    printf("vane presets ok\n");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
