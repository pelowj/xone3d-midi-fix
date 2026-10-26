# Xone:3D USB MIDI fix for macOS

An unofficial tool that makes USB MIDI work again on the Allen & Heath Xone:3D with current macOS.
It is not made, endorsed or supported by Allen & Heath.

## The problem

On recent macOS (tested on macOS 27, Apple silicon) the Xone:3D works as a USB soundcard, but its
MIDI port stays offline in Audio MIDI Setup: no controls reach your DJ software, and nothing
lights up.

The firmware declares its two USB-MIDI endpoints with a maximum packet size of 4 bytes. The USB
specification only allows 8, 16, 32 or 64 for this kind of endpoint. Current macOS refuses to
create the endpoints, so the MIDI port never comes up.

## The fix

`x3dfix` takes Allen & Heath's official firmware 1.26 file, changes those two endpoints to 8 bytes
(the buffer size the firmware already uses in hardware), corrects the image checksum, and flashes
it with A&H's own update protocol. Seven bytes change in total. The version still reads v1.26.

The firmware itself is Allen & Heath's and is not included here. Download it from A&H.

Windows and Linux users probably don't need this. Forum reports suggest the stock firmware's MIDI
works on both, but that hasn't been confirmed here.

## What you need

- A Xone:3D and a Mac with the Xcode command line tools (`xcode-select --install`).
- `XONE3D 1.26.BIN`, from Allen & Heath's Xone:3D downloads (the Mac bundle `XONE_3D_ALL_MAC.zip`
  has it in its firmware folder). The tool checks the file's SHA-256 and refuses anything other than
  the exact official file:
  `19cb34f028995ec011bb77585c09d3138b5c2c4de0113c9050e90867cae6b2e2`

## Use

```sh
make
./x3dfix "XONE3D 1.26.BIN"                 # dry run: checks the file, reads the mixer, writes nothing
caffeinate -dims ./x3dfix --flash "XONE3D 1.26.BIN"
```

The update takes about a minute. Don't unplug the mixer or let the Mac sleep while it runs
(`caffeinate` prevents sleep). When it finishes, power the Xone:3D off and on again.

Then open Audio MIDI Setup → MIDI Studio: the Xone3d device should be online. The 3D sends on
the MIDI channel set on the unit (channel 16 by default).

`./x3dfix --save patched.BIN "XONE3D 1.26.BIN"` writes the patched image to a file without touching
the mixer, if you want to inspect it.

## Risks and recovery

Flashing firmware can leave a device unusable. Use this at your own risk. So far it has been tested
on one unit.

What has happened in testing:

- An update stalled at the very first block (the mixer stayed busy). After a power cycle the USB side
  still worked, but the mixer, display, BPM, LFO and button lights were dead. **Flashing again
  straight away fixed it completely.** If an update stalls or the mixer misbehaves afterwards,
  power-cycle it and run `--flash` again.
- Some USB-C adapters have been less reliable than a powered hub; use a hub or a direct port if you can.
- The update resets the mixer's settings word (MIDI map, soundcard input selections) to defaults.
  Check those settings afterwards if you had changed them.

To go back to stock firmware, flash the unmodified 1.26 with A&H's own utility on a PC.

## Files

- `x3dfix.c`: the patcher and flasher (one file, IOKit only).
- `PROTOCOL.md`: the Xone:3D firmware update protocol and the details of the patch.

## Licence

MIT, see `LICENSE`. This covers the code in this repository only, not Allen & Heath's firmware.
Allen & Heath and Xone are trademarks of Allen & Heath Ltd.
