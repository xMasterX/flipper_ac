#include "toshiba_ir_protocol.h"
#include <stdio.h>

static int fails;

static uint8_t ref_xor(const uint8_t* b, int n) {
    uint8_t x = 0;
    for(int i = 0; i < n; i++) x ^= b[i];
    return x;
}

// Decode both passes, MSB first, and require them identical.
static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    size_t i = 0;
    uint8_t pass_bytes[2][9];
    for(int pass = 0; pass < 2; pass++) {
        if(t[i++] != 4400 || t[i++] != 4300) { printf("bad header p%d\n", pass); return 0; }
        for(int byte = 0; byte < 9; byte++) {
            uint8_t v = 0;
            for(int b = 0; b < 8; b++) {
                if(t[i++] != 580) { printf("bad bit mark\n"); return 0; }
                uint32_t sp = t[i++];
                if(sp == 1600) v = (v << 1) | 1;
                else if(sp == 490) v = (v << 1);
                else { printf("bad space %u\n", sp); return 0; }
            }
            pass_bytes[pass][byte] = v;
        }
        if(pass == 0) {
            if(t[i++] != 580 || t[i++] != 7400) { printf("bad inter-message gap\n"); return 0; }
        }
    }
    if(t[i++] != 580) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    for(int k = 0; k < 9; k++) {
        if(pass_bytes[0][k] != pass_bytes[1][k]) { printf("FAIL passes differ at %d\n", k); return 0; }
        st[k] = pass_bytes[0][k];
    }
    return 1;
}

int main(void) {
    uint32_t t[TOSHIBA_IR_MAX_TIMINGS];
    size_t n = 0;
    uint8_t st[9];

    // IRToshibaAC::stateReset is {F2,0D,03,FC,01} + temp 22C + swing off.
    // Rebuild it by hand and check our checksum model against it.
    uint8_t reset[9] = {0xF2, 0x0D, 0x03, 0xFC, 0x01, 0x00, 0x00, 0x00, 0x00};
    reset[5] = (uint8_t)(2 /* swing off */ | ((22 - 17) << 4));
    reset[8] = ref_xor(reset, 8);
    printf("library reset state: %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
           reset[0],reset[1],reset[2],reset[3],reset[4],reset[5],reset[6],reset[7],reset[8]);
    printf("  byte1 is byte0 inverted: %s\n", (uint8_t)~reset[0] == reset[1] ? "ok" : "FAIL");
    printf("  byte3 is byte2 inverted: %s\n", (uint8_t)~reset[2] == reset[3] ? "ok" : "FAIL");
    if((uint8_t)~reset[0] != reset[1] || (uint8_t)~reset[2] != reset[3]) fails++;

    int checked = 0;
    for(int m = ToshibaModeCool; m < ToshibaModeCount; m++) {
        for(int f = 0; f < ToshibaFanCount; f++) {
            for(int temp = TOSHIBA_TEMP_MIN; temp <= TOSHIBA_TEMP_MAX; temp++) {
                ToshibaRequest r = {(ToshibaMode)m, (ToshibaFan)f, (uint8_t)temp, 0, 0};
                if(!toshiba_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 295) { printf("FAIL len %zu != 295\n", n); fails++; continue; }
                if(!decode(t, n, st)) { fails++; continue; }

                if(st[0] != 0xF2 || st[1] != 0x0D) { printf("FAIL preamble\n"); fails++; }
                if(st[2] != 0x03 || st[3] != 0xFC) { printf("FAIL length bytes\n"); fails++; }
                if(st[4] != 0x01) { printf("FAIL byte4\n"); fails++; }
                if((st[5] >> 4) != (unsigned)(temp - 17)) { printf("FAIL temp %d\n", temp); fails++; }
                if((st[6] & 7) == 7) { printf("FAIL mode reads as off\n"); fails++; }
                if(st[8] != ref_xor(st, 8)) { printf("FAIL checksum\n"); fails++; }
                checked++;
            }
        }
    }
    printf("state frames verified: %d\n", checked);

    // Mode codes: auto 0, cool 1, dry 2, heat 3, fan 4
    struct { ToshibaMode m; unsigned want; const char* nm; } modes[] = {
        {ToshibaModeAuto, 0, "auto"}, {ToshibaModeCool, 1, "cool"}, {ToshibaModeDry, 2, "dry"},
        {ToshibaModeHeat, 3, "heat"}, {ToshibaModeFan, 4, "fan"}};
    for(size_t k = 0; k < 5; k++) {
        ToshibaRequest r = {modes[k].m, ToshibaFanAuto, 24, 0, 0};
        toshiba_ir_encode_state(&r, t, &n); decode(t, n, st);
        unsigned got = st[6] & 7;
        printf("mode %-5s: got %u want %u  %s\n", modes[k].nm, got, modes[k].want,
               got == modes[k].want ? "ok" : "FAIL");
        if(got != modes[k].want) fails++;
    }

    // Fan codes: auto 0, min 1, med 3, max 5
    unsigned fanwant[4] = {0, 1, 3, 5};
    for(int f = 0; f < ToshibaFanCount; f++) {
        ToshibaRequest r = {ToshibaModeCool, (ToshibaFan)f, 24, 0, 0};
        toshiba_ir_encode_state(&r, t, &n); decode(t, n, st);
        unsigned got = st[6] >> 5;
        if(got != fanwant[f]) { printf("FAIL fan %d: got %u want %u\n", f, got, fanwant[f]); fails++; }
    }
    printf("fan codes ok\n");

    // Power off is mode 7, not a bit
    ToshibaRequest r = {ToshibaModeCool, ToshibaFanAuto, 24, 0, 0};
    if(toshiba_ir_encode_toggle(&r, ToshibaTogglePowerOff, t, &n) && decode(t, n, st)) {
        unsigned got = st[6] & 7;
        printf("power off -> mode %u (want 7)  %s\n", got, got == 7 ? "ok" : "FAIL");
        if(got != 7) fails++;
        if(st[8] != ref_xor(st, 8)) { printf("FAIL power-off checksum\n"); fails++; }
    } else fails++;

    // Swing on/off and the filter bit
    r.toggle_bits = 1u << ToshibaToggleSwing;
    toshiba_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[5] & 7) != 1) { printf("FAIL swing on\n"); fails++; }
    r.toggle_bits = 0;
    toshiba_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[5] & 7) != 2) { printf("FAIL swing off\n"); fails++; }
    r.toggle_bits = 1u << ToshibaToggleFilter;
    toshiba_ir_encode_state(&r, t, &n); decode(t, n, st);
    if(((st[7] >> 4) & 1) != 1) { printf("FAIL filter bit\n"); fails++; }
    printf("swing + filter ok\n");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
