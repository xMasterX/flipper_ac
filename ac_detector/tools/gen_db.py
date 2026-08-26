#!/usr/bin/env python3
"""Generate ac_protocol_db.c from the curated table below.

Timings come from two places, in this order of trust:
  1. The 15 protocol modules in this workspace, which were verified against
     real hardware while building the per-protocol remote apps.
  2. IRremoteESP8266's ir_*.cpp constants, for protocols we have no app for.

Brand lists come from IRremoteESP8266's SupportedProtocols.md (devices its
authors actually tested) followed by widely reported OEM rebrands.  The
rebrand tail is community knowledge, not measured, and is flagged as such in
the README.

Run:  python3 tools/gen_db.py > ac_protocol_db.c
"""

# name, variant, lead(mark,space), hdr(mark,space), bit_mark, one, zero,
# bits-on-the-wire, msb_first, signature, app
#
# `bits` is the number of data bits actually on the wire, including any
# constant filler bits a protocol inserts between sections - not the size of
# the decoded state array.  Gree, for example, is 8 state bytes but 67 bits.
P = []
def add(name, variant, hdr, bit, bits, msb=0, sig=None, app="-", lead=(0, 0),
        brands=""):
    P.append(dict(name=name, variant=variant, lead=lead, hdr=hdr, bit=bit,
                  bits=bits, msb=msb, sig=sig, app=app, brands=brands))

COOLIX_BRANDS = ("Beko, Midea, Airwell, Bosch, Comfee, Dimplex, Electrolux, "
                 "Ferroli, Galanz, Inventor, Kelon, Keystone, Klimaire, "
                 "Lennox, MrCool, Pioneer, Qlima, Rowa, Sinclair, Tornado, "
                 "Tosot, Vivax, Zephir")
MIDEA_BRANDS = ("Midea, Comfee, Danby, Kaysun, Keystone, Lennox, MrCool, "
                "Pioneer, Trotec, Carrier, Senville, Klimaire, Inventor, "
                "Qlima, Ferroli, Electrolux")
GREE_BRANDS = ("Gree, Green, Amana, Cooper & Hunter, EKOKAI, RusClimate, "
               "Soleus Air, Ultimate, Vaillant, Tosot, Sinclair, Rovex, "
               "Tadiran, Lessar, Dahatsu, Shivaki")
ELECTRA_BRANDS = ("Electra, AEG, AUX, Ballu, Centek, Electrolux, Frigidaire, "
                  "Subtropic, Tadiran, Zanussi, Airwell")
TCL_BRANDS = "TCL, Daewoo, Electrolux, Leberg, Teknopoint, Rowa, Ariston"
HAIER_BRANDS = "Haier, Daichi, Mabe, Candy"
FUJITSU_BRANDS = "Fujitsu, Fujitsu General, OGeneral, Friedrich"
TOSHIBA_BRANDS = "Toshiba, Carrier, Alaska"
DAIKIN_BRANDS = "Daikin, McQuay"
KELV_BRANDS = "Kelvinator, Gree (YAP0F8), Sharp, Green"
LG_BRANDS = "LG, General Electric, Goldstar"
HITACHI_BRANDS = "Hitachi"
CARRIER_BRANDS = "Carrier, Surrey, Toshiba-Carrier"
SANYO_BRANDS = "Sanyo"
SAMSUNG_BRANDS = "Samsung"
MITSU_BRANDS = "Mitsubishi, Mitsubishi Electric"
MHI_BRANDS = "Mitsubishi Heavy Industries"
NEOCLIMA_BRANDS = "Neoclima, Soleus Air, Zanussi"
WHIRLPOOL_BRANDS = "Whirlpool"
ARGO_BRANDS = "Argo"
SHARP_BRANDS = "Sharp"
KELON_BRANDS = "Hisense, Kelon, AUX"
VESTEL_BRANDS = "Vestel"
BOSCH_BRANDS = "Bosch, Durastar"
MIRAGE_BRANDS = "Mirage, Maxell, Tronitechnik"

# ---------------------------------------------------------------- our apps
# Coolix and Midea share a line code to within measurement error.  Only the
# payload separates them, so both carry a content check.
add("Coolix", "24-bit", (4692, 4416), (552, 1656, 552), 48, msb=1,
    sig="COOLIX", app="Coolix AC Remote", brands=COOLIX_BRANDS)
add("Midea", "48-bit", (4480, 4480), (560, 1680, 560), 96, msb=1,
    sig="MIDEA", app="Midea AC Remote", brands=MIDEA_BRANDS)
add("Gree", "YAW1F/YBOFB/YX1FSF", (9000, 4500), (620, 1600, 540), 67,
    app="Gree AC Remote", brands=GREE_BRANDS)
add("Kelvinator", "KSV / YAP0F8 / YB1FA", (9010, 4505), (680, 1530, 510), 134,
    app="Kelvinator AC Remote", brands=KELV_BRANDS)
add("LG", "28-bit", (8500, 4250), (550, 1600, 550), 28, msb=1,
    app="LG AC Remote", brands=LG_BRANDS)
add("LG2", "32-bit", (3200, 9900), (550, 1600, 550), 32, msb=1,
    app="-", brands=LG_BRANDS)
add("Daikin", "ARC433 / ARC466", (3650, 1623), (428, 1280, 428), 285,
    sig=("FF:11", "FF:DA"), app="Daikin AC Remote", brands=DAIKIN_BRANDS)
add("Fujitsu", "ARRAH2E / ARREB1E", (3324, 1574), (448, 1182, 390), 128,
    sig=("FF:14", "FF:63"), app="Fujitsu AC Remote", brands=FUJITSU_BRANDS)
add("Fujitsu", "short code (off/eco)", (3324, 1574), (448, 1182, 390), 56,
    sig=("FF:14", "FF:63"), app="Fujitsu AC Remote", brands=FUJITSU_BRANDS)
add("Toshiba", "9 byte", (4400, 4300), (580, 1600, 490), 72, msb=1,
    sig=("FF:F2", "FF:0D"), app="Toshiba AC Remote", brands=TOSHIBA_BRANDS)
add("Toshiba", "10 byte", (4400, 4300), (580, 1600, 490), 80, msb=1,
    sig=("FF:F2", "FF:0D"), app="Toshiba AC Remote", brands=TOSHIBA_BRANDS)
add("Toshiba", "12 byte", (4400, 4300), (580, 1600, 490), 96, msb=1,
    sig=("FF:F2", "FF:0D"), app="Toshiba AC Remote", brands=TOSHIBA_BRANDS)
add("Panasonic", "DKE/JKE/NKE/LKE", (3456, 1728), (432, 1296, 432), 216,
    sig=("FF:02", "FF:20", "FF:E0", "FF:04"), app="Panasonic AC Remote",
    brands="Panasonic")
add("Haier", "YR-W02", (3000, 4300), (520, 1650, 650), 112, msb=1,
    lead=(3000, 3000), sig=("FF:A6",), app="Haier AC Remote",
    brands=HAIER_BRANDS)
add("Haier", "HSU07-HEA03", (3000, 4300), (520, 1650, 650), 72, msb=1,
    lead=(3000, 3000), sig=("FF:A5",), app="Haier AC Remote",
    brands=HAIER_BRANDS)
add("Haier", "AC160", (3000, 4300), (520, 1650, 650), 160, msb=1,
    lead=(3000, 3000), sig=("FF:A6",), app="Haier AC Remote",
    brands=HAIER_BRANDS)
add("Haier", "AC176", (3000, 4300), (520, 1650, 650), 176, msb=1,
    lead=(3000, 3000), sig=("FF:A6",), app="Haier AC Remote",
    brands=HAIER_BRANDS)
add("MitsubishiHeavy", "ZM-S (152)", (3140, 1630), (370, 420, 1220), 152,
    sig=("FF:AD", "FF:51", "FF:3C"), app="MitsuHeavy AC Remote",
    brands=MHI_BRANDS)
add("MitsubishiHeavy", "ZJ-S (88)", (3140, 1630), (370, 420, 1220), 88,
    sig=("FF:AD", "FF:51", "FF:3C"), app="MitsuHeavy AC Remote",
    brands=MHI_BRANDS)
add("Neoclima", "ZH/TY-01", (6112, 7391), (537, 1651, 571), 96,
    app="Neoclima AC Remote", brands=NEOCLIMA_BRANDS)
add("Carrier", "64-bit", (8940, 4556), (503, 1736, 615), 64, msb=1,
    app="Carrier AC Remote", brands=CARRIER_BRANDS)
add("Carrier", "40-bit", (8402, 4166), (547, 1540, 497), 40, msb=1,
    app="-", brands=CARRIER_BRANDS)
add("Carrier", "128-bit", (4600, 2600), (340, 1000, 400), 128,
    app="-", brands=CARRIER_BRANDS)
add("Delonghi", "PAC", (8984, 4200), (572, 1558, 510), 64,
    app="Delonghi AC Remote", brands="De'Longhi")
add("TCL", "112-bit", (3000, 1650), (500, 1050, 325), 112,
    sig=("FF:23", "FF:CB", "FF:26"), app="TCL AC Remote", brands=TCL_BRANDS)
add("Electra", "YKR series", (9080, 4485), (610, 1660, 548), 104,
    sig=("FF:C3",), app="Ballu AC Remote", brands=ELECTRA_BRANDS)

# ------------------------------------------------- no app of ours (yet)
add("Daikin2", "ARC477A1", (3500, 1728), (460, 1270, 420), 312,
    lead=(10024, 25180), sig=("FF:11", "FF:DA"), app="Daikin AC Remote",
    brands=DAIKIN_BRANDS)
add("Daikin216", "ARC484A4 / ARC433B69", (3440, 1750), (420, 1300, 450), 216,
    sig=("FF:11", "FF:DA"), app="Daikin AC Remote", brands=DAIKIN_BRANDS)
add("Daikin160", "ARC423A5", (5000, 2145), (342, 1786, 700), 160,
    sig=("FF:11", "FF:DA"), app="Daikin AC Remote", brands=DAIKIN_BRANDS)
add("Daikin176", "BRC4C151 / BRC4C153", (5070, 2140), (370, 1780, 710), 176,
    sig=("FF:11", "FF:DA"), app="Daikin AC Remote", brands=DAIKIN_BRANDS)
# 152 payload bits plus the five zero bits ARC480A5 sends before the header.
add("Daikin152", "ARC480A5", (3492, 1718), (433, 1529, 433), 157,
    sig=("FF:11", "FF:DA"), app="Daikin AC Remote", brands=DAIKIN_BRANDS)
add("Daikin128", "BRC52B63 / 17 series", (4600, 2500), (350, 954, 382), 128,
    lead=(9800, 9800), app="Daikin AC Remote", brands=DAIKIN_BRANDS)
add("Daikin64", "DGS01", (4600, 2500), (350, 954, 382), 64,
    lead=(9800, 9800), app="Daikin AC Remote", brands=DAIKIN_BRANDS)
add("Mitsubishi", "MSZ / MSH / MLZ", (3400, 1750), (450, 1300, 420), 144,
    sig=("FF:23", "FF:CB", "FF:26"), app="Mitsubishi (Full) AC Remote",
    brands=MITSU_BRANDS)
add("Mitsubishi112", "112-bit", (3450, 1696), (450, 1250, 385), 112,
    sig=("FF:23", "FF:CB", "FF:26"), app="Mitsubishi (Full) AC Remote",
    brands="Mitsubishi, Sharp")
add("Mitsubishi136", "136-bit", (3324, 1474), (467, 1137, 351), 136,
    sig=("FF:23", "FF:CB", "FF:26"), app="Mitsubishi (Full) AC Remote",
    brands=MITSU_BRANDS)
add("Hitachi", "224-bit", (3300, 1700), (400, 1250, 500), 224,
    sig=("FF:80", "FF:08"), brands=HITACHI_BRANDS)
add("Hitachi1", "104-bit", (3400, 3400), (400, 1250, 500), 104,
    brands=HITACHI_BRANDS)
add("Hitachi", "264-bit", (3416, 1604), (463, 1208, 372), 264,
    sig=("FF:01", "FF:10"), brands=HITACHI_BRANDS)
add("Hitachi", "296-bit", (3416, 1604), (463, 1208, 372), 296,
    sig=("FF:01", "FF:10"), brands=HITACHI_BRANDS)
add("Hitachi", "344-bit", (3416, 1604), (463, 1208, 372), 344,
    sig=("FF:01", "FF:10"), brands=HITACHI_BRANDS)
add("Hitachi", "424-bit (RAR-8P2)", (3416, 1604), (463, 1208, 372), 424,
    sig=("FF:01", "FF:10"), brands=HITACHI_BRANDS)
add("Hitachi3", "216-bit", (3400, 1660), (460, 1250, 410), 216,
    brands=HITACHI_BRANDS)
add("Samsung", "AR series", (3086, 8864), (586, 1432, 436), 112,
    lead=(690, 17844), sig=("FF:02", "FF:92"), app="Samsung (Full) AC Remote",
    brands=SAMSUNG_BRANDS)
add("Samsung", "AR extended", (3086, 8864), (586, 1432, 436), 168,
    lead=(690, 17844), sig=("FF:02", "FF:92"), app="Samsung (Full) AC Remote",
    brands=SAMSUNG_BRANDS)
add("Sanyo", "72-bit", (8500, 4200), (500, 1600, 550), 72, brands=SANYO_BRANDS)
add("Sanyo", "88-bit", (5400, 2000), (500, 1500, 750), 88, brands=SANYO_BRANDS)
add("Sanyo", "152-bit", (3300, 1725), (440, 1290, 405), 152,
    brands=SANYO_BRANDS)
# Whirlpool and Kelon's 168-bit frame are the same protocol - same preamble,
# same checksum bytes, same section split - so they share one row rather than
# taking turns claiming each other's signals.
add("Whirlpool", "= Kelon DG11R2-01", (8950, 4484), (597, 1649, 533), 168,
    sig=("FF:83", "FF:06"), app="Kelon AC Remote",
    brands="Whirlpool, Kelon, Hisense, AUX")
add("Argo", "WREM2", (6400, 3300), (400, 2200, 900), 96, brands=ARGO_BRANDS)
add("Airton", "RD1A1", (6630, 3350), (400, 1260, 430), 56, brands="Airton")
add("Amcor", "TAC-444 / TAC-495", (8200, 4200), (600, 1500, 600), 64,
    brands="Amcor")
add("Corona", "AR-01", (3500, 1680), (450, 1270, 420), 168, brands="Corona")
add("Ecoclim", "HYSFR-P348", (5730, 1935), (440, 1739, 637), 56,
    brands="EcoClim")
add("Kelon", "RCH-R0Y3 / ON-OFF", (9000, 4600), (560, 1680, 600), 48,
    app="Kelon AC Remote", brands=KELON_BRANDS)
add("Mirage", "KKG9AC1 / KKG29AC1", (8360, 4248), (554, 1592, 545), 120,
    brands=MIRAGE_BRANDS)
add("Rhoss", "IDEA", (3042, 4248), (648, 1545, 457), 96, brands="Rhoss")
add("Sharp", "A705 / A903 / A907", (3800, 1900), (470, 1400, 500), 104,
    brands=SHARP_BRANDS)
add("Technibel", "", (8836, 4380), (523, 1696, 564), 56, brands="Technibel")
add("Teco", "", (9000, 4440), (620, 1650, 580), 35, brands="Alaska, Teco")
add("Teknopoint", "", (3600, 1600), (477, 1200, 530), 112,
    brands="Teknopoint")
add("Transcold", "", (5944, 7563), (555, 3556, 1526), 24, brands="Transcold")
add("Trotec", "", (5952, 7364), (592, 1560, 592), 72, brands="Trotec, Duux")
add("Trotec3550", "", (12000, 5130), (550, 1950, 500), 72, brands="Trotec")
add("Vestel", "", (3110, 9066), (520, 1535, 480), 56, brands=VESTEL_BRANDS)
add("York", "", (4887, 2267), (612, 1778, 579), 136, brands="York")
add("Bosch144", "RG10A(G2S)BGEF", (4380, 4400), (502, 1560, 494), 144,
    brands=BOSCH_BRANDS)
add("Goodweather", "ZH/JT-03", (6820, 6820), (730, 1700, 700), 48, msb=1,
    brands="Goodweather")
add("Eurom", "Polar 16CH", (6800, 3400), (500, 1700, 550), 96, brands="Eurom")

# --------------------------------------------------------------- emitter
import sys

def sig_fields(sig):
    """Turn the compact signature spec into (kind, len, bytes[], mask[])."""
    if sig is None:
        return "AcSigNone", 0, [0] * 6, [0] * 6
    if sig == "COOLIX":
        return "AcSigCoolix", 0, [0] * 6, [0] * 6
    if sig == "MIDEA":
        return "AcSigMidea", 0, [0] * 6, [0] * 6
    vals, masks = [], []
    for part in sig:
        mask, val = part.split(":")
        masks.append(int(mask, 16))
        vals.append(int(val, 16))
    assert len(vals) <= 6, sig
    return "AcSigPrefix", len(vals), vals + [0] * (6 - len(vals)), \
        masks + [0] * (6 - len(masks))

def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

def blist(v):
    return "{" + ", ".join("0x%02X" % x for x in v) + "}"

out = sys.stdout
out.write("""// Generated by tools/gen_db.py - do not edit by hand.
//
// Timings for the protocols we ship a remote app for were taken from those
// apps, which were checked against real air conditioners.  The rest come from
// IRremoteESP8266's ir_*.cpp constants.  Brand lists start with the devices
// IRremoteESP8266 documents as tested and continue with OEM rebrands that are
// widely reported but that we have not measured ourselves.

#include "ac_protocol_db.h"

const AcProtoEntry ac_proto_db[] = {
""")

seen = set()
for e in P:
    key = (e["name"], e["variant"], e["bits"])
    assert key not in seen, "duplicate entry %r" % (key,)
    seen.add(key)
    kind, slen, sv, sm = sig_fields(e["sig"])
    out.write("    {\n")
    out.write("        .name = %s,\n" % cstr(e["name"]))
    out.write("        .variant = %s,\n" % cstr(e["variant"]))
    out.write("        .brands = %s,\n" % cstr(e["brands"]))
    out.write("        .app = %s,\n" % cstr(e["app"]))
    out.write("        .lead_mark = %d, .lead_space = %d,\n" % e["lead"])
    out.write("        .hdr_mark = %d, .hdr_space = %d,\n" % e["hdr"])
    out.write("        .bit_mark = %d, .one_space = %d, .zero_space = %d,\n"
              % e["bit"])
    out.write("        .bits = %d,\n" % e["bits"])
    out.write("        .msb_first = %d,\n" % e["msb"])
    out.write("        .sig_kind = %s, .sig_len = %d,\n" % (kind, slen))
    out.write("        .sig = %s,\n" % blist(sv))
    out.write("        .sig_mask = %s,\n" % blist(sm))
    out.write("    },\n")

out.write("};\n\nconst size_t ac_proto_db_count = "
          "sizeof(ac_proto_db) / sizeof(ac_proto_db[0]);\n")
sys.stderr.write("%d entries\n" % len(P))
