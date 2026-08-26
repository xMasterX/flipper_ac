// Round-trip test: feed the detector the exact timings our fifteen verified
// remote apps emit and check it names them correctly. Those encoders were
// checked against real air conditioners while the apps were built, so this
// pins the detector to hardware-confirmed waveforms rather than to a second
// transcription of the same reference.

#include "ac_decode.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ballu_ir_protocol.h"
#include "kelon_ir_protocol.h"
#include "kelvinator_ir_protocol.h"
#include "mitsubishi_ir_protocol.h"
#include "samsung_ir_protocol.h"
#include "carrier_ir_protocol.h"
#include "coolix_ir_protocol.h"
#include "daikin_ir_protocol.h"
#include "delonghi_ir_protocol.h"
#include "fujitsu_ir_protocol.h"
#include "gree_ir_protocol.h"
#include "haier_ir_protocol.h"
#include "lg_ir_protocol.h"
#include "midea_ir_protocol.h"
#include "mitsubishi_heavy_ir_protocol.h"
#include "neoclima_ir_protocol.h"
#include "panasonic_ir_protocol.h"
#include "tcl_ir_protocol.h"
#include "toshiba_ir_protocol.h"

static int fails;

static void fail(const char* fmt, ...) {
    (void)fmt;
    fails++;
}

#define FAILF(...)             \
    do {                       \
        printf("  FAIL: ");    \
        printf(__VA_ARGS__);   \
        printf("\n");          \
        fails++;               \
    } while(0)

// ---------------------------------------------------------------------------
// A receiver does not hand back the numbers the transmitter used. Marks come
// back long and spaces short by roughly the same amount, and everything
// carries a little jitter. Every case is run through this first.
// ---------------------------------------------------------------------------
static uint32_t rnd_state = 12345;

static uint32_t rnd(uint32_t n) {
    rnd_state = rnd_state * 1103515245u + 12345u;
    return (rnd_state >> 16) % n;
}

static void distort(const uint32_t* in, size_t n, uint32_t* out, int excess, int jitter_pct) {
    for(size_t i = 0; i < n; i++) {
        int32_t v = (int32_t)in[i];
        v += (i % 2 == 0) ? excess : -excess; // marks stretch, spaces shrink
        if(jitter_pct) {
            int32_t span = v * jitter_pct / 100;
            if(span > 0) v += (int32_t)rnd((uint32_t)(2 * span + 1)) - span;
        }
        if(v < 1) v = 1;
        out[i] = (uint32_t)v;
    }
}

static const char* kind_name(AcResultKind k) {
    switch(k) {
    case AcResultNoise:
        return "Noise";
    case AcResultUnknown:
        return "Unknown";
    default:
        return "Match";
    }
}

/// Run one waveform clean, then distorted three ways, and require the same
/// verdict from all four.
static void expect_match(
    const char* label,
    const uint32_t* t,
    size_t n,
    const char* want_name,
    const char* want_variant) {
    struct {
        const char* how;
        int excess;
        int jitter;
    } passes[] = {
        {"clean", 0, 0},
        {"excess 60", 60, 0},
        {"excess 60 + 6% jitter", 60, 6},
        {"excess -40 + 8% jitter", -40, 8},
    };

    uint32_t buf[AC_MAX_TIMINGS];
    for(size_t p = 0; p < sizeof(passes) / sizeof(passes[0]); p++) {
        distort(t, n, buf, passes[p].excess, passes[p].jitter);

        AcDetection d;
        bool fresh = ac_decode(buf, n, &d);

        if(!fresh || d.kind != AcResultMatch) {
            FAILF("%s (%s): got %s, wanted %s", label, passes[p].how, kind_name(d.kind), want_name);
            continue;
        }
        if(strcmp(d.entry->name, want_name)) {
            FAILF(
                "%s (%s): matched %s/%s, wanted %s/%s",
                label,
                passes[p].how,
                d.entry->name,
                d.entry->variant,
                want_name,
                want_variant);
            continue;
        }
        if(want_variant && strcmp(d.entry->variant, want_variant)) {
            FAILF(
                "%s (%s): variant %s, wanted %s",
                label,
                passes[p].how,
                d.entry->variant,
                want_variant);
        }
    }
}

static void expect_kind(const char* label, const uint32_t* t, size_t n, AcResultKind want) {
    AcDetection d;
    ac_decode(t, n, &d);
    if(d.kind != want) {
        const char* extra = (d.kind == AcResultMatch) ? d.entry->name : "";
        FAILF("%s: got %s %s, wanted %s", label, kind_name(d.kind), extra, kind_name(want));
    }
}

// ---------------------------------------------------------------------------
// The fifteen protocols we ship an app for.
// ---------------------------------------------------------------------------

#define REQ_CASE(slug, Slug)                                                 \
    static size_t gen_##slug(uint32_t* t) {                                  \
        Slug##Request req;                                                   \
        memset(&req, 0, sizeof(req));                                        \
        req.mode = 1;                                                        \
        req.fan = 1;                                                         \
        req.temp = 24;                                                       \
        req.toggle_bits = 0;                                                 \
        req.option = 0;                                                      \
        size_t n = 0;                                                        \
        if(!slug##_ir_encode_state(&req, t, &n)) {                           \
            FAILF(#slug ": encoder refused a plain cool/24 request");        \
            return 0;                                                        \
        }                                                                    \
        return n;                                                            \
    }

REQ_CASE(ballu, Ballu)
REQ_CASE(kelon, Kelon)
REQ_CASE(kelvinator, Kelvinator)
REQ_CASE(mitsubishi, Mitsubishi)
REQ_CASE(samsung, Samsung)
REQ_CASE(carrier, Carrier)
REQ_CASE(daikin, Daikin)
REQ_CASE(delonghi, Delonghi)
REQ_CASE(fujitsu, Fujitsu)
REQ_CASE(gree, Gree)
REQ_CASE(haier, Haier)
REQ_CASE(lg, Lg)
REQ_CASE(midea, Midea)
REQ_CASE(mitsubishi_heavy, MitsubishiHeavy)
REQ_CASE(neoclima, Neoclima)
REQ_CASE(panasonic, Panasonic)
REQ_CASE(tcl, Tcl)
REQ_CASE(toshiba, Toshiba)

/// The Mitsubishi app builds three different frame formats, chosen in Setup.
/// Each has to come back as itself.
static size_t gen_mitsubishi_model(uint32_t* t, uint8_t model) {
    MitsubishiRequest req;
    memset(&req, 0, sizeof(req));
    req.mode = 1;
    req.fan = 1;
    req.temp = 24;
    req.option = model;
    size_t n = 0;
    if(!mitsubishi_ir_encode_state(&req, t, &n)) {
        FAILF("mitsubishi model %u: encoder refused a plain cool/24 request", model);
        return 0;
    }
    return n;
}

static size_t gen_mitsubishi112(uint32_t* t) {
    return gen_mitsubishi_model(t, MitsubishiModel112);
}

static size_t gen_mitsubishi136(uint32_t* t) {
    return gen_mitsubishi_model(t, MitsubishiModel136);
}

/// The Daikin app builds eight frame formats, chosen in Setup. Each has to
/// come back as itself.
static size_t gen_daikin_model(uint32_t* t, uint8_t model) {
    DaikinRequest req;
    memset(&req, 0, sizeof(req));
    req.mode = 1;
    req.fan = 1;
    req.temp = 24;
    req.option = model;
    size_t n = 0;
    if(!daikin_ir_encode_state(&req, t, &n)) {
        FAILF("daikin model %u: encoder refused a plain cool/24 request", model);
        return 0;
    }
    return n;
}

#define DAIKIN_MODEL_GEN(name, model)               \
    static size_t name(uint32_t* t) {               \
        return gen_daikin_model(t, model);          \
    }

DAIKIN_MODEL_GEN(gen_daikin477, DaikinModelArc477)
DAIKIN_MODEL_GEN(gen_daikin216, DaikinModel216)
DAIKIN_MODEL_GEN(gen_daikin160, DaikinModel160)
DAIKIN_MODEL_GEN(gen_daikin176, DaikinModel176)
DAIKIN_MODEL_GEN(gen_daikin152, DaikinModel152)
DAIKIN_MODEL_GEN(gen_daikin128, DaikinModel128)
DAIKIN_MODEL_GEN(gen_daikin64, DaikinModel64)

/// Apps whose Setup picker chooses between frame formats. Each format has to
/// come back from the detector as itself.
#define MODEL_GEN(fn, Type, encode, model)              \
    static size_t fn(uint32_t* t) {                     \
        Type req;                                       \
        memset(&req, 0, sizeof(req));                   \
        req.mode = 1;                                   \
        req.fan = 1;                                    \
        req.temp = 24;                                  \
        req.option = model;                             \
        size_t n = 0;                                   \
        if(!encode(&req, t, &n)) {                      \
            FAILF(#fn ": encoder refused cool/24");     \
            return 0;                                   \
        }                                               \
        return n;                                       \
    }

MODEL_GEN(gen_haier_hsu, HaierRequest, haier_ir_encode_state, HaierModelHsu07)
MODEL_GEN(gen_haier160, HaierRequest, haier_ir_encode_state, HaierModel160)
MODEL_GEN(gen_haier176, HaierRequest, haier_ir_encode_state, HaierModel176)
MODEL_GEN(gen_mhi_zjs, MitsubishiHeavyRequest, mitsubishi_heavy_ir_encode_state,
          MitsubishiHeavyModelZjs)
MODEL_GEN(gen_kelon168, KelonRequest, kelon_ir_encode_state, KelonModel168)

static size_t gen_coolix(uint32_t* t) {
    size_t n = 0;
    if(!coolix_ir_encode_state(CoolixModeCool, CoolixFanAuto, 24, t, &n)) {
        FAILF("coolix: encoder refused a plain cool/24 request");
        return 0;
    }
    return n;
}

typedef size_t (*GenFn)(uint32_t*);

typedef struct {
    const char* label;
    GenFn gen;
    const char* name;
    const char* variant;
} Case;

static const Case cases[] = {
    {"ballu", gen_ballu, "Electra", "YKR series"},
    {"carrier", gen_carrier, "Carrier", "64-bit"},
    {"coolix", gen_coolix, "Coolix", "24-bit"},
    {"daikin", gen_daikin, "Daikin", "ARC433 / ARC466"},
    {"daikin ARC477", gen_daikin477, "Daikin2", "ARC477A1"},
    {"daikin ARC484", gen_daikin216, "Daikin216", "ARC484A4 / ARC433B69"},
    {"daikin ARC423", gen_daikin160, "Daikin160", "ARC423A5"},
    {"daikin BRC4C15", gen_daikin176, "Daikin176", "BRC4C151 / BRC4C153"},
    {"daikin ARC480", gen_daikin152, "Daikin152", "ARC480A5"},
    {"daikin BRC52B", gen_daikin128, "Daikin128", "BRC52B63 / 17 series"},
    {"daikin DGS01", gen_daikin64, "Daikin64", "DGS01"},
    {"delonghi", gen_delonghi, "Delonghi", "PAC"},
    {"fujitsu", gen_fujitsu, "Fujitsu", "ARRAH2E / ARREB1E"},
    {"gree", gen_gree, "Gree", "YAW1F/YBOFB/YX1FSF"},
    {"haier", gen_haier, "Haier", "YR-W02"},
    {"haier HSU07", gen_haier_hsu, "Haier", "HSU07-HEA03"},
    {"haier AC160", gen_haier160, "Haier", "AC160"},
    {"haier AC176", gen_haier176, "Haier", "AC176"},
    {"kelon", gen_kelon, "Kelon", "RCH-R0Y3 / ON-OFF"},
    // The 168-bit Kelon frame is the Whirlpool protocol; one row covers both.
    {"kelon DG11R2", gen_kelon168, "Whirlpool", "= Kelon DG11R2-01"},
    {"kelvinator", gen_kelvinator, "Kelvinator", "KSV / YAP0F8 / YB1FA"},
    {"lg", gen_lg, "LG", "28-bit"},
    {"midea", gen_midea, "Midea", "48-bit"},
    {"mitsubishi", gen_mitsubishi, "Mitsubishi", "MSZ / MSH / MLZ"},
    {"mitsubishi112", gen_mitsubishi112, "Mitsubishi112", "112-bit"},
    {"mitsubishi136", gen_mitsubishi136, "Mitsubishi136", "136-bit"},
    {"mitsubishi_heavy", gen_mitsubishi_heavy, "MitsubishiHeavy", "ZM-S (152)"},
    {"mitsubishi_heavy ZJ-S", gen_mhi_zjs, "MitsubishiHeavy", "ZJ-S (88)"},
    {"neoclima", gen_neoclima, "Neoclima", "ZH/TY-01"},
    {"panasonic", gen_panasonic, "Panasonic", "DKE/JKE/NKE/LKE"},
    {"samsung", gen_samsung, "Samsung", "AR series"},
    {"tcl", gen_tcl, "TCL", "112-bit"},
    {"toshiba", gen_toshiba, "Toshiba", "9 byte"},
};

static void test_round_trip(void) {
    printf("round trip against the shipped encoders\n");
    uint32_t t[AC_MAX_TIMINGS];
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t n = cases[i].gen(t);
        if(!n) continue;
        expect_match(cases[i].label, t, n, cases[i].name, cases[i].variant);
    }
}

// ---------------------------------------------------------------------------
// Coolix and Midea share a line code and a bit count. Only the payload tells
// them apart, so check they do not steal each other's signals.
// ---------------------------------------------------------------------------
static void test_coolix_midea_split(void) {
    printf("coolix and midea stay apart\n");
    uint32_t t[AC_MAX_TIMINGS];

    size_t n = gen_coolix(t);
    if(n) expect_match("coolix payload", t, n, "Coolix", "24-bit");

    n = gen_midea(t);
    if(n) expect_match("midea payload", t, n, "Midea", "48-bit");
}

// ---------------------------------------------------------------------------
// A capture usually holds the frame more than once - the receiver only gives
// up after 150 ms of silence.
// ---------------------------------------------------------------------------
static void test_repeats(void) {
    printf("repeated frames collapse to one\n");

    uint32_t one[AC_MAX_TIMINGS];
    uint32_t two[AC_MAX_TIMINGS];

    size_t n = gen_gree(one);
    if(!n) return;

    // Two copies with a 40 ms gap, the way a remote sends them.
    memcpy(two, one, n * sizeof(uint32_t));
    two[n] = 40000;
    memcpy(two + n + 1, one, n * sizeof(uint32_t));
    size_t n2 = n + 1 + n;
    if(n2 > AC_MAX_TIMINGS) return;

    AcDetection d;
    if(!ac_decode(two, n2, &d) || d.kind != AcResultMatch) {
        FAILF("repeat: not matched at all");
        return;
    }
    if(strcmp(d.entry->name, "Gree")) FAILF("repeat: matched %s", d.entry->name);
    if(d.repeats != 2) FAILF("repeat: counted %u copies, wanted 2", d.repeats);

    AcDetection single;
    ac_decode(one, n, &single);
    if(d.data_len != single.data_len || memcmp(d.data, single.data, d.data_len)) {
        FAILF("repeat: payload differs from the single-frame payload");
    }
}

// ---------------------------------------------------------------------------
// Noise rejection. Anything here must leave the display alone.
// ---------------------------------------------------------------------------
static void test_noise(void) {
    printf("noise is ignored\n");
    uint32_t t[AC_MAX_TIMINGS];

    // A compact fluorescent lamp: bursts at roughly the carrier frequency
    // with no structure to them.
    for(size_t i = 0; i < 400; i++) {
        t[i] = 60 + rnd(900);
    }
    expect_kind("lamp hash", t, 400, AcResultNoise);

    // A switching supply: a plausible header, then edges that agree with
    // nothing.
    t[0] = 4000;
    t[1] = 4000;
    for(size_t i = 2; i < 300; i++) {
        t[i] = 100 + rnd(2500);
    }
    expect_kind("header then hash", t, 300, AcResultNoise);

    // Too short to be an air conditioner frame.
    for(size_t i = 0; i < 20; i++) {
        t[i] = (i == 0) ? 9000 : (i == 1 ? 4500 : (i % 2 ? 560 : 1690));
    }
    expect_kind("short burst", t, 20, AcResultNoise);

    // A steady square wave. Consistent, but no header.
    for(size_t i = 0; i < 200; i++) {
        t[i] = 500;
    }
    expect_kind("square wave", t, 200, AcResultNoise);

    // Marks that drift steadily instead of holding one width.
    t[0] = 9000;
    t[1] = 4500;
    for(size_t i = 2; i < 260; i += 2) {
        t[i] = 200 + (uint32_t)i * 4;
        t[i + 1] = 600;
    }
    expect_kind("drifting marks", t, 260, AcResultNoise);
}

// ---------------------------------------------------------------------------
// A well-formed frame from a protocol we do not carry should say Unknown, not
// be silently dropped and not be forced onto the nearest entry.
// ---------------------------------------------------------------------------
static void test_unknown(void) {
    printf("an unlisted but well-formed frame reads as Unknown\n");

    // Deliberately unlike anything in the database: a 2100/2100 header and
    // 900/2700 bit spaces.
    uint32_t t[AC_MAX_TIMINGS];
    size_t n = 0;
    t[n++] = 2100;
    t[n++] = 2100;
    for(int i = 0; i < 64; i++) {
        t[n++] = 300;
        t[n++] = (i % 3) ? 900 : 2700;
    }
    t[n++] = 300;

    AcDetection d;
    if(!ac_decode(t, n, &d)) {
        FAILF("unknown: rejected as noise");
        return;
    }
    if(d.kind != AcResultUnknown) {
        FAILF("unknown: got %s %s", kind_name(d.kind), d.kind == AcResultMatch ? d.entry->name : "");
        return;
    }
    if(d.bits != 64) FAILF("unknown: counted %u bits, wanted 64", d.bits);
}

// ---------------------------------------------------------------------------
// The database itself.
// ---------------------------------------------------------------------------
static void test_db_sanity(void) {
    printf("database sanity\n");

    for(size_t i = 0; i < ac_proto_db_count; i++) {
        const AcProtoEntry* e = &ac_proto_db[i];
        if(!e->name || !e->name[0]) FAILF("entry %zu has no name", i);
        if(!e->brands || !e->brands[0]) FAILF("%s has no brands", e->name);
        if(!e->app || !e->app[0]) FAILF("%s has no app field", e->name);
        if(e->hdr_mark == 0 || e->hdr_space == 0) FAILF("%s has no header", e->name);
        if(e->bit_mark == 0) FAILF("%s has no bit mark", e->name);
        if(e->one_space == 0 || e->zero_space == 0) FAILF("%s has no bit spaces", e->name);
        if(e->one_space == e->zero_space) FAILF("%s cannot tell 1 from 0", e->name);
        if(e->bits > 512) FAILF("%s wants %u bits, more than a capture holds", e->name, e->bits);
        if(e->sig_kind == AcSigPrefix && e->sig_len == 0) FAILF("%s has an empty prefix", e->name);
        for(uint8_t k = 0; k < e->sig_len; k++) {
            if(e->sig[k] & ~e->sig_mask[k]) FAILF("%s signature byte %u is outside its mask", e->name, k);
        }

        // Every one-space must be under the section-gap threshold, or the
        // parser would read a data bit as the end of a section.
        if(e->one_space >= AC_SECTION_GAP_US)
            FAILF("%s one-space %u reaches the section gap", e->name, e->one_space);
        if(e->zero_space >= AC_SECTION_GAP_US)
            FAILF("%s zero-space %u reaches the section gap", e->name, e->zero_space);
    }
}

// ---------------------------------------------------------------------------
// Build a frame from an entry's own numbers and check the detector hands it
// back the same protocol. This sweeps rows we have no encoder for, and it is
// how genuinely ambiguous pairs surface: if two rows cannot be told apart,
// one of them loses here.
// ---------------------------------------------------------------------------

static void fill_payload(const AcProtoEntry* e, uint8_t* buf, size_t len) {
    memset(buf, 0, len);
    switch(e->sig_kind) {
    case AcSigCoolix:
        for(size_t k = 0; k + 1 < len; k += 2) {
            buf[k] = (uint8_t)(0xB2 + k);
            buf[k + 1] = (uint8_t)~buf[k];
        }
        break;
    case AcSigMidea:
        for(size_t k = 0; k < 6 && k < len; k++) {
            buf[k] = (uint8_t)(k ? 0x1F + k : 0xA1);
            if(k + 6 < len) buf[k + 6] = (uint8_t)~buf[k];
        }
        break;
    case AcSigPrefix:
        for(uint8_t k = 0; k < e->sig_len && k < len; k++) {
            buf[k] = e->sig[k];
        }
        break;
    default:
        for(size_t k = 0; k < len; k++) {
            buf[k] = (uint8_t)(0x5A ^ (k * 7));
        }
        break;
    }
}

static size_t synth(const AcProtoEntry* e, uint32_t* t) {
    uint8_t payload[AC_MAX_BYTES];
    fill_payload(e, payload, sizeof(payload));

    size_t n = 0;
    if(e->lead_mark) {
        t[n++] = e->lead_mark;
        t[n++] = e->lead_space;
    }
    t[n++] = e->hdr_mark;
    t[n++] = e->hdr_space;

    for(uint16_t b = 0; b < e->bits; b++) {
        uint8_t byte = payload[b >> 3];
        uint8_t off = (uint8_t)(b & 7u);
        bool bit = e->msb_first ? ((byte >> (7 - off)) & 1) : ((byte >> off) & 1);
        if(n + 3 >= AC_MAX_TIMINGS) return 0;
        t[n++] = e->bit_mark;
        t[n++] = bit ? e->one_space : e->zero_space;
    }
    t[n++] = e->bit_mark;
    return n;
}

static void test_self_identification(void) {
    printf("every database row identifies as itself\n");

    uint32_t t[AC_MAX_TIMINGS];
    uint32_t buf[AC_MAX_TIMINGS];

    for(size_t i = 0; i < ac_proto_db_count; i++) {
        const AcProtoEntry* e = &ac_proto_db[i];
        size_t n = synth(e, t);
        if(!n) continue; // too long for one capture, nothing to check

        // Clean, then with a realistic receiver bias on top.
        for(int pass = 0; pass < 2; pass++) {
            distort(t, n, buf, pass ? 70 : 0, pass ? 5 : 0);

            AcDetection d;
            if(!ac_decode(buf, n, &d) || d.kind != AcResultMatch) {
                FAILF("self %s/%s: got %s", e->name, e->variant, kind_name(d.kind));
                continue;
            }
            // A different variant of the same protocol is still the right
            // answer for a user picking an app; a different protocol is not.
            if(strcmp(d.entry->name, e->name)) {
                FAILF(
                    "self %s/%s (%u bits): read as %s/%s",
                    e->name,
                    e->variant,
                    e->bits,
                    d.entry->name,
                    d.entry->variant);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The worker stops at 1024 timings, so a long frame can arrive cut in half. A
// truncated capture must never be reported as a confident match.
// ---------------------------------------------------------------------------
static void test_truncation(void) {
    printf("truncated captures do not become false matches\n");

    uint32_t t[AC_MAX_TIMINGS];
    size_t n = gen_daikin(t);
    if(!n) return;

    for(size_t cut = n / 3; cut < n; cut += n / 7) {
        AcDetection d;
        ac_decode(t, cut, &d);
        if(d.kind == AcResultMatch && strcmp(d.entry->name, "Daikin")) {
            FAILF("truncated daikin at %zu of %zu read as %s", cut, n, d.entry->name);
        }
    }
}

int main(void) {
    printf("ac_detector: %u protocols in the database\n\n", (unsigned)ac_proto_db_count);

    test_db_sanity();
    test_round_trip();
    test_coolix_midea_split();
    test_repeats();
    test_noise();
    test_unknown();
    test_self_identification();
    test_truncation();

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ok", fails, fails == 1 ? "" : "s");
    (void)fail;
    return fails ? 1 : 0;
}
