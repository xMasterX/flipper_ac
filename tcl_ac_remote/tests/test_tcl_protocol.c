#include "tcl_ir_protocol.h"
#include <stdio.h>

static int fails;

static uint8_t ref_sum(const uint8_t* b, int n) {
    uint8_t s = 0;
    for(int i = 0; i < n; i++) s += b[i];
    return s;
}

static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    size_t i = 0;
    if(t[i++] != 3000 || t[i++] != 1650) { printf("bad header\n"); return 0; }
    for(int byte = 0; byte < 14; byte++) {
        uint8_t v = 0;
        for(int b = 0; b < 8; b++) {
            if(t[i++] != 500) { printf("bad bit mark\n"); return 0; }
            uint32_t sp = t[i++];
            if(sp == 1050) v |= 1 << b;
            else if(sp != 325) { printf("bad space %u\n", sp); return 0; }
        }
        st[byte] = v;
    }
    if(t[i++] != 500) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    return 1;
}

int main(void) {
    uint32_t t[TCL_IR_MAX_TIMINGS];
    size_t n = 0;
    uint8_t st[14];

    // IRTcl112Ac::stateReset is documented as "On, Cool, 24C". Its literal
    // checksum byte is stale (the library recomputes before sending), so we
    // check the FIELDS against it, not byte 13.
    uint8_t reset[14] = {0x23,0xCB,0x26,0x01,0x00,0x24,0x03,0x07,0x40,0,0,0,0,0x03};
    printf("library reset: mode=%u (want cool 3)  temp=%u -> %uC (want 24)\n",
           reset[6] & 0x0F, reset[7] & 0x0F, 31 - (reset[7] & 0x0F));
    if((reset[6] & 0x0F) != 3) fails++;
    if(31 - (reset[7] & 0x0F) != 24) fails++;
    if((reset[5] >> 2) & 1) { printf("  power bit set: ok\n"); } else { printf("  FAIL power bit\n"); fails++; }
    printf("  literal byte13=%02X, recomputed=%02X (stale in the library, ignored)\n",
           reset[13], ref_sum(reset, 13));

    int checked = 0;
    for(int m = TclModeCool; m < TclModeCount; m++) {
        for(int f = 0; f < TclFanCount; f++) {
            for(int temp = TCL_TEMP_MIN; temp <= TCL_TEMP_MAX; temp++) {
                TclRequest r = {(TclMode)m, (TclFan)f, (uint8_t)temp, 0, 0};
                if(!tcl_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 227) { printf("FAIL len %zu != 227\n", n); fails++; continue; }
                if(!decode(t, n, st)) { fails++; continue; }

                if(st[0] != 0x23 || st[1] != 0xCB || st[2] != 0x26) { printf("FAIL preamble\n"); fails++; }
                if(st[3] != 0x01) { printf("FAIL msgtype\n"); fails++; }
                if(((st[5] >> 2) & 1) != 1) { printf("FAIL power bit\n"); fails++; }
                if((st[7] & 0x0F) != (unsigned)(31 - temp)) { printf("FAIL temp %d\n", temp); fails++; }
                if(st[13] != ref_sum(st, 13)) { printf("FAIL checksum\n"); fails++; }
                checked++;
            }
        }
    }
    printf("state frames verified: %d\n", checked);

    // Mode codes: heat 1, dry 2, cool 3, fan 7, auto 8
    struct { TclMode m; unsigned want; const char* nm; } modes[] = {
        {TclModeHeat, 1, "heat"}, {TclModeDry, 2, "dry"}, {TclModeCool, 3, "cool"},
        {TclModeFan, 7, "fan"}, {TclModeAuto, 8, "auto"}};
    for(size_t k = 0; k < 5; k++) {
        TclRequest r = {modes[k].m, TclFanAuto, 24, 0, 0};
        tcl_ir_encode_state(&r, t, &n); decode(t, n, st);
        unsigned got = st[6] & 0x0F;
        printf("mode %-5s: got %u want %u  %s\n", modes[k].nm, got, modes[k].want,
               got == modes[k].want ? "ok" : "FAIL");
        if(got != modes[k].want) fails++;
    }

    // Fan codes: auto 0, low 2, med 3, high 5
    unsigned fanwant[4] = {0, 2, 3, 5};
    for(int f = 0; f < TclFanCount; f++) {
        TclRequest r = {TclModeCool, (TclFan)f, 24, 0, 0};
        tcl_ir_encode_state(&r, t, &n); decode(t, n, st);
        if((st[8] & 7) != fanwant[f]) { printf("FAIL fan %d\n", f); fails++; }
    }
    printf("fan codes ok\n");

    // Feature bits at their union positions
    struct { TclToggle tg; int byte; int bit; const char* nm; } bits[] = {
        {TclToggleQuiet, 5, 5, "quiet"}, {TclToggleLight, 5, 6, "light"},
        {TclToggleEcono, 5, 7, "econo"}, {TclToggleHealth, 6, 4, "health"},
        {TclToggleTurbo, 6, 5, "turbo"}};
    for(size_t k = 0; k < 5; k++) {
        TclRequest r = {TclModeCool, TclFanAuto, 24, 1u << bits[k].tg, 0};
        if(!tcl_ir_encode_state(&r, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(!((st[bits[k].byte] >> bits[k].bit) & 1)) { printf("FAIL %s bit\n", bits[k].nm); fails++; }
        if(st[13] != ref_sum(st, 13)) { printf("FAIL checksum with %s\n", bits[k].nm); fails++; }
    }
    printf("feature bits ok\n");

    // Power off clears bit 2 of byte 5
    TclRequest r = {TclModeCool, TclFanAuto, 24, 0, 0};
    if(tcl_ir_encode_toggle(&r, TclTogglePowerOff, t, &n) && decode(t, n, st)) {
        if((st[5] >> 2) & 1) { printf("FAIL power-off bit\n"); fails++; }
        if(st[13] != ref_sum(st, 13)) { printf("FAIL power-off checksum\n"); fails++; }
        printf("power-off frame ok\n");
    } else fails++;

    // Swing on/off and vane presets
    r.toggle_bits = 1u << TclToggleSwing;
    tcl_ir_encode_state(&r, t, &n); decode(t, n, st);
    if(((st[8] >> 3) & 7) != 0b111) { printf("FAIL swing on\n"); fails++; }
    for(int e = 0; e < TclExtraCount; e++) {
        if(!tcl_ir_encode_extra(&r, (TclExtra)e, t, &n) || !decode(t, n, st)) { fails++; continue; }
        unsigned v = (st[8] >> 3) & 7;
        if(v == 0 || v == 0b111) { printf("FAIL vane preset e=%d gave %u\n", e, v); fails++; }
        if(st[13] != ref_sum(st, 13)) { printf("FAIL vane checksum\n"); fails++; }
    }
    printf("swing + vane presets ok\n");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
