# Flipper Zero AC remote apps

Fifteen infrared air-conditioner remotes for the Flipper Zero, one app per IR
protocol. Each lives in its own folder and builds independently with
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
| [gree_ac_remote](gree_ac_remote) | Gree, 8 B | Amana, Cooper & Hunter, EKOKAI, RusClimate, Soleus Air |
| [haier_ac_remote](haier_ac_remote) | Haier YR-W02, 14 B | Daichi, Mabe |
| [lg_ac_remote](lg_ac_remote) | LG / LG2, 28-bit | General Electric |
| [midea_ac_remote](midea_ac_remote) | Midea, 48-bit | Comfee, Danby, Kaysun, Keystone, Lennox, MrCool, Pioneer, Trotec |
| [mitsubishi_heavy_ac_remote](mitsubishi_heavy_ac_remote) | Mitsubishi Heavy ZM-S, 19 B | |
| [neoclima_ac_remote](neoclima_ac_remote) | Neoclima, 12 B | Soleus Air |
| [panasonic_ac_remote](panasonic_ac_remote) | Panasonic, 27 B | |
| [tcl_ac_remote](tcl_ac_remote) | TCL112, 14 B | Daewoo, Electrolux, Leberg, Teknopoint |
| [toshiba_ac_remote](toshiba_ac_remote) | Toshiba, 9 B | Carrier (some) |

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
