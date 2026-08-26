// Rebuilds the 14 bytes from the timings and checks them against
// IRremoteESP8266's layout. The section checksum is transcribed independently
// from calcSectionChecksum() - population counts, not sums - rather than
// copied from the encoder.

#include "samsung_ir_protocol.h"
#include <stdio.h>
#include <string.h>

static int fails;

#define FAILF(...)           \
    do {                     \
        printf("  FAIL: ");  \
        printf(__VA_ARGS__); \
        printf("\n");        \
        fails++;             \
    } while(0)

#define STATE_LEN   14
#define SECTION_LEN 7

static uint8_t ref_popcount(uint8_t v) {
    uint8_t n = 0;
    for(int i = 0; i < 8; i++) n = (uint8_t)(n + ((v >> i) & 1));
    return n;
}

static uint8_t ref_section_checksum(const uint8_t* s) {
    uint8_t sum = 0;
    sum = (uint8_t)(sum + ref_popcount(s[0]));
    sum = (uint8_t)(sum + ref_popcount((uint8_t)(s[1] & 0x0F)));
    sum = (uint8_t)(sum + ref_popcount((uint8_t)(s[2] >> 4)));
    for(int i = 3; i < 7; i++) sum = (uint8_t)(sum + ref_popcount(s[i]));
    return (uint8_t)(sum ^ 0xFF);
}

/// Decode `sections` seven-byte sections. The standard message has two; the
/// extended one the handset sends for power and sleep has three.
static int decode_n(const uint32_t* t, size_t n, uint8_t* st, int sections) {
    memset(st, 0, (size_t)sections * SECTION_LEN);
    size_t i = 0;

    if(n < 4 || t[i] != 690 || t[i + 1] != 17844) {
        FAILF("bad lead-in");
        return 0;
    }
    i += 2;

    for(int s = 0; s < sections; s++) {
        if(i + 1 >= n || t[i] != 3086 || t[i + 1] != 8864) {
            FAILF("bad section %d header", s);
            return 0;
        }
        i += 2;

        for(int byte = 0; byte < SECTION_LEN; byte++) {
            uint8_t v = 0;
            for(int b = 0; b < 8; b++) {
                if(i + 1 >= n) {
                    FAILF("ran out of timings in section %d byte %d", s, byte);
                    return 0;
                }
                if(t[i] != 586) {
                    FAILF("bad bit mark %u", t[i]);
                    return 0;
                }
                uint32_t sp = t[i + 1];
                if(sp == 1432) {
                    v |= (uint8_t)(1 << b);
                } else if(sp != 436) {
                    FAILF("bad space %u in section %d byte %d", sp, s, byte);
                    return 0;
                }
                i += 2;
            }
            st[s * SECTION_LEN + byte] = v;
        }

        if(s + 1 < sections) {
            if(i + 1 >= n || t[i] != 586 || t[i + 1] != 2886) {
                FAILF("missing gap after section %d", s);
                return 0;
            }
            i += 2;
        }
    }

    if(i != n - 1 || t[i] != 586) {
        FAILF("expected a trailing bit mark, stopped at %zu of %zu", i, n);
        return 0;
    }
    return 1;
}

static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    return decode_n(t, n, st, STATE_LEN / SECTION_LEN);
}

static void check_checksums(const uint8_t* st) {
    for(int s = 0; s < 2; s++) {
        const uint8_t* sec = st + s * SECTION_LEN;
        uint8_t want = ref_section_checksum(sec);
        uint8_t got = (uint8_t)((sec[1] >> 4) | ((sec[2] & 0x0F) << 4));
        if(got != want) FAILF("section %d checksum %02X, wanted %02X", s, got, want);
    }
}

static void check(SamsungMode mode, SamsungFan fan, uint8_t temp, uint32_t toggles) {
    SamsungRequest req = {mode, fan, temp, toggles, 0};
    uint32_t t[SAMSUNG_IR_MAX_TIMINGS];
    size_t n = 0;

    if(!samsung_ir_encode_state(&req, t, &n)) {
        FAILF("encode failed for mode %d fan %d temp %u", mode, fan, temp);
        return;
    }

    uint8_t st[STATE_LEN];
    if(!decode(t, n, st)) return;

    static const uint8_t MODE_WIRE[SamsungModeCount] = {0, 1, 0, 2, 4, 3};
    if(((st[12] >> 4) & 0b111) != MODE_WIRE[mode])
        FAILF("mode %d encoded as %u", mode, (st[12] >> 4) & 7);

    // Auto mode accepts exactly one fan value, and no other mode accepts it.
    static const uint8_t FAN_WIRE[SamsungFanCount] = {0, 2, 4, 5, 7};
    uint8_t want_fan = (MODE_WIRE[mode] == 0) ? 6 : FAN_WIRE[fan];
    if(((st[12] >> 1) & 0b111) != want_fan)
        FAILF("fan %d in mode %d encoded as %u, wanted %u", fan, mode, (st[12] >> 1) & 7, want_fan);

    uint8_t want_temp = temp;
    if(want_temp < 16) want_temp = 16;
    if(want_temp > 30) want_temp = 30;
    if((st[11] >> 4) != (want_temp - 16)) FAILF("temp %u encoded as %u", temp, (st[11] >> 4) + 16);

    // Power is stored twice and both copies must agree.
    if(((st[6] >> 4) & 0b11) != 0b11) FAILF("power copy 1 not on");
    if(((st[13] >> 4) & 0b11) != 0b11) FAILF("power copy 2 not on");

    bool v = (toggles >> SamsungToggleSwingV) & 1;
    bool h = (toggles >> SamsungToggleSwingH) & 1;
    uint8_t want_swing = (v && h) ? 0b100 : v ? 0b010 : h ? 0b011 : 0b111;
    if(((st[9] >> 4) & 0b111) != want_swing)
        FAILF("swing is %u, wanted %u", (st[9] >> 4) & 7, want_swing);

    if((bool)((st[5] >> 5) & 1) != (bool)((toggles >> SamsungToggleQuiet) & 1))
        FAILF("quiet bit wrong");
    if((bool)((st[5] >> 4) & 1) != (bool)((toggles >> SamsungToggleSleep) & 1))
        FAILF("sleep bit wrong");
    if((bool)(st[11] & 1) != (bool)((toggles >> SamsungToggleIon) & 1)) FAILF("ion bit wrong");
    if((bool)((st[10] >> 4) & 1) != (bool)((toggles >> SamsungToggleDisplay) & 1))
        FAILF("display bit wrong");

    check_checksums(st);
}

/// A real handset sends the long message for power and sleep. Check that we
/// do the same, that the inserted middle section is the documented one, and
/// that all three sections carry their own checksum.
static void test_extended_frame(void) {
    SamsungRequest req = {SamsungModeCool, SamsungFanLow, 24, 0, 0};
    uint32_t t[SAMSUNG_IR_MAX_TIMINGS];
    size_t n = 0;

    if(!samsung_ir_encode_toggle(&req, SamsungTogglePowerOff, t, &n)) {
        FAILF("power-off encode failed");
        return;
    }

    uint8_t ext[21];
    if(!decode_n(t, n, ext, 3)) return;

    if(((ext[6] >> 4) & 0b11) != 0) FAILF("power copy 1 still on");
    if(((ext[20] >> 4) & 0b11) != 0) FAILF("power copy 2 still on");

    // The middle section is fixed apart from its own checksum nibbles.
    static const uint8_t want_middle[SECTION_LEN] = {0x01, 0xD2, 0x0F, 0x00, 0x00, 0x00, 0x00};
    for(int k = 0; k < SECTION_LEN; k++) {
        if(k == 1 || k == 2) continue; // checksum nibbles live here
        if(ext[SECTION_LEN + k] != want_middle[k])
            FAILF("middle section byte %d is %02X, wanted %02X", k, ext[SECTION_LEN + k], want_middle[k]);
    }

    for(int s = 0; s < 3; s++) {
        const uint8_t* sec = ext + s * SECTION_LEN;
        uint8_t want = ref_section_checksum(sec);
        uint8_t got = (uint8_t)((sec[1] >> 4) | ((sec[2] & 0x0F) << 4));
        if(got != want) FAILF("extended section %d checksum %02X, wanted %02X", s, got, want);
    }

    // Everything that is not power or sleep stays on the short message.
    size_t short_n = 0;
    if(!samsung_ir_encode_toggle(&req, SamsungToggleQuiet, t, &short_n)) {
        FAILF("quiet encode failed");
        return;
    }
    if(short_n >= n) FAILF("a quiet press sent the long message");
}

static void test_extras(void) {
    SamsungRequest req = {SamsungModeCool, SamsungFanLow, 24, 0, 0};
    static const uint8_t WANT_SPECIAL[4] = {0b011, 0b101, 0b111, 0b000};

    for(int e = 0; e < SamsungExtraCount; e++) {
        uint32_t t[SAMSUNG_IR_MAX_TIMINGS];
        size_t n = 0;
        if(!samsung_ir_encode_extra(&req, (SamsungExtra)e, t, &n)) {
            FAILF("extra %d encode failed", e);
            continue;
        }
        uint8_t st[STATE_LEN];
        if(!decode(t, n, st)) continue;

        if(e <= SamsungExtraSpecialOff) {
            if(((st[10] >> 1) & 0b111) != WANT_SPECIAL[e])
                FAILF("extra %d special field is %u, wanted %u", e, (st[10] >> 1) & 7, WANT_SPECIAL[e]);
        } else if(e == SamsungExtraSwingBoth) {
            if(((st[9] >> 4) & 0b111) != 0b100) FAILF("swing both not set");
        } else if(e == SamsungExtraSwingNone) {
            if(((st[9] >> 4) & 0b111) != 0b111) FAILF("swing off not set");
        } else if(e == SamsungExtraClean) {
            if(!((st[10] >> 7) & 1) || !((st[11] >> 1) & 1)) FAILF("clean toggles not both set");
        } else {
            if(!((st[13] >> 2) & 1)) FAILF("beep toggle not set");
        }
        check_checksums(st);
    }
}

int main(void) {
    printf("samsung protocol\n");

    for(int m = SamsungModeCool; m < SamsungModeCount; m++) {
        for(int f = 0; f < SamsungFanCount; f++) {
            for(uint8_t temp = 16; temp <= 30; temp++) {
                check((SamsungMode)m, (SamsungFan)f, temp, 0);
            }
        }
    }

    for(int t = SamsungToggleSwingV; t < SamsungToggleCount; t++) {
        check(SamsungModeCool, SamsungFanLow, 24, 1u << t);
    }
    check(SamsungModeHeat, SamsungFanHigh, 28, 0x7E);

    test_extended_frame();
    test_extras();

    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ok", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
