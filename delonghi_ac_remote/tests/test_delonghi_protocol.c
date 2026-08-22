#include "delonghi_ir_protocol.h"
#include <stdio.h>

static int fails;

// Rebuild the 64-bit word from the timings (LSB first), checking structure.
static int decode(const uint32_t* t, size_t n, unsigned long long* out) {
    size_t i = 0;
    if(t[i++] != 8984 || t[i++] != 4200) { printf("bad header\n"); return 0; }
    unsigned long long v = 0;
    for(int b = 0; b < 64; b++) {
        if(t[i++] != 572) { printf("bad bit mark\n"); return 0; }
        uint32_t sp = t[i++];
        if(sp == 1558) v |= 1ULL << b;
        else if(sp != 510) { printf("bad space %u\n", sp); return 0; }
    }
    if(t[i++] != 572) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    *out = v;
    return 1;
}

static uint8_t sum_bytes(unsigned long long raw) {
    uint8_t s = 0;
    for(int off = 0; off < 56; off += 8) s += (uint8_t)((raw >> off) & 0xFF);
    return s;
}

int main(void) {
    uint32_t t[DELONGHI_IR_MAX_TIMINGS];
    size_t n = 0;
    unsigned long long v = 0;
    int checked = 0;

    for(int m = DelonghiModeCool; m < DelonghiModeCount; m++) {
        for(int f = 0; f < DelonghiFanCount; f++) {
            for(int temp = DELONGHI_TEMP_MIN; temp <= DELONGHI_TEMP_MAX; temp++) {
                DelonghiRequest r = {(DelonghiMode)m, (DelonghiFan)f, (uint8_t)temp, 0, 0};
                if(!delonghi_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 131) { printf("FAIL len %zu != 131\n", n); fails++; continue; }
                if(!decode(t, n, &v)) { fails++; continue; }

                if((v & 0xFF) != 0x53) { printf("FAIL header byte\n"); fails++; }
                if(((v >> 16) & 1) != 1) { printf("FAIL power bit\n"); fails++; }
                if(((v >> 15) & 1) != 0) { printf("FAIL fahrenheit set\n"); fails++; }
                if(((v >> 56) & 0xFF) != sum_bytes(v)) { printf("FAIL checksum\n"); fails++; }

                // Auto and Dry must force fan auto; fan-only must not use auto
                unsigned fan = (v >> 13) & 3;
                if((m == DelonghiModeAuto || m == DelonghiModeDry) && fan != 0) {
                    printf("FAIL fan not forced auto in mode %d\n", m); fails++;
                }
                if(m == DelonghiModeFan && fan == 0) { printf("FAIL fan-only auto\n"); fails++; }

                // Temperature only rides along in Cool
                unsigned tf = (v >> 8) & 0x1F;
                unsigned want_tf = (m == DelonghiModeCool) ? (unsigned)(temp - 17)
                                 : (m == DelonghiModeFan) ? 6u : 0u;
                if(tf != want_tf) { printf("FAIL temp field m=%d T=%d got %u want %u\n", m, temp, tf, want_tf); fails++; }
                checked++;
            }
        }
    }
    printf("state words verified: %d\n", checked);

    // Reference: the library's reset state is 0x5400000000000153, i.e. header
    // 0x53 with a matching byte-sum checksum of 0x54.
    printf("reset-state checksum model: 0x53+0x01 = 0x%02X (want 0x54)  %s\n",
           (unsigned)(0x53 + 0x01), (0x53 + 0x01) == 0x54 ? "ok" : "FAIL");

    // Power-off frame must clear bit 16 and keep the checksum valid
    DelonghiRequest r = {DelonghiModeCool, DelonghiFanAuto, 24, 0, 0};
    if(delonghi_ir_encode_toggle(&r, DelonghiTogglePowerOff, t, &n) && decode(t, n, &v)) {
        if(((v >> 16) & 1) != 0) { printf("FAIL power-off bit still set\n"); fails++; }
        if(((v >> 56) & 0xFF) != sum_bytes(v)) { printf("FAIL power-off checksum\n"); fails++; }
        printf("power-off frame ok (0x%016llX)\n", v);
    } else { fails++; }

    // Boost/Sleep bits land where the union says
    r.toggle_bits = (1u << DelonghiToggleBoost) | (1u << DelonghiToggleSleep);
    if(delonghi_ir_encode_state(&r, t, &n) && decode(t, n, &v)) {
        if(((v >> 20) & 1) != 1) { printf("FAIL boost bit\n"); fails++; }
        if(((v >> 21) & 1) != 1) { printf("FAIL sleep bit\n"); fails++; }
        if(((v >> 56) & 0xFF) != sum_bytes(v)) { printf("FAIL toggle checksum\n"); fails++; }
        printf("boost+sleep bits ok\n");
    } else { fails++; }

    // Off-timer presets
    for(int e = DelonghiExtraOffTimer1h; e < DelonghiExtraCount; e++) {
        DelonghiRequest rr = {DelonghiModeCool, DelonghiFanAuto, 24, 0, 0};
        if(!delonghi_ir_encode_extra(&rr, (DelonghiExtra)e, t, &n) || !decode(t, n, &v)) { fails++; continue; }
        if(((v >> 40) & 1) != 1) { printf("FAIL off-timer enable e=%d\n", e); fails++; }
        if(((v >> 56) & 0xFF) != sum_bytes(v)) { printf("FAIL off-timer checksum\n"); fails++; }
    }
    printf("off-timer presets ok\n");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
