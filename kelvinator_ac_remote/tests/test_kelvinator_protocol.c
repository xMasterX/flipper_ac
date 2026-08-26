// Rebuilds the 16 bytes from the timings and checks them against
// IRremoteESP8266's layout, with the block checksum transcribed independently
// from calcBlockChecksum() rather than copied from the encoder.

#include "kelvinator_ir_protocol.h"
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

#define STATE_LEN 16

// IRKelvinatorAC::calcBlockChecksum(block, 8): seed 10, plus the low nibbles
// of the first four bytes, plus the high nibbles of the next three.
static uint8_t ref_block_checksum(const uint8_t* block) {
    uint8_t sum = 10;
    const uint8_t* p = block;
    for(uint8_t i = 0; i < 4 && i < 7; i++, p++) sum = (uint8_t)(sum + (*p & 0x0F));
    for(uint8_t i = 4; i < 7; i++, p++) sum = (uint8_t)(sum + (*p >> 4));
    return sum & 0x0F;
}

/// Read `bits` bits, least significant first, from timing pairs.
static size_t read_bits(const uint32_t* t, size_t n, size_t i, int bits, uint8_t* out) {
    for(int b = 0; b < bits; b++) {
        if(i + 1 >= n) {
            FAILF("ran out of timings at bit %d", b);
            return 0;
        }
        if(t[i] != 680) {
            FAILF("bad bit mark %u at bit %d", t[i], b);
            return 0;
        }
        uint32_t sp = t[i + 1];
        if(sp == 1530) {
            out[b / 8] |= (uint8_t)(1 << (b % 8));
        } else if(sp != 510) {
            FAILF("bad space %u at bit %d", sp, b);
            return 0;
        }
        i += 2;
    }
    return i;
}

static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    memset(st, 0, STATE_LEN);
    size_t i = 0;

    for(int block = 0; block < 2; block++) {
        if(i + 1 >= n || t[i] != 9010 || t[i + 1] != 4505) {
            FAILF("bad header for block %d", block);
            return 0;
        }
        i += 2;

        i = read_bits(t, n, i, 32, st + block * 8);
        if(!i) return 0;

        // Three constant bits, 0b010 least significant first.
        uint8_t marker = 0;
        i = read_bits(t, n, i, 3, &marker);
        if(!i) return 0;
        if(marker != 0b010) FAILF("block %d marker is %u, wanted 2", block, marker);

        if(i + 1 >= n || t[i] != 680 || t[i + 1] != 19975) {
            FAILF("missing gap after block %d chunk 1", block);
            return 0;
        }
        i += 2;

        i = read_bits(t, n, i, 32, st + block * 8 + 4);
        if(!i) return 0;

        if(block == 0) {
            if(i + 1 >= n || t[i] != 680 || t[i + 1] != 39950) {
                FAILF("missing long gap between blocks");
                return 0;
            }
            i += 2;
        }
    }

    if(i != n - 1 || t[i] != 680) {
        FAILF("expected a trailing bit mark, stopped at %zu of %zu", i, n);
        return 0;
    }
    return 1;
}

static void check(KelvinatorMode mode, KelvinatorFan fan, uint8_t temp, uint32_t toggles) {
    KelvinatorRequest req = {mode, fan, temp, toggles, 0};
    uint32_t t[KELVINATOR_IR_MAX_TIMINGS];
    size_t n = 0;

    if(!kelvinator_ir_encode_state(&req, t, &n)) {
        FAILF("encode failed for mode %d fan %d temp %u", mode, fan, temp);
        return;
    }

    uint8_t st[STATE_LEN];
    if(!decode(t, n, st)) return;

    static const uint8_t MODE_WIRE[KelvinatorModeCount] = {0, 1, 0, 2, 4, 3};
    if((st[0] & 0x07) != MODE_WIRE[mode]) FAILF("mode %d encoded as %u", mode, st[0] & 7);
    if(!((st[0] >> 3) & 1)) FAILF("power bit not set");

    static const uint8_t FAN_WIRE[KelvinatorFanCount] = {0, 1, 2, 3};
    uint8_t want_fan = FAN_WIRE[fan];
    if(((st[14] >> 4) & 0x07) != want_fan) FAILF("fan %d encoded as %u", fan, st[14] >> 4);
    uint8_t want_basic = want_fan > 3 ? 3 : want_fan;
    if(((st[0] >> 4) & 0x03) != want_basic) FAILF("basic fan is %u, wanted %u", (st[0] >> 4) & 3, want_basic);

    // Auto and Dry are pinned to 25 C regardless of what the user picked.
    uint8_t want_temp = temp;
    if(mode == KelvinatorModeAuto || mode == KelvinatorModeDry) want_temp = 25;
    if(want_temp < 16) want_temp = 16;
    if(want_temp > 30) want_temp = 30;
    if((st[1] & 0x0F) != (want_temp - 16))
        FAILF("temp %u in mode %d encoded as %u", temp, mode, (st[1] & 0x0F) + 16);

    if(st[3] != 0x50) FAILF("byte 3 is %02X, wanted 50", st[3]);
    if(st[11] != 0x70) FAILF("byte 11 is %02X, wanted 70", st[11]);

    if(st[8] != st[0] || st[9] != st[1] || st[10] != st[2])
        FAILF("bytes 8-10 are not copies of 0-2");

    // X-Fan is only meaningful in Cool and Dry.
    bool want_xfan = ((toggles >> KelvinatorToggleXfan) & 1) &&
                     (mode == KelvinatorModeCool || mode == KelvinatorModeDry);
    if((bool)((st[2] >> 7) & 1) != want_xfan) FAILF("X-Fan bit wrong in mode %d", mode);

    // Turbo is dropped once the fan speed is chosen by hand.
    bool want_turbo = ((toggles >> KelvinatorToggleTurbo) & 1) && fan == KelvinatorFanAuto;
    if((bool)((st[2] >> 4) & 1) != want_turbo) FAILF("turbo bit wrong for fan %d", fan);

    if((bool)((st[12] >> 7) & 1) != (bool)((toggles >> KelvinatorToggleQuiet) & 1))
        FAILF("quiet bit wrong");

    bool sh = (toggles >> KelvinatorToggleSwingH) & 1;
    bool sv = (toggles >> KelvinatorToggleSwingV) & 1;
    if((bool)((st[4] >> 4) & 1) != sh) FAILF("swing H bit wrong");
    if((st[4] & 0x0F) != (sv ? 1u : 0u)) FAILF("vane is %u, wanted %u", st[4] & 0x0F, sv ? 1 : 0);
    if((bool)((st[0] >> 6) & 1) != (sv || sh)) FAILF("swing-auto bit wrong");

    if((st[7] >> 4) != ref_block_checksum(st))
        FAILF("block 1 checksum %u, wanted %u", st[7] >> 4, ref_block_checksum(st));
    if((st[15] >> 4) != ref_block_checksum(st + 8))
        FAILF("block 2 checksum %u, wanted %u", st[15] >> 4, ref_block_checksum(st + 8));
}

static void test_power_off(void) {
    KelvinatorRequest req = {KelvinatorModeCool, KelvinatorFanLow, 22, 0, 0};
    uint32_t t[KELVINATOR_IR_MAX_TIMINGS];
    size_t n = 0;
    if(!kelvinator_ir_encode_toggle(&req, KelvinatorTogglePowerOff, t, &n)) {
        FAILF("power-off encode failed");
        return;
    }
    uint8_t st[STATE_LEN];
    if(!decode(t, n, st)) return;
    if((st[0] >> 3) & 1) FAILF("power bit still set on a power-off frame");
    if((st[7] >> 4) != ref_block_checksum(st)) FAILF("power-off checksum wrong");
}

static void test_extras(void) {
    KelvinatorRequest req = {KelvinatorModeCool, KelvinatorFanLow, 22, 0, 0};
    static const uint8_t WANT_VANE[6] = {2, 3, 4, 5, 6, 0};

    for(int e = 0; e < KelvinatorExtraCount; e++) {
        uint32_t t[KELVINATOR_IR_MAX_TIMINGS];
        size_t n = 0;
        if(!kelvinator_ir_encode_extra(&req, (KelvinatorExtra)e, t, &n)) {
            FAILF("extra %d encode failed", e);
            continue;
        }
        uint8_t st[STATE_LEN];
        if(!decode(t, n, st)) continue;

        if(e <= KelvinatorExtraVaneOff) {
            if((st[4] & 0x0F) != WANT_VANE[e])
                FAILF("extra %d set vane %u, wanted %u", e, st[4] & 0x0F, WANT_VANE[e]);
        } else {
            bool want_ion = e == KelvinatorExtraIonOn;
            if((bool)((st[2] >> 6) & 1) != want_ion) FAILF("extra %d ion bit wrong", e);
        }
        if((st[15] >> 4) != ref_block_checksum(st + 8)) FAILF("extra %d checksum wrong", e);
    }
}

int main(void) {
    printf("kelvinator protocol\n");

    for(int m = KelvinatorModeCool; m < KelvinatorModeCount; m++) {
        for(int f = 0; f < KelvinatorFanCount; f++) {
            for(uint8_t temp = 16; temp <= 30; temp++) {
                check((KelvinatorMode)m, (KelvinatorFan)f, temp, 0);
            }
        }
    }

    for(int t = KelvinatorToggleSwingV; t < KelvinatorToggleCount; t++) {
        check(KelvinatorModeCool, KelvinatorFanAuto, 22, 1u << t);
        check(KelvinatorModeHeat, KelvinatorFanHigh, 28, 1u << t);
    }
    check(KelvinatorModeCool, KelvinatorFanAuto, 22, 0x7E);

    test_power_off();
    test_extras();

    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ok", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
