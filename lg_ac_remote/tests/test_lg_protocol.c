#include "lg_ir_protocol.h"
#include <stdio.h>
#include <string.h>

static int fails;
static void ck(const char* what, unsigned long got, unsigned long want) {
    int ok = got == want;
    if(!ok) fails++;
    printf("%-30s got %07lX want %07lX  %s\n", what, got, want, ok ? "ok" : "FAIL");
}

static int decode(const uint32_t* t, size_t n, unsigned long* out) {
    size_t i = 0;
    if(t[i++] != 8500 || t[i++] != 4250) { printf("bad header\n"); return 0; }
    unsigned long v = 0;
    for(int b = 0; b < 28; b++) {
        if(t[i++] != 550) { printf("bad bit mark\n"); return 0; }
        uint32_t sp = t[i++];
        if(sp == 1600) v = (v << 1) | 1;
        else if(sp == 550) v = (v << 1);
        else { printf("bad space %u\n", sp); return 0; }
    }
    if(t[i++] != 550) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    *out = v;
    return 1;
}

static uint8_t nibsum(unsigned long raw) {
    unsigned long body = raw >> 4;
    uint8_t s = 0;
    for(int i = 0; i < 4; i++) s += (body >> (i * 4)) & 0xF;
    return s & 0xF;
}

int main(void) {
    uint32_t t[LG_IR_MAX_TIMINGS];
    size_t n = 0;
    unsigned long v = 0;
    LgRequest req = {LgModeCool, LgFanAuto, 24, 0, 0};

    struct { const char* nm; LgToggle tg; unsigned long want; } tog[] = {
        {"power off", LgTogglePowerOff, 0x88C0051},
        {"swingV toggle", LgToggleSwing, 0x8810001},
        {"light toggle", LgToggleLight, 0x88C00A6},
    };
    for(size_t k = 0; k < sizeof(tog)/sizeof(tog[0]); k++) {
        if(!lg_ir_encode_toggle(&req, tog[k].tg, t, &n) || !decode(t, n, &v)) { fails++; continue; }
        ck(tog[k].nm, v, tog[k].want);
    }

    struct { const char* nm; LgExtra ex; unsigned long want; } ext[] = {
        {"vane lowest", LgExtraVane1, 0x8813048},
        {"vane highest", LgExtraVane6, 0x881309D},
        {"swingH auto", LgExtraSwingHAuto, 0x881316B},
        {"swingH off", LgExtraSwingHOff, 0x881317C},
    };
    for(size_t k = 0; k < sizeof(ext)/sizeof(ext[0]); k++) {
        if(!lg_ir_encode_extra(&req, ext[k].ex, t, &n) || !decode(t, n, &v)) { fails++; continue; }
        ck(ext[k].nm, v, ext[k].want);
    }

    int checked = 0;
    for(int m = LgModeCool; m < LgModeCount; m++) {
        for(int f = 0; f < LgFanCount; f++) {
            for(int temp = LG_TEMP_MIN; temp <= LG_TEMP_MAX; temp++) {
                LgRequest r = {(LgMode)m, (LgFan)f, (uint8_t)temp, 0, 0};
                if(!lg_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 59) { printf("FAIL len %zu != 59\n", n); fails++; continue; }
                if(!decode(t, n, &v)) { fails++; continue; }
                if(((v >> 20) & 0xFF) != 0x88) { printf("FAIL signature m=%d\n", m); fails++; }
                if(((v >> 18) & 0x3) != 0) { printf("FAIL power-on bits\n"); fails++; }
                if(((v >> 8) & 0xF) != (unsigned)(temp - 15)) { printf("FAIL temp %d\n", temp); fails++; }
                if((v & 0xF) != nibsum(v)) { printf("FAIL checksum 0x%07lX\n", v); fails++; }
                checked++;
            }
        }
    }
    printf("\nstate words verified: %d\n", checked);
    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
