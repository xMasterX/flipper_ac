#include "carrier_ir_protocol.h"
#include <stdio.h>

static int fails;

// Rebuild the 64-bit word from the timings (LSB first), checking structure.
static int decode(const uint32_t* t, size_t n, unsigned long long* out) {
    size_t i = 0;
    if(t[i++] != 8940 || t[i++] != 4556) { printf("bad header\n"); return 0; }
    unsigned long long v = 0;
    for(int b = 0; b < 64; b++) {
        if(t[i++] != 503) { printf("bad bit mark\n"); return 0; }
        uint32_t sp = t[i++];
        if(sp == 1736) v |= 1ULL << b;
        else if(sp != 615) { printf("bad space %u\n", sp); return 0; }
    }
    if(t[i++] != 503) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    *out = v;
    return 1;
}

static unsigned nib(unsigned long long v, int off, int w) {
    return (unsigned)((v >> off) & ((1ULL << w) - 1));
}

static uint8_t csum(unsigned long long raw) {
    unsigned long long d = raw >> 20;
    uint8_t r = 0;
    for(; d; d >>= 4) r += d & 0xF;
    return r & 0xF;
}

int main(void) {
    uint32_t t[CARRIER_IR_MAX_TIMINGS];
    size_t n = 0;
    unsigned long long v = 0;

    // The library's reset state must satisfy our checksum model
    unsigned long long reset = 0x109000002C2A5584ULL;
    printf("reset-state checksum: got %X want %X  %s\n",
           csum(reset), nib(reset, 16, 4), csum(reset) == nib(reset, 16, 4) ? "ok" : "FAIL");
    if(csum(reset) != nib(reset, 16, 4)) fails++;

    int checked = 0;
    for(int m = CarrierModeCool; m < CarrierModeCount; m++) {
        for(int f = 0; f < CarrierFanCount; f++) {
            for(int temp = CARRIER_TEMP_MIN; temp <= CARRIER_TEMP_MAX; temp++) {
                CarrierRequest r = {(CarrierMode)m, (CarrierFan)f, (uint8_t)temp, 0, 0};
                if(!carrier_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 131) { printf("FAIL len %zu != 131\n", n); fails++; continue; }
                if(!decode(t, n, &v)) { fails++; continue; }

                if(nib(v, 0, 8) != 0x84) { printf("FAIL fixed byte0\n"); fails++; }
                if(nib(v, 8, 8) != 0x55) { printf("FAIL fixed byte1\n"); fails++; }
                if(nib(v, 36, 1) != 1) { printf("FAIL power bit\n"); fails++; }
                if(nib(v, 24, 4) != (unsigned)(temp - 16)) { printf("FAIL temp %d\n", temp); fails++; }
                if(nib(v, 16, 4) != csum(v)) { printf("FAIL checksum\n"); fails++; }
                // timers must be off on a plain state command
                if(nib(v, 37, 1) || nib(v, 38, 1)) { printf("FAIL timer enabled\n"); fails++; }
                checked++;
            }
        }
    }
    printf("state words verified: %d\n", checked);

    // Mode codes match the library: Heat 0b01, Cool 0b10, Fan 0b11
    struct { CarrierMode m; unsigned want; const char* nm; } modes[] = {
        {CarrierModeCool, 0b10, "cool"}, {CarrierModeHeat, 0b01, "heat"}, {CarrierModeFan, 0b11, "fan"}};
    for(size_t k = 0; k < 3; k++) {
        CarrierRequest r = {modes[k].m, CarrierFanAuto, 24, 0, 0};
        carrier_ir_encode_state(&r, t, &n); decode(t, n, &v);
        unsigned got = nib(v, 20, 2);
        printf("mode %-5s bits: got %u want %u  %s\n", modes[k].nm, got, modes[k].want,
               got == modes[k].want ? "ok" : "FAIL");
        if(got != modes[k].want) fails++;
    }

    // Power off clears bit 36 and keeps the checksum valid
    CarrierRequest r = {CarrierModeCool, CarrierFanAuto, 24, 0, 0};
    if(carrier_ir_encode_toggle(&r, CarrierTogglePowerOff, t, &n) && decode(t, n, &v)) {
        if(nib(v, 36, 1) != 0) { printf("FAIL power-off bit\n"); fails++; }
        if(nib(v, 16, 4) != csum(v)) { printf("FAIL power-off checksum\n"); fails++; }
        printf("power-off frame ok\n");
    } else fails++;

    // Swing and Sleep land on bits 29 and 39
    r.toggle_bits = (1u << CarrierToggleSwing) | (1u << CarrierToggleSleep);
    if(carrier_ir_encode_state(&r, t, &n) && decode(t, n, &v)) {
        if(nib(v, 29, 1) != 1) { printf("FAIL swing bit\n"); fails++; }
        if(nib(v, 39, 1) != 1) { printf("FAIL sleep bit\n"); fails++; }
        if(nib(v, 16, 4) != csum(v)) { printf("FAIL toggle checksum\n"); fails++; }
        printf("swing+sleep bits ok\n");
    } else fails++;

    // Timer presets set the right enable bit and hour field, and stay in range
    r.toggle_bits = 0;
    for(int e = CarrierExtraOff1h; e < CarrierExtraCount; e++) {
        if(!carrier_ir_encode_extra(&r, (CarrierExtra)e, t, &n) || !decode(t, n, &v)) { fails++; continue; }
        int is_on = e >= CarrierExtraOn1h;
        unsigned en = nib(v, is_on ? 38 : 37, 1);
        unsigned hrs = nib(v, is_on ? 52 : 60, 4);
        if(en != 1) { printf("FAIL enable e=%d\n", e); fails++; }
        if(hrs < 1 || hrs > 9) { printf("FAIL hours %u e=%d\n", hrs, e); fails++; }
        if(nib(v, 16, 4) != csum(v)) { printf("FAIL timer checksum e=%d\n", e); fails++; }
    }
    printf("timer presets ok\n");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
