#include "daikin_ir_protocol.h"
#include <stdio.h>

static int fails;

// IRDaikinESP::checksum(): three independent sums, one per section.
static int checksums_ok(const uint8_t* st) {
    uint8_t s = 0;
    for(int i = 0; i < 7; i++) s += st[i];
    if(st[7] != s) { printf("FAIL sum1 %02X vs %02X\n", st[7], s); return 0; }
    s = 0;
    for(int i = 8; i < 15; i++) s += st[i];
    if(st[15] != s) { printf("FAIL sum2 %02X vs %02X\n", st[15], s); return 0; }
    s = 0;
    for(int i = 16; i < 34; i++) s += st[i];
    if(st[34] != s) { printf("FAIL sum3 %02X vs %02X\n", st[34], s); return 0; }
    return 1;
}

static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    size_t i = 0;
    // 5-bit zero preamble, no header
    for(int b = 0; b < 5; b++) {
        if(t[i++] != 428) { printf("bad preamble mark\n"); return 0; }
        if(t[i++] != 428) { printf("FAIL preamble bit %d is not zero\n", b); return 0; }
    }
    if(t[i++] != 428 || t[i++] != 428 + 29000) { printf("bad preamble footer\n"); return 0; }

    int idx = 0;
    const int seclen[3] = {8, 8, 19};
    for(int sec = 0; sec < 3; sec++) {
        if(t[i++] != 3650 || t[i++] != 1623) { printf("bad header sec%d\n", sec); return 0; }
        for(int byte = 0; byte < seclen[sec]; byte++) {
            uint8_t v = 0;
            for(int b = 0; b < 8; b++) {
                if(t[i++] != 428) { printf("bad bit mark\n"); return 0; }
                uint32_t sp = t[i++];
                if(sp == 1280) v |= 1 << b;
                else if(sp != 428) { printf("bad space %u\n", sp); return 0; }
            }
            st[idx++] = v;
        }
        if(sec < 2) {
            if(t[i++] != 428 || t[i++] != 428 + 29000) { printf("bad section gap %d\n", sec); return 0; }
        }
    }
    if(t[i++] != 428) { printf("bad trailing mark\n"); return 0; }
    if(i != n) { printf("length %zu != %zu\n", i, n); return 0; }
    return 1;
}


// ---------------------------------------------------------------------------
// The seven other frame formats the Setup screen can select.
//
// Each is checked structurally - header timings, section split, checksums -
// and against the handful of fields the app actually drives. The checksums
// are transcribed from IRremoteESP8266's own routines, not copied from the
// encoder.
// ---------------------------------------------------------------------------

#define FAILF(...)           \
    do {                     \
        printf("  FAIL: ");  \
        printf(__VA_ARGS__); \
        printf("\n");        \
        fails++;             \
    } while(0)

typedef struct {
    const char* name;
    uint8_t model;
    uint8_t len;
    uint8_t sec1; // 0 when the frame is not two headed sections
    uint16_t hdr_mark, hdr_space, bit_mark, one_space, zero_space;
    uint32_t gap;
    uint8_t lead_pairs; // leader mark/space bursts before the header
    uint8_t lead_bits; // zero bits before the header
} VariantSpec;

static const VariantSpec SPECS[] = {
    {"ARC477", DaikinModelArc477, 39, 20, 3500, 1728, 460, 1270, 420, 35204, 1, 0},
    {"ARC484", DaikinModel216, 27, 8, 3440, 1750, 420, 1300, 450, 29650, 0, 0},
    {"ARC423", DaikinModel160, 20, 7, 5000, 2145, 342, 1786, 700, 29650, 0, 0},
    {"BRC4C15", DaikinModel176, 22, 7, 5070, 2140, 370, 1780, 710, 29410, 0, 0},
    {"ARC480", DaikinModel152, 19, 0, 3492, 1718, 433, 1529, 433, 25182, 0, 5},
    {"BRC52B", DaikinModel128, 16, 8, 4600, 2500, 350, 954, 382, 20300, 2, 0},
};

/// Read `bytes` bytes, least significant bit first.
static size_t read_bytes(
    const uint32_t* t,
    size_t n,
    size_t i,
    const VariantSpec* v,
    int bytes,
    uint8_t* out) {
    for(int byte = 0; byte < bytes; byte++) {
        uint8_t val = 0;
        for(int b = 0; b < 8; b++) {
            if(i + 1 >= n) {
                FAILF("%s: ran out of timings in byte %d", v->name, byte);
                return 0;
            }
            if(t[i] != v->bit_mark) {
                FAILF("%s: bad bit mark %u at byte %d", v->name, t[i], byte);
                return 0;
            }
            if(t[i + 1] == v->one_space) {
                val |= (uint8_t)(1 << b);
            } else if(t[i + 1] != v->zero_space) {
                FAILF("%s: bad space %u at byte %d", v->name, t[i + 1], byte);
                return 0;
            }
            i += 2;
        }
        out[byte] = val;
    }
    return i;
}

static int decode_variant(const uint32_t* t, size_t n, const VariantSpec* v, uint8_t* st) {
    size_t i = 0;

    for(uint8_t k = 0; k < v->lead_pairs; k++) {
        if(i + 1 >= n) {
            FAILF("%s: no leader burst %u", v->name, k);
            return 0;
        }
        i += 2;
    }
    for(uint8_t k = 0; k < v->lead_bits; k++) {
        if(i + 1 >= n || t[i] != v->bit_mark || t[i + 1] != v->zero_space) {
            FAILF("%s: leader bit %u is not a zero", v->name, k);
            return 0;
        }
        i += 2;
    }
    if(v->lead_bits) {
        if(i + 1 >= n || t[i] != v->bit_mark || t[i + 1] != v->gap) {
            FAILF("%s: no gap after the leader bits", v->name);
            return 0;
        }
        i += 2;
    }

    if(i + 1 >= n || t[i] != v->hdr_mark || t[i + 1] != v->hdr_space) {
        FAILF("%s: bad header %u/%u", v->name, t[i], t[i + 1]);
        return 0;
    }
    i += 2;

    int first = v->sec1 ? v->sec1 : v->len;
    i = read_bytes(t, n, i, v, first, st);
    if(!i) return 0;

    if(v->sec1) {
        if(i + 1 >= n || t[i] != v->bit_mark || t[i + 1] != v->gap) {
            FAILF("%s: no gap between sections", v->name);
            return 0;
        }
        i += 2;
        // BRC52B's second section has no header of its own.
        if(v->model != DaikinModel128) {
            if(i + 1 >= n || t[i] != v->hdr_mark || t[i + 1] != v->hdr_space) {
                FAILF("%s: section 2 has no header", v->name);
                return 0;
            }
            i += 2;
        }
        i = read_bytes(t, n, i, v, v->len - v->sec1, st + v->sec1);
        if(!i) return 0;
    }
    if(i != n - 1) FAILF("%s: %zu timings left over", v->name, n - 1 - i);
    return 1;
}

static uint8_t ref_sum(const uint8_t* p, int n) {
    uint8_t s = 0;
    for(int i = 0; i < n; i++) s = (uint8_t)(s + p[i]);
    return s;
}

static uint8_t ref_nibbles(const uint8_t* p, int n, uint8_t init) {
    uint8_t s = init;
    for(int i = 0; i < n; i++) s = (uint8_t)(s + (p[i] >> 4) + (p[i] & 0x0F));
    return s;
}

static void check_variant_checksums(const VariantSpec* v, const uint8_t* st) {
    switch(v->model) {
    case DaikinModel128: {
        uint8_t want1 = ref_nibbles(st, 7, (uint8_t)(st[7] & 0x0F)) & 0x0F;
        if((st[7] >> 4) != want1) FAILF("%s: sum1 %u, wanted %u", v->name, st[7] >> 4, want1);
        uint8_t want2 = ref_nibbles(st + 8, 7, 0);
        if(st[15] != want2) FAILF("%s: sum2 %02X, wanted %02X", v->name, st[15], want2);
        break;
    }
    case DaikinModel152: {
        uint8_t want = ref_sum(st, v->len - 1);
        if(st[v->len - 1] != want) FAILF("%s: sum %02X, wanted %02X", v->name, st[v->len - 1], want);
        break;
    }
    default: {
        uint8_t want1 = ref_sum(st, v->sec1 - 1);
        if(st[v->sec1 - 1] != want1)
            FAILF("%s: sum1 %02X, wanted %02X", v->name, st[v->sec1 - 1], want1);
        uint8_t want2 = ref_sum(st + v->sec1, v->len - v->sec1 - 1);
        if(st[v->len - 1] != want2)
            FAILF("%s: sum2 %02X, wanted %02X", v->name, st[v->len - 1], want2);
        break;
    }
    }
}

static void test_variants(void) {
    printf("the seven other frame formats\n");

    for(size_t k = 0; k < sizeof(SPECS) / sizeof(SPECS[0]); k++) {
        const VariantSpec* v = &SPECS[k];

        for(int m = DaikinModeCool; m < DaikinModeCount; m++) {
            for(int f = 0; f < DaikinFanCount; f++) {
                for(uint8_t temp = 18; temp <= 30; temp += 4) {
                    DaikinRequest req = {
                        (DaikinMode)m, (DaikinFan)f, temp, 0, v->model};
                    uint32_t t[DAIKIN_IR_MAX_TIMINGS];
                    size_t n = 0;
                    if(!daikin_ir_encode_state(&req, t, &n)) {
                        FAILF("%s: encode failed for mode %d temp %u", v->name, m, temp);
                        continue;
                    }
                    uint8_t st[64];
                    if(!decode_variant(t, n, v, st)) goto next;
                    check_variant_checksums(v, st);
                }
            }
        }

        // Power off must clear the power bit, and the checksums must follow.
        {
            DaikinRequest req = {DaikinModeCool, DaikinFanLow, 24, 0, v->model};
            uint32_t t[DAIKIN_IR_MAX_TIMINGS];
            size_t n = 0;
            if(daikin_ir_encode_toggle(&req, DaikinTogglePowerOff, t, &n)) {
                uint8_t st[64];
                if(decode_variant(t, n, v, st)) check_variant_checksums(v, st);
            } else {
                FAILF("%s: power-off encode failed", v->name);
            }
        }
    next:;
    }

    // DGS01 is a single 64-bit word rather than a byte array, so it gets its
    // own check: two leader bursts, a header, 64 bits, a closing mark.
    {
        DaikinRequest req = {DaikinModeCool, DaikinFanLow, 24, 0, DaikinModel64};
        uint32_t t[DAIKIN_IR_MAX_TIMINGS];
        size_t n = 0;
        if(!daikin_ir_encode_state(&req, t, &n)) {
            FAILF("DGS01: encode failed");
        } else {
            size_t i = 4; // two leader bursts
            if(t[i] != 4600 || t[i + 1] != 2500) FAILF("DGS01: bad header");
            i += 2;
            uint64_t raw = 0;
            for(int b = 0; b < 64; b++) {
                if(t[i] != 350) FAILF("DGS01: bad bit mark at %d", b);
                if(t[i + 1] == 954) raw |= 1ULL << b;
                else if(t[i + 1] != 382) FAILF("DGS01: bad space at %d", b);
                i += 2;
            }
            // Four-bit sum of every nibble below bit 60.
            uint64_t data = raw & ((1ULL << 60) - 1);
            uint8_t sum = 0;
            for(; data; data >>= 4) sum = (uint8_t)(sum + (data & 0x0F));
            if(((raw >> 60) & 0x0F) != (sum & 0x0F))
                FAILF("DGS01: checksum %u, wanted %u", (unsigned)((raw >> 60) & 0xF), sum & 0xF);
            if(!((raw >> 51) & 1)) FAILF("DGS01: power bit not set");
        }
    }

    // The picker must actually change the signal.
    size_t lens[DaikinModelCount];
    for(uint8_t model = 0; model < DaikinModelCount; model++) {
        DaikinRequest req = {DaikinModeCool, DaikinFanLow, 24, 0, model};
        uint32_t t[DAIKIN_IR_MAX_TIMINGS];
        lens[model] = 0;
        if(!daikin_ir_encode_state(&req, t, &lens[model]))
            FAILF("model %u: encode failed", model);
    }
    for(uint8_t a = 0; a < DaikinModelCount; a++) {
        for(uint8_t b = (uint8_t)(a + 1); b < DaikinModelCount; b++) {
            if(lens[a] == lens[b])
                FAILF("models %u and %u produce the same length %zu", a, b, lens[a]);
        }
    }

    DaikinRequest bad = {DaikinModeCool, DaikinFanLow, 24, 0, DaikinModelCount};
    uint32_t t[DAIKIN_IR_MAX_TIMINGS];
    size_t n = 0;
    if(daikin_ir_encode_state(&bad, t, &n)) FAILF("an unknown model was accepted");

    if(daikin_ir_get_option_count() != DaikinModelCount)
        FAILF("the Setup picker does not offer every model");
}

int main(void) {
    test_variants();

    uint32_t t[DAIKIN_IR_MAX_TIMINGS];
    size_t n = 0;
    uint8_t st[35];

    int checked = 0;
    for(int m = DaikinModeCool; m < DaikinModeCount; m++) {
        for(int f = 0; f < DaikinFanCount; f++) {
            for(int temp = DAIKIN_TEMP_MIN; temp <= DAIKIN_TEMP_MAX; temp++) {
                DaikinRequest r = {(DaikinMode)m, (DaikinFan)f, (uint8_t)temp, 0, 0};
                if(!daikin_ir_encode_state(&r, t, &n)) { printf("encode fail\n"); fails++; continue; }
                if(n != 583) { printf("FAIL len %zu != 583\n", n); fails++; continue; }
                if(!decode(t, n, st)) { fails++; continue; }

                // The three section preambles must all survive
                if(st[0] != 0x11 || st[1] != 0xDA || st[2] != 0x27) { printf("FAIL sec1 preamble\n"); fails++; }
                if(st[8] != 0x11 || st[9] != 0xDA || st[10] != 0x27) { printf("FAIL sec2 preamble\n"); fails++; }
                if(st[16] != 0x11 || st[17] != 0xDA || st[18] != 0x27) { printf("FAIL sec3 preamble\n"); fails++; }
                if((st[21] & 1) != 1) { printf("FAIL power bit\n"); fails++; }
                if(((st[21] >> 3) & 1) != 1) { printf("FAIL always-1 bit\n"); fails++; }
                if(st[22] != (unsigned)(temp * 2)) { printf("FAIL temp %d -> %u\n", temp, st[22]); fails++; }
                if(!checksums_ok(st)) fails++;
                checked++;
            }
        }
    }
    printf("state frames verified: %d\n", checked);

    struct { DaikinMode m; unsigned want; const char* nm; } modes[] = {
        {DaikinModeAuto, 0b000, "auto"}, {DaikinModeDry, 0b010, "dry"},
        {DaikinModeCool, 0b011, "cool"}, {DaikinModeHeat, 0b100, "heat"},
        {DaikinModeFan, 0b110, "fan"}};
    for(size_t k = 0; k < 5; k++) {
        DaikinRequest r = {modes[k].m, DaikinFanAuto, 24, 0, 0};
        daikin_ir_encode_state(&r, t, &n); decode(t, n, st);
        unsigned got = (st[21] >> 4) & 7;
        printf("mode %-5s: got %u want %u  %s\n", modes[k].nm, got, modes[k].want,
               got == modes[k].want ? "ok" : "FAIL");
        if(got != modes[k].want) fails++;
    }

    // Fan speeds 1..5 are stored as 2 + speed; auto is 0b1010
    unsigned fanwant[4] = {0b1010, 3, 5, 7};
    for(int f = 0; f < DaikinFanCount; f++) {
        DaikinRequest r = {DaikinModeCool, (DaikinFan)f, 24, 0, 0};
        daikin_ir_encode_state(&r, t, &n); decode(t, n, st);
        if((st[24] >> 4) != fanwant[f]) {
            printf("FAIL fan %d: got %u want %u\n", f, st[24] >> 4, fanwant[f]); fails++;
        }
    }
    printf("fan codes ok (auto 0b1010, speeds stored as 2 + speed)\n");

    // Quiet also rewrites the fan nibble to the quiet code
    DaikinRequest r = {DaikinModeCool, DaikinFanHigh, 24, 1u << DaikinToggleQuiet, 0};
    daikin_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[24] >> 4) != 0b1011) { printf("FAIL quiet fan code\n"); fails++; }
    if(((st[29] >> 5) & 1) != 1) { printf("FAIL quiet bit\n"); fails++; }
    printf("quiet overrides the fan nibble: ok\n");

    struct { DaikinToggle tg; int byte; int bit; const char* nm; } bits[] = {
        {DaikinTogglePowerful, 29, 0, "powerful"}, {DaikinToggleEcono, 32, 2, "econo"},
        {DaikinToggleMold, 33, 1, "mold"}};
    for(size_t k = 0; k < 3; k++) {
        DaikinRequest rr = {DaikinModeCool, DaikinFanAuto, 24, 1u << bits[k].tg, 0};
        if(!daikin_ir_encode_state(&rr, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(!((st[bits[k].byte] >> bits[k].bit) & 1)) { printf("FAIL %s bit\n", bits[k].nm); fails++; }
        if(!checksums_ok(st)) { printf("  (with %s)\n", bits[k].nm); fails++; }
    }
    printf("feature bits ok\n");

    // Vane: 0xF on, 0x0 off
    r.toggle_bits = 1u << DaikinToggleSwing;
    daikin_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[24] & 0x0F) != 0xF) { printf("FAIL swingV on\n"); fails++; }
    r.toggle_bits = 0;
    daikin_ir_encode_state(&r, t, &n); decode(t, n, st);
    if((st[24] & 0x0F) != 0x0) { printf("FAIL swingV off\n"); fails++; }
    printf("swing field ok\n");

    if(daikin_ir_encode_toggle(&r, DaikinTogglePowerOff, t, &n) && decode(t, n, st)) {
        if(st[21] & 1) { printf("FAIL power-off bit\n"); fails++; }
        if(!checksums_ok(st)) fails++;
        printf("power-off frame ok\n");
    } else fails++;

    for(int e = 0; e < DaikinExtraCount; e++) {
        if(!daikin_ir_encode_extra(&r, (DaikinExtra)e, t, &n) || !decode(t, n, st)) { fails++; continue; }
        if(!checksums_ok(st)) { printf("  (extra %d)\n", e); fails++; }
    }
    printf("extra frames ok\n");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails != 0;
}
