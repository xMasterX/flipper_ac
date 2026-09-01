# Flipper Zero AC remote apps

Twenty infrared air-conditioner remotes for the Flipper Zero, one app per IR
protocol, plus a detector that tells you which one you need. Each lives in its
own folder and builds independently with
[ufbt](https://github.com/flipperdevices/flipperzero-ufbt).

**Why separate apps rather than one universal remote?** Because you only own
one air conditioner. Install the single app that matches it and pin that to
your Flipper's favourites, and the remote is two button presses away — instead
of launching a large app and picking your way through brand and protocol menus
every time you want to change the temperature. Each app is also smaller, and
only ever offers the modes and buttons your unit actually has, so there is
nothing on screen that does nothing.

| App | Protocol | Also covers |
|-----|----------|-------------|
| [ballu_ac_remote](ballu_ac_remote) | Ballu YKR-K/002E, 13 B | |
| [carrier_ac_remote](carrier_ac_remote) | Carrier AC64, 64-bit | Surrey |
| [coolix_ac_remote](coolix_ac_remote) | Coolix, 24-bit | Beko, Midea OEMs |
| [daikin_ac_remote](daikin_ac_remote) | Daikin ARC433, 35 B | |
| [delonghi_ac_remote](delonghi_ac_remote) | De'Longhi PAC A95, 64-bit | |
| [fujitsu_ac_remote](fujitsu_ac_remote) | Fujitsu, 16 B + 7 B | Fujitsu General, OGeneral |
| [goodweather_ac_remote](goodweather_ac_remote) | Goodweather, 48-bit | **Rapid** |
| [gree_ac_remote](gree_ac_remote) | Gree, 8 B | Amana, Cooper & Hunter, EKOKAI, RusClimate, Soleus Air |
| [haier_ac_remote](haier_ac_remote) | Haier YR-W02, 14 B | Daichi, Mabe |
| [kelon_ac_remote](kelon_ac_remote) | Kelon, 48-bit | Hisense, AUX |
| [kelvinator_ac_remote](kelvinator_ac_remote) | Kelvinator, 16 B | **Gree YAP0F8/YAPOF3**, **Sharp A5VEY/YB1FA**, Green |
| [lg_ac_remote](lg_ac_remote) | LG / LG2, 28-bit | General Electric |
| [midea_ac_remote](midea_ac_remote) | Midea, 48-bit | Comfee, Danby, Kaysun, Keystone, Lennox, MrCool, Pioneer, Trotec |
| [mitsubishi_ac_remote](mitsubishi_ac_remote) | Mitsubishi Electric, 18 B | MSZ / MSH / MLZ series |
| [mitsubishi_heavy_ac_remote](mitsubishi_heavy_ac_remote) | Mitsubishi Heavy ZM-S, 19 B | |
| [neoclima_ac_remote](neoclima_ac_remote) | Neoclima, 12 B | Soleus Air |
| [panasonic_ac_remote](panasonic_ac_remote) | Panasonic, 27 B | |
| [samsung_ac_remote](samsung_ac_remote) | Samsung AR, 14 B + 21 B | |
| [tcl_ac_remote](tcl_ac_remote) | TCL112, 14 B | Daewoo, Electrolux, Leberg, Teknopoint |
| [toshiba_ac_remote](toshiba_ac_remote) | Toshiba, 9 B | Carrier (some) |

## Which app do I need?

Install **[ac_detector](ac_detector)** first, point your existing remote at the
Flipper's infrared window and press Power. It reads the frame, names the
protocol, lists the brands that ship it, and tells you which app above to use.

This matters more than it sounds, because the name on the front of an air
conditioner often has nothing to do with the protocol inside it. A Tornado, a
Vivax and a Beko may all speak Coolix; a Ballu speaks Electra; a Sharp may
speak Kelvinator. The detector reads what the remote actually sends.

Mitsubishi Electric and Mitsubishi Heavy Industries are different companies
with different protocols, and both have an app here. Kelvinator is worth
calling out too: Gree's YAP0F8 and YAPOF3 handsets and Sharp's A5VEY speak it,
not their own makers' protocols.

It knows 70 frame formats across 54 protocols and around 100 brands —
considerably more than this repo has apps for — so it will also tell you when
your unit uses something we have not built a remote for yet, and print the
payload so you can report it. Signals it cannot make sense of, such as flicker from lamps or a
switching supply, are filtered out and never disturb the reading on screen.
See [ac_detector/docs/DETECTION.md](ac_detector/docs/DETECTION.md) for how it
decides.

## Model pickers

Several manufacturers ship more than one frame format, and nothing in a
received signal says which one a given unit wants. Those apps put a **Model**
row on the Setup screen; the choice is saved to the SD card and survives a
restart. The names match what AC Detector prints on its Model page, so you can
read the format off the detector and pick the same entry.

| App | Models |
|-----|--------|
| daikin | ARC433, ARC477, ARC484, ARC423, BRC4C15, ARC480, BRC52B, DGS01 |
| haier | YR-W02, HSU07, AC160, AC176 |
| mitsubishi | 144-bit, 112-bit, 136-bit |
| fujitsu | ARDB1, ARJW2, ARRAH2E, ARREB1E, ARREW4E, ARRY4 |
| gree | YAW1F, YBOFB, YX1FSF |
| kelon | RCH-R0Y3, DG11R2 |
| mitsubishi_heavy | ZM-S, ZJ-S |
| lg, midea, panasonic, toshiba | see each app's Setup screen |

Samsung is the exception: its 21-byte "extended" frame is a message type
rather than a model — a real handset sends it for power, timer and sleep
changes and the short one otherwise — so the app does the same automatically
and offers no choice.

## Building

```bash
cd <app>_ac_remote
ufbt            # build
ufbt launch     # build, upload and run on a connected Flipper
```

They are developed against the **Unleashed dev** SDK. To point ufbt at it:

```bash
ufbt update --index-url=https://up.unleashedflip.com/directory.json --channel=dev
```

## Using them

The layout is the same in every app: **Mode** and **Fan** open a dropdown
(OK to open, OK again to confirm and transmit), temperature has `-` / `+`,
then the buttons that particular AC actually has, then **Extra** and **Setup**.

- An **inverted (filled) button** is the one the cursor is on.
- A **ring around a button** means that toggle is believed to be on. The AC
  sends nothing back, so it is the app's own guess, not a reading.
- Temperature changes are debounced by 800 ms, so holding `+` transmits once.
- **Extra** holds commands the main screen cannot express — fixed vane
  positions, timers, less common functions — and shows the payload of the last
  transmission at the bottom.
- **Setup** persists settings to the SD card, and on protocols with several
  incompatible handset generations it also picks the variant.

### Handset variants

Six apps expose a variant picker in Setup, because nothing in the IR signal
says which generation a unit expects:

| App | Setting | Options |
|-----|---------|---------|
| fujitsu | Model | ARRAH2E, ARREB1E, ARRY4, ARREW4E, ARDB1, ARJW2 |
| panasonic | Model | JKE, LKE, NKE, DKE, CKP, RKR |
| lg | Protocol | LG, LG2 |
| gree | Model | YAW1F, YBOFB, YX1FSF |
| midea | Model | Standard, Kaysun |
| toshiba | Remote | Generic, WA-TH0x |

All default to the first option. If a unit ignores the app entirely, this is
the first thing worth changing — LG vs LG2, for instance, differ only in the
header timings, so the wrong one is simply not decoded.

## Testing

Every app carries a host-side test for its encoder:

```bash
cd <app>_ac_remote && ./tests/run.sh
```

The protocol modules deliberately include nothing from the Flipper SDK, so
they compile and run with plain `cc` on a desktop. Each test rebuilds the
frame from the generated timings with an independent decoder and checks it
against the reference library's published constants, layout and checksum, over
every reachable mode / fan / temperature combination.

That verifies the *encoding*. It cannot verify that a particular air
conditioner responds — model variants are real, and only hardware settles that.

## Credits

The UI and app structure come from **[sokogen/flipperzero-htw-ac-remote][htw]**,
an HTW air-conditioner remote for the Flipper. These apps keep its layout,
state handling and IR transmit path, with the protocol layer replaced and the
views generalised so one template serves every protocol.

Protocol definitions were ported from two projects, and the tests check
against their published constants:

- **[IRremoteESP8266][ir]** — `src/ir_*.h` / `ir_*.cpp`. The primary reference
  for almost every protocol here; its bitfield unions are the strictest
  description of each frame.
- **[ESPHome][esp]** — `esphome/components/*`. Used where it is clearer, and
  the only source for Ballu, which IRremoteESP8266 does not implement.

Where the two disagree, IRremoteESP8266's union wins — for example ESPHome's
Gree component writes absolute Celsius into byte 1, which also sets a timer
bit; these apps follow the union instead.

[htw]: https://github.com/sokogen/flipperzero-htw-ac-remote
[ir]: https://github.com/crankyoldgit/IRremoteESP8266
[esp]: https://github.com/esphome/esphome

## Licence

MIT, inherited from the original HTW app. See [LICENSE](LICENSE).
