# How AC Detector identifies a remote

The Flipper's infrared worker hands the app raw timings: microsecond
durations that alternate mark (carrier on) and space (carrier off), starting
with a mark. A capture ends after 150 ms of silence, which usually means one
press produces one capture holding the whole frame, repeats included.

Everything below happens in `ac_decode.c`. It has no Flipper dependencies, so
`tests/run.sh` exercises it on the host.

## The shape of an air conditioner frame

Nearly every air conditioner remote uses pulse-distance coding: a fixed-width
mark followed by a space whose length is the bit. A frame is

```
header mark, header space, (bit mark, bit space) x N, stop mark
```

with two complications that matter here:

- **Sections.** Daikin, Panasonic, Gree, Kelvinator and others break the frame
  into pieces separated by a long gap. Some repeat the header at each piece,
  some do not, and Daikin opens with five stray bits before any header at all.
- **Repeats.** Most remotes send the frame two or more times per press. Since
  the receiver only gives up after 150 ms, all of them land in one capture.

## Matching

Rather than guess the protocol from the numbers and then look it up, the
decoder walks the capture once **per candidate**, using that candidate's own
timings as the yardstick:

```
for each row in the database:
    parse the capture as if it were this protocol
    if anything does not fit, reject the row outright
    otherwise score how well the timings agree
```

A row is rejected the moment a mark or space falls outside its window, so a
wrong protocol almost never produces a parse at all. This is also what makes
the section and preamble handling cheap: the parser knows what a header looks
like for the row it is testing, so it can tell a header from a bit without
guessing.

### Scoring is bias-free

An infrared receiver holds its output low a little longer than the carrier
actually lasts. Marks come back long and spaces short, by roughly the same
amount - somewhere around 50 to 150 us depending on the receiver and how
bright the remote is. IRremoteESP8266 calls this the mark excess.

The obvious fix, subtracting a fixed excess before comparing, is wrong, and
was wrong here for a while: it took a real 325 us TCL zero-space to 385 us,
which is exactly Mitsubishi112's nominal, and clean captures started
identifying as the wrong protocol.

What the scorer compares instead is **mark-plus-space periods**. Whatever the
receiver adds to a mark it takes off the space that follows, so the period is
untouched:

```
err = rel(hm + hs, HDR_MARK + HDR_SPACE)
    + rel(hs,      HDR_SPACE)
    + 2 * rel(bm + one,  BIT_MARK + ONE_SPACE)
    + 2 * rel(bm + zero, BIT_MARK + ZERO_SPACE)
    + rel(one,     ONE_SPACE)
```

The two standalone terms are on values large enough that a hundred
microseconds barely moves them, and they separate protocols that share a
period but split it differently. The parser still uses windows wide enough to
absorb the bias in either direction - that is where tolerance belongs.

### Ranking

1. A confirmed signature beats no signature.
2. An exact bit count beats a wildcard.
3. Then the lowest timing error wins.

### Signatures

Timings are not always enough. Coolix and Midea use the same line code and,
because Midea sends its six bytes and then the same six inverted, the same
number of bits on the wire. Only the payload separates them:

| Check | What it looks for |
| --- | --- |
| `AcSigCoolix` | three byte pairs, each second byte the complement of the first |
| `AcSigMidea` | six bytes then the same six inverted, under a `0b10100` header |
| `AcSigPrefix` | leading bytes match under a mask, searched at byte offset 0-2 |

The offset search is there for preambles: Daikin's five stray opening bits
push `11 DA` past the first byte.

### Repeats

After parsing, a capture whose bit count is an exact multiple of the row's is
checked copy against copy. If the copies are identical it is a repeat, and the
screen shows one frame with an `xN` marker. If they are not identical it is
not a repeat and the row is rejected - which is precisely what stops Coolix,
whose repeats are identical, from claiming a Midea frame, whose second half is
inverted.

## The noise gate

The screen only changes for something that looks like a remote. A capture that
matches no row still has to clear a structural test before it is shown as
`Unknown`:

- at least 48 timings, and at least 20 data bits
- a first mark between 1500 and 30000 us, and a plausible header space
- bit marks that agree: at least 88% of them within 40% of the median, and the
  median itself between 200 and 1400 us
- spaces that fall into two clean groups, each within 40% of its own mean

Lamp flicker, switching-supply hash and stray reflections fail the mark
agreement test essentially always, because they have no fixed bit width to
agree on. Those captures bump the counter shown on the idle screen and leave
whatever was last identified alone.

Truncated frames - a long Hitachi capture cut at the worker's 1024-timing
ceiling, or a press caught halfway - fail too, because a capture that starts
mid-frame has a bit mark where a header should be.

## Where the numbers come from

`ac_protocol_db.c` is generated by `tools/gen_db.py`.

- Timings for the fifteen protocols this repo ships a remote app for were
  taken from those apps, which were checked against real air conditioners.
- The rest come from IRremoteESP8266's `ir_*.cpp` constants.
- Brand lists start with the devices IRremoteESP8266 documents as tested and
  continue with OEM rebrands that are widely reported but that we have not
  measured. A brand appearing here is a lead, not a guarantee.

`bits` is the number of data bits **on the wire**, filler included, which is
not always eight times the state length. Gree carries eight state bytes but
puts three constant bits in the middle, so it is 67.

## Tests

`tests/run.sh` links the decoder against the protocol encoders from the
fifteen remote apps and checks that the detector names each one correctly -
clean, and with a receiver bias and jitter applied. It also:

- checks Coolix and Midea do not steal each other's signals
- feeds a doubled frame and checks it collapses to one
- feeds five kinds of noise and checks all of them are ignored
- feeds a well-formed frame from no known protocol and checks it reads as
  `Unknown` rather than being forced onto the nearest row
- builds a frame from **every** database row and checks it identifies as
  itself, which is how an ambiguous pair would surface
- feeds truncated Daikin captures and checks none becomes a false match
