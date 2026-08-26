// Rebuilds the 18 bytes from the timings the encoder produces and checks them
// against IRremoteESP8266's documented layout, transcribed independently from
// ir_Mitsubishi.h's bitfield union rather than copied from the encoder.

#include "mitsubishi_ir_protocol.h"
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

#define STATE_LEN 18

// IRMitsubishiAC::calculateChecksum -> sumBytes(data, len - 1).
static uint8_t ref_checksum(const uint8_t* b) {
    uint8_t sum = 0;
    for(int i = 0; i < STATE_LEN - 1; i++) sum = (uint8_t)(sum + b[i]);
    return sum;
}

/// Decode one copy of the frame, returning how many timings it consumed.
static size_t decode_copy(const uint32_t* t, size_t n, size_t i, uint8_t* st) {
    if(i + 1 >= n || t[i] != 3400 || t[i + 1] != 1750) {
        FAILF("bad header at %zu", i);
        return 0;
    }
    i += 2;
    for(int byte = 0; byte < STATE_LEN; byte++) {
        uint8_t v = 0;
        for(int b = 0; b < 8; b++) {
            if(i + 1 >= n) {
                FAILF("ran out of timings in byte %d", byte);
                return 0;
            }
            if(t[i] != 450) {
                FAILF("bad bit mark %u at byte %d", t[i], byte);
                return 0;
            }
            uint32_t sp = t[i + 1];
            if(sp == 1300) {
                v |= (uint8_t)(1 << b); // least significant bit first
            } else if(sp != 420) {
                FAILF("bad space %u at byte %d", sp, byte);
                return 0;
            }
            i += 2;
        }
        st[byte] = v;
    }
    return i;
}

/// The whole transmission: two identical copies, 15.5 ms apart.
static int decode(const uint32_t* t, size_t n, uint8_t* st) {
    size_t i = decode_copy(t, n, 0, st);
    if(!i) return 0;

    if(i + 1 >= n || t[i] != 440 || t[i + 1] != 15500) {
        FAILF("missing repeat gap after the first copy");
        return 0;
    }
    i += 2;

    uint8_t second[STATE_LEN];
    i = decode_copy(t, n, i, second);
    if(!i) return 0;

    if(memcmp(st, second, STATE_LEN)) {
        FAILF("the two copies differ");
        return 0;
    }
    if(i != n - 1 || t[i] != 440) {
        FAILF("expected a trailing 440 us mark, got %zu of %zu timings", i, n);
        return 0;
    }
    return 1;
}

static void check_state(MitsubishiMode mode, MitsubishiFan fan, uint8_t temp, uint32_t toggles) {
    MitsubishiRequest req = {mode, fan, temp, toggles, MitsubishiModel144};
    uint32_t t[MITSUBISHI_IR_MAX_TIMINGS];
    size_t n = 0;

    if(!mitsubishi_ir_encode_state(&req, t, &n)) {
        FAILF("encode failed for mode %d fan %d temp %u", mode, fan, temp);
        return;
    }

    uint8_t st[STATE_LEN];
    if(!decode(t, n, st)) return;

    // Preamble
    static const uint8_t want[5] = {0x23, 0xCB, 0x26, 0x01, 0x00};
    if(memcmp(st, want, 5)) FAILF("preamble is %02X %02X %02X %02X %02X", st[0], st[1], st[2], st[3], st[4]);

    if(!((st[5] >> 5) & 1)) FAILF("power bit not set on a state frame");

    static const uint8_t MODE_WIRE[MitsubishiModeCount] = {0b100, 0b011, 0b100, 0b010, 0b001, 0b111};
    uint8_t got_mode = (uint8_t)((st[6] >> 3) & 0x07);
    if(got_mode != MODE_WIRE[mode]) FAILF("mode %d encoded as %u", mode, got_mode);

    uint8_t want_temp = temp;
    if(want_temp < 16) want_temp = 16;
    if(want_temp > 31) want_temp = 31;
    if((st[7] & 0x0F) != (want_temp - 16)) FAILF("temp %u encoded as %u", temp, st[7] & 0x0F);

    // The low nibble of byte 8 is the mode-dependent value IRremoteESP8266
    // writes in setMode(); zero there is not equivalent.
    static const uint8_t B8_LOW[MitsubishiModeCount] = {0, 0b0110, 0, 0b0010, 0, 0b0111};
    if((st[8] & 0x0F) != B8_LOW[mode]) FAILF("byte 8 low nibble is %u for mode %d", st[8] & 0x0F, mode);

    bool quiet = (toggles >> MitsubishiToggleQuiet) & 1;
    static const uint8_t FAN_WIRE[MitsubishiFanCount] = {0, 1, 2, 3};
    uint8_t want_fan = quiet ? 6 : FAN_WIRE[fan];
    if((st[9] & 0x07) != want_fan) FAILF("fan %d encoded as %u", fan, st[9] & 0x07);
    if(!((st[9] >> 6) & 1)) FAILF("vane-valid bit not set");
    bool want_fan_auto = (want_fan == 0);
    if((bool)((st[9] >> 7) & 1) != want_fan_auto) FAILF("fan-auto bit wrong for fan %d", fan);

    bool swing_v = (toggles >> MitsubishiToggleSwingV) & 1;
    uint8_t want_vane = swing_v ? 0b111 : 0b000;
    if(((st[9] >> 3) & 0x07) != want_vane) FAILF("vane is %u, wanted %u", (st[9] >> 3) & 7, want_vane);

    bool swing_h = (toggles >> MitsubishiToggleSwingH) & 1;
    uint8_t want_wide = swing_h ? 0b1000 : 0b0011;
    if(((st[8] >> 4) & 0x0F) != want_wide) FAILF("wide vane is %u, wanted %u", st[8] >> 4, want_wide);

    if((bool)((st[14] >> 5) & 1) != (bool)((toggles >> MitsubishiToggleEcono) & 1))
        FAILF("econo bit wrong");
    if((bool)((st[16] >> 1) & 1) != (bool)((toggles >> MitsubishiToggleNatural) & 1))
        FAILF("natural bit wrong");

    if(st[17] != ref_checksum(st)) FAILF("checksum %02X, wanted %02X", st[17], ref_checksum(st));
}

static void test_power_off(void) {
    MitsubishiRequest req = {MitsubishiModeCool, MitsubishiFanAuto, 24, 0, MitsubishiModel144};
    uint32_t t[MITSUBISHI_IR_MAX_TIMINGS];
    size_t n = 0;
    if(!mitsubishi_ir_encode_toggle(&req, MitsubishiTogglePowerOff, t, &n)) {
        FAILF("power-off encode failed");
        return;
    }
    uint8_t st[STATE_LEN];
    if(!decode(t, n, st)) return;
    if((st[5] >> 5) & 1) FAILF("power bit still set on a power-off frame");
    if(st[17] != ref_checksum(st)) FAILF("power-off checksum wrong");
}

static void test_extras(void) {
    MitsubishiRequest req = {MitsubishiModeCool, MitsubishiFanLow, 24, 0, MitsubishiModel144};
    static const uint8_t WANT_VANE[5] = {0b001, 0b010, 0b011, 0b100, 0b101};
    static const uint8_t WANT_WIDE[6] = {0b0001, 0b0010, 0b0011, 0b0100, 0b0101, 0b0110};

    for(int e = 0; e < MitsubishiExtraCount; e++) {
        uint32_t t[MITSUBISHI_IR_MAX_TIMINGS];
        size_t n = 0;
        if(!mitsubishi_ir_encode_extra(&req, (MitsubishiExtra)e, t, &n)) {
            FAILF("extra %d encode failed", e);
            continue;
        }
        uint8_t st[STATE_LEN];
        if(!decode(t, n, st)) continue;

        if(e <= MitsubishiExtraVaneLowest) {
            if(((st[9] >> 3) & 0x07) != WANT_VANE[e])
                FAILF("extra %d set vane %u, wanted %u", e, (st[9] >> 3) & 7, WANT_VANE[e]);
        } else {
            int w = e - MitsubishiExtraWideLeftMax;
            if(((st[8] >> 4) & 0x0F) != WANT_WIDE[w])
                FAILF("extra %d set wide %u, wanted %u", e, st[8] >> 4, WANT_WIDE[w]);
        }
        if(st[17] != ref_checksum(st)) FAILF("extra %d checksum wrong", e);
    }
}


// ---------------------------------------------------------------------------
// The two other frame formats the Setup screen can select.
// ---------------------------------------------------------------------------

#define S112_LEN 14
#define S136_LEN 17

/// Read a single-copy frame: header, N bytes least significant bit first, and
/// a closing mark.
static int decode_single(
    const uint32_t* t,
    size_t n,
    uint32_t hdr_mark,
    uint32_t hdr_space,
    uint32_t bit_mark,
    uint32_t one,
    uint32_t zero,
    int bytes,
    uint8_t* st) {
    if(n < 4 || t[0] != hdr_mark || t[1] != hdr_space) {
        FAILF("bad header %u/%u", t[0], t[1]);
        return 0;
    }
    size_t i = 2;
    for(int byte = 0; byte < bytes; byte++) {
        uint8_t v = 0;
        for(int b = 0; b < 8; b++) {
            if(i + 1 >= n) {
                FAILF("ran out of timings in byte %d", byte);
                return 0;
            }
            if(t[i] != bit_mark) {
                FAILF("bad bit mark %u at byte %d", t[i], byte);
                return 0;
            }
            if(t[i + 1] == one) {
                v |= (uint8_t)(1 << b);
            } else if(t[i + 1] != zero) {
                FAILF("bad space %u at byte %d", t[i + 1], byte);
                return 0;
            }
            i += 2;
        }
        st[byte] = v;
    }
    if(i != n - 1 || t[i] != bit_mark) {
        FAILF("expected a trailing mark, stopped at %zu of %zu", i, n);
        return 0;
    }
    return 1;
}

/// IRTcl112Ac::calcChecksum - a plain byte sum, which MITSUBISHI112 borrows.
static uint8_t ref_sum(const uint8_t* b, int len) {
    uint8_t sum = 0;
    for(int i = 0; i < len - 1; i++) sum = (uint8_t)(sum + b[i]);
    return sum;
}

static void check_112(MitsubishiMode mode, MitsubishiFan fan, uint8_t temp) {
    MitsubishiRequest req = {mode, fan, temp, 0, MitsubishiModel112};
    uint32_t t[MITSUBISHI_IR_MAX_TIMINGS];
    size_t n = 0;
    if(!mitsubishi_ir_encode_state(&req, t, &n)) {
        FAILF("112: encode failed for mode %d temp %u", mode, temp);
        return;
    }
    uint8_t st[S112_LEN];
    if(!decode_single(t, n, 3450, 1696, 450, 1250, 385, S112_LEN, st)) return;

    static const uint8_t want[5] = {0x23, 0xCB, 0x26, 0x01, 0x00};
    if(memcmp(st, want, 5)) FAILF("112: preamble wrong");

    if(!((st[5] >> 2) & 1)) FAILF("112: power bit not set");

    static const uint8_t MODE_WIRE[MitsubishiModeCount] =
        {0b111, 0b011, 0b111, 0b010, 0b001, 0b111};
    if((st[6] & 0x07) != MODE_WIRE[mode]) FAILF("112: mode %d encoded as %u", mode, st[6] & 7);

    // The field stores 31 minus the setpoint, not the setpoint minus 16.
    uint8_t want_temp = temp;
    if(want_temp < 16) want_temp = 16;
    if(want_temp > 31) want_temp = 31;
    if((st[7] & 0x0F) != (uint8_t)(31 - want_temp))
        FAILF("112: temp %u encoded as %u, wanted %u", temp, st[7] & 0x0F, 31 - want_temp);

    if(st[S112_LEN - 1] != ref_sum(st, S112_LEN))
        FAILF("112: checksum %02X, wanted %02X", st[S112_LEN - 1], ref_sum(st, S112_LEN));
}

static void check_136(MitsubishiMode mode, MitsubishiFan fan, uint8_t temp) {
    MitsubishiRequest req = {mode, fan, temp, 0, MitsubishiModel136};
    uint32_t t[MITSUBISHI_IR_MAX_TIMINGS];
    size_t n = 0;
    if(!mitsubishi_ir_encode_state(&req, t, &n)) {
        FAILF("136: encode failed for mode %d temp %u", mode, temp);
        return;
    }
    uint8_t st[S136_LEN];
    if(!decode_single(t, n, 3324, 1474, 467, 1137, 351, S136_LEN, st)) return;

    static const uint8_t want[4] = {0x23, 0xCB, 0x26, 0x21};
    if(memcmp(st, want, 4)) FAILF("136: preamble wrong");

    if(!((st[5] >> 6) & 1)) FAILF("136: power bit not set");

    static const uint8_t MODE_WIRE[MitsubishiModeCount] =
        {0b011, 0b001, 0b011, 0b101, 0b010, 0b000};
    if((st[6] & 0x07) != MODE_WIRE[mode]) FAILF("136: mode %d encoded as %u", mode, st[6] & 7);

    uint8_t want_temp = temp;
    if(want_temp < 17) want_temp = 17;
    if(want_temp > 30) want_temp = 30;
    if((st[6] >> 4) != (uint8_t)(want_temp - 16))
        FAILF("136: temp %u encoded as %u", temp, (st[6] >> 4) + 16);

    // Bytes 11..16 are the complements of bytes 5..10; there is no checksum.
    for(int i = 0; i < 6; i++) {
        if(st[11 + i] != (uint8_t)~st[5 + i])
            FAILF("136: byte %d is not the complement of byte %d", 11 + i, 5 + i);
    }
}

static void test_variants(void) {
    printf("the 112-bit and 136-bit formats\n");

    for(int m = MitsubishiModeCool; m < MitsubishiModeCount; m++) {
        for(int f = 0; f < MitsubishiFanCount; f++) {
            for(uint8_t temp = 16; temp <= 31; temp++) {
                check_112((MitsubishiMode)m, (MitsubishiFan)f, temp);
                check_136((MitsubishiMode)m, (MitsubishiFan)f, temp);
            }
        }
    }

    // Power off must clear the power bit on every format.
    for(uint8_t model = 0; model < MitsubishiModelCount; model++) {
        MitsubishiRequest req = {MitsubishiModeCool, MitsubishiFanLow, 24, 0, model};
        uint32_t t[MITSUBISHI_IR_MAX_TIMINGS];
        size_t n = 0;
        if(!mitsubishi_ir_encode_toggle(&req, MitsubishiTogglePowerOff, t, &n)) {
            FAILF("model %u: power-off encode failed", model);
            continue;
        }
        if(model == MitsubishiModel112) {
            uint8_t st[S112_LEN];
            if(decode_single(t, n, 3450, 1696, 450, 1250, 385, S112_LEN, st) &&
               ((st[5] >> 2) & 1))
                FAILF("112: power bit still set");
        } else if(model == MitsubishiModel136) {
            uint8_t st[S136_LEN];
            if(decode_single(t, n, 3324, 1474, 467, 1137, 351, S136_LEN, st) &&
               ((st[5] >> 6) & 1))
                FAILF("136: power bit still set");
        }
    }

    // Every format must produce a different frame, or the picker does nothing.
    size_t lens[MitsubishiModelCount];
    for(uint8_t model = 0; model < MitsubishiModelCount; model++) {
        MitsubishiRequest req = {MitsubishiModeCool, MitsubishiFanLow, 24, 0, model};
        uint32_t t[MITSUBISHI_IR_MAX_TIMINGS];
        lens[model] = 0;
        if(!mitsubishi_ir_encode_state(&req, t, &lens[model]))
            FAILF("model %u: encode failed", model);
    }
    for(uint8_t a = 0; a < MitsubishiModelCount; a++) {
        for(uint8_t b = (uint8_t)(a + 1); b < MitsubishiModelCount; b++) {
            if(lens[a] == lens[b])
                FAILF("models %u and %u produce the same length %zu", a, b, lens[a]);
        }
    }

    // An out-of-range model must be refused, not silently treated as 144.
    MitsubishiRequest bad = {MitsubishiModeCool, MitsubishiFanLow, 24, 0, MitsubishiModelCount};
    uint32_t t[MITSUBISHI_IR_MAX_TIMINGS];
    size_t n = 0;
    if(mitsubishi_ir_encode_state(&bad, t, &n)) FAILF("an unknown model was accepted");

    if(mitsubishi_ir_get_option_count() != MitsubishiModelCount)
        FAILF("the Setup picker does not offer every model");
}

int main(void) {
    printf("mitsubishi (full) protocol\n");

    for(int m = MitsubishiModeCool; m < MitsubishiModeCount; m++) {
        for(int f = 0; f < MitsubishiFanCount; f++) {
            for(uint8_t temp = 16; temp <= 31; temp++) {
                check_state((MitsubishiMode)m, (MitsubishiFan)f, temp, 0);
            }
        }
    }

    // Each toggle on its own, then all of them together.
    for(int t = MitsubishiToggleSwingV; t < MitsubishiToggleCount; t++) {
        check_state(MitsubishiModeCool, MitsubishiFanLow, 24, 1u << t);
    }
    check_state(MitsubishiModeHeat, MitsubishiFanHigh, 28, 0x7E);

    test_power_off();
    test_extras();
    test_variants();

    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ok", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
