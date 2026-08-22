#include "fujitsu_ir_protocol.h"
#include <stdio.h>

static int fails;

// IRFujitsuAC::checkSum for ARRAH2E:
//   sumBytes(longcode + kFujitsuAcStateLengthShort,
//            kFujitsuAcStateLength - kFujitsuAcStateLengthShort - 1)
// with the short length 7 and full length 16, that is bytes 7..14 inclusive.
// Transcribed straight from the library rather than from our own encoder, so
// an off-by-one in the encoder cannot hide behind a matching mistake here.
#define REF_SHORT_LEN 7
#define REF_LONG_LEN  16
static uint8_t ref_checksum(const uint8_t* st) {
    uint8_t sum = 0;
    for(int i = REF_SHORT_LEN; i < REF_LONG_LEN - 1; i++) sum += st[i];
    return (uint8_t)(0 - sum);
}

static int decode(const uint32_t* t, size_t n, uint8_t* st, size_t* out_len) {
    size_t i = 0;
    if(t[i++] != 3324 || t[i++] != 1574) { printf("bad header\n"); return 0; }
    size_t bytes = (n - 3) / 16;
    if((n - 3) % 16 != 0) { printf("FAIL not a whole number of bytes (n=%zu)\n", n); return 0; }
    for(size_t byte = 0; byte < bytes; byte++) {
        uint8_t v = 0;
        for(int b = 0; b < 8; b++) {
            if(t[i++] != 448) { printf("bad bit mark\n"); return 0; }
            uint32_t sp = t[i++];
            if(sp == 1182) v |= 1 << b;
            else if(sp != 390) { printf("bad space %u\n", sp); return 0; }
        }
        st[byte] = v;
    }
    if(t[i++] != 448) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    *out_len = bytes;
    return 1;
}

int main(void) {
    uint32_t t[FUJITSU_IR_MAX_TIMINGS];
    size_t n = 0, blen = 0;
    uint8_t st[16];

    int checked = 0;
    for(int m = FujitsuModeCool; m < FujitsuModeCount; m++) {
        for(int f = 0; f < FujitsuFanCount; f++) {
            for(int temp = FUJITSU_TEMP_MIN; temp <= FUJITSU_TEMP_MAX; temp++) {
                FujitsuRequest r = {(FujitsuMode)m, (FujitsuFan)f, (uint8_t)temp, 0, 0};
                if(!fujitsu_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 259) { printf("FAIL len %zu != 259\n", n); fails++; continue; }
                if(!decode(t, n, st, &blen) || blen != 16) { fails++; continue; }

                if(st[0] != 0x14 || st[1] != 0x63) { printf("FAIL preamble\n"); fails++; }
                if(st[3] != 0x10 || st[4] != 0x10) { printf("FAIL bytes 3/4\n"); fails++; }
                if(st[5] != 0xFE) { printf("FAIL long-frame cmd byte\n"); fails++; }
                if(st[6] != 9) { printf("FAIL rest length\n"); fails++; }
                if(st[7] != 0x30) { printf("FAIL protocol byte\n"); fails++; }
                if((st[8] & 1) != 1) { printf("FAIL power bit\n"); fails++; }
                if(((st[8] >> 1) & 1) != 0) { printf("FAIL fahrenheit set\n"); fails++; }
                if(((st[8] >> 2) & 0x3F) != (unsigned)((temp - 16) * 4)) {
                    printf("FAIL temp %d -> %u\n", temp, (st[8] >> 2) & 0x3F); fails++;
                }
                if(((st[14] >> 5) & 1) != 1) { printf("FAIL byte14 marker bit\n"); fails++; }
                if(st[15] != ref_checksum(st)) { printf("FAIL checksum\n"); fails++; }
                checked++;
            }
        }
    }
    printf("long frames verified: %d\n", checked);

    // Mode codes: auto 0, cool 1, dry 2, fan 3, heat 4
    struct { FujitsuMode m; unsigned want; const char* nm; } modes[] = {
        {FujitsuModeAuto, 0, "auto"}, {FujitsuModeCool, 1, "cool"}, {FujitsuModeDry, 2, "dry"},
        {FujitsuModeFan, 3, "fan"}, {FujitsuModeHeat, 4, "heat"}};
    for(size_t k = 0; k < 5; k++) {
        FujitsuRequest r = {modes[k].m, FujitsuFanAuto, 24, 0, 0};
        fujitsu_ir_encode_state(&r, t, &n); decode(t, n, st, &blen);
        unsigned got = st[9] & 7;
        printf("mode %-5s: got %u want %u  %s\n", modes[k].nm, got, modes[k].want,
               got == modes[k].want ? "ok" : "FAIL");
        if(got != modes[k].want) fails++;
    }

    // Fan is ordered high=1 .. low=3, which is easy to get backwards
    unsigned fanwant[4] = {0, 3, 2, 1};
    for(int f = 0; f < FujitsuFanCount; f++) {
        FujitsuRequest r = {FujitsuModeCool, (FujitsuFan)f, 24, 0, 0};
        fujitsu_ir_encode_state(&r, t, &n); decode(t, n, st, &blen);
        if((st[10] & 7) != fanwant[f]) {
            printf("FAIL fan %d: got %u want %u\n", f, st[10] & 7, fanwant[f]); fails++;
        }
    }
    printf("fan codes ok (auto 0, low 3, med 2, high 1)\n");

    // Short command frames: 7 bytes, last is the complement of the command
    struct { const char* nm; unsigned cmd; } shorts[] = {
        {"power off", 0x02}, {"powerful", 0x39}, {"econo", 0x09}};
    FujitsuToggle stg[] = {FujitsuTogglePowerOff, FujitsuTogglePowerful, FujitsuToggleEcono};
    FujitsuRequest r = {FujitsuModeCool, FujitsuFanAuto, 24, 0, 0};
    for(size_t k = 0; k < 3; k++) {
        if(!fujitsu_ir_encode_toggle(&r, stg[k], t, &n)) { fails++; continue; }
        if(n != 115) { printf("FAIL short len %zu != 115\n", n); fails++; continue; }
        if(!decode(t, n, st, &blen) || blen != 7) { fails++; continue; }
        int ok = st[5] == shorts[k].cmd && st[6] == (uint8_t)~shorts[k].cmd;
        if(!ok) fails++;
        printf("%-10s cmd %02X, complement %02X  %s\n", shorts[k].nm, st[5], st[6], ok ? "ok" : "FAIL");
    }

    for(int e = 0; e < FujitsuExtraCount; e++) {
        if(!fujitsu_ir_encode_extra(&r, (FujitsuExtra)e, t, &n) || !decode(t, n, st, &blen)) { fails++; continue; }
        if(blen != 7) { printf("FAIL extra not a short frame\n"); fails++; continue; }
        if(st[6] != (uint8_t)~st[5]) { printf("FAIL extra complement e=%d\n", e); fails++; }
    }
    printf("extra short frames ok\n");

    // Long-frame feature bits
    struct { FujitsuToggle tg; int byte; int bit; const char* nm; } bits[] = {
        {FujitsuToggleClean, 9, 3, "clean"}, {FujitsuToggleFilter, 14, 3, "filter"}};
    for(size_t k = 0; k < 2; k++) {
        FujitsuRequest rr = {FujitsuModeCool, FujitsuFanAuto, 24, 1u << bits[k].tg, 0};
        if(!fujitsu_ir_encode_state(&rr, t, &n) || !decode(t, n, st, &blen)) { fails++; continue; }
        if(!((st[bits[k].byte] >> bits[k].bit) & 1)) { printf("FAIL %s bit\n", bits[k].nm); fails++; }
        if(st[15] != ref_checksum(st)) { printf("FAIL checksum with %s\n", bits[k].nm); fails++; }
    }
    // Swing bit sits in byte 10 bits 4-5
    FujitsuRequest rs = {FujitsuModeCool, FujitsuFanAuto, 24, 1u << FujitsuToggleSwing, 0};
    fujitsu_ir_encode_state(&rs, t, &n); decode(t, n, st, &blen);
    if(((st[10] >> 4) & 3) != 1) { printf("FAIL swing field\n"); fails++; }
    printf("feature bits ok\n");

    // --- handset variants -------------------------------------------------
    // Each model must produce a distinct frame, and each must satisfy its own
    // checksum rule. ARDB1/ARJW2 are a byte shorter and use 0x9B - sum.
    printf("\nmodels:\n");
    unsigned long long seen[FujitsuModelCount];
    for(int mdl = 0; mdl < FujitsuModelCount; mdl++) {
        FujitsuRequest rm = {FujitsuModeCool, FujitsuFanAuto, 24, 0, (uint8_t)mdl};
        if(!fujitsu_ir_encode_state(&rm, t, &n) || !decode(t, n, st, &blen)) {
            printf("  %-8s encode/decode FAILED\n", fujitsu_ir_get_option_name(mdl));
            fails++; seen[mdl] = 0; continue;
        }
        int short_family = (mdl == FujitsuModelARDB1 || mdl == FujitsuModelARJW2);
        size_t want_len = short_family ? 15 : 16;
        if(blen != want_len) {
            printf("  %-8s FAIL length %zu != %zu\n", fujitsu_ir_get_option_name(mdl), blen, want_len);
            fails++;
        }
        uint8_t sum = 0, want;
        if(short_family) {
            for(size_t k = 0; k + 1 < blen; k++) sum += st[k];
            want = (uint8_t)(0x9B - sum);
        } else {
            for(size_t k = 7; k + 1 < blen; k++) sum += st[k];
            want = (uint8_t)(0 - sum);
        }
        int ok = st[blen - 1] == want;
        if(!ok) fails++;
        // ARREW4E is the only one on protocol 0x31
        unsigned want_proto = (mdl == FujitsuModelARREW4E) ? 0x31u : 0x30u;
        if(st[7] != want_proto) { printf("  FAIL protocol byte for model %d\n", mdl); fails++; }
        if(st[6] != (unsigned)(want_len - 7)) { printf("  FAIL rest length for model %d\n", mdl); fails++; }

        unsigned long long id = 0;
        for(size_t k = 0; k < blen; k++) id = id * 31 + st[k];
        seen[mdl] = id;
        printf("  %-8s %zu bytes, proto %02X, checksum %02X  %s\n",
               fujitsu_ir_get_option_name(mdl), blen, st[7], st[blen - 1], ok ? "ok" : "FAIL");
    }
    for(int a = 0; a < FujitsuModelCount; a++)
        for(int b = a + 1; b < FujitsuModelCount; b++)
            if(seen[a] && seen[a] == seen[b] &&
               !((a == FujitsuModelARRAH2E || a == FujitsuModelARREB1E || a == FujitsuModelARRY4) &&
                 (b == FujitsuModelARRAH2E || b == FujitsuModelARREB1E || b == FujitsuModelARRY4)) &&
               !((a == FujitsuModelARDB1 && b == FujitsuModelARJW2))) {
                printf("FAIL models %d and %d produce identical frames\n", a, b); fails++;
            }

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
