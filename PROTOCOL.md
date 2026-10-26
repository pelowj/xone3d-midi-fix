# Xone:3D firmware update protocol and patch details

Worked out from Allen & Heath's Mac configuration utility (Xone3D.app v1.00) and confirmed against
the Windows utility (Xone3Dcfg v1.16) for interoperability with current macOS.

## Device

- USB ID `0a4a:ff00`, USB 1.1 full speed.
- USB controller: TI TUSB3200A. Its boot ROM loads the application from an I2C EEPROM.
- `bcdDevice` (0x0100) is not the firmware version; use request 0.

## Vendor requests

All on endpoint 0, `wValue` 0x0100, `wIndex` 1. `bmRequestType` 0xC2 for IN, 0x42 for OUT.

| bRequest | Dir | Length | Meaning |
|---|---|---|---|
| 0 | IN | 16 | Firmware version string, e.g. `v1.26` |
| 1 | IN | 2 | Read the settings ("routing") word |
| 2 | OUT | 2 | Write the settings word |
| 5 | OUT | 0 | Enter firmware update mode |
| 6 | IN | 6 | Update status |
| 7 | OUT | 16 | Write one 16-byte firmware block |

Status (request 6): bytes 0–1 = current address, bytes 2–3 = error counter, byte 5 bit 0 = busy.

Settings word: bits 4–7 = MIDI channel − 1, bit 8 = soundcard mode, bit 14 = digital out,
bit 15 = MIDI map, bits 0–3, 9, 10 = soundcard input selections.

## Update procedure

1. The image must be 65536 bytes and start with `00 32 51 04`.
2. Send request 5.
3. For each 16-byte block from offset 0x0000 to 0xFFE0 (the last 16 bytes are never sent):
   send request 7 with the block, then poll request 6 every 10 ms while busy. If the error counter
   changed, send the block again (waiting 100 ms from the second attempt), up to 50 attempts.
4. Power-cycle the mixer.

## Image format

The file is a TUSB3200A EEPROM image with a 12-byte header:

| Offset | Size | 1.26 value | Field |
|---|---|---|---|
| 0 | 4 | 0x04513200 | Signature |
| 4 | 1 | 12 | Header size |
| 5 | 1 | 0 | Header version |
| 6 | 1 | 0x0A | EEPROM type |
| 7 | 1 | 1 | Data type (application) |
| 8 | 2 | 0x1E7C | Payload size |
| 10 | 2 | 0x7267 | Checksum |

The stored checksum is the 16-bit byte sum of the payload minus one, in the official 1.26 file.
The patch keeps that relation.

## The patch

The MIDIStreaming interface (class 1, subclass 3) has bulk endpoints 0x03 (OUT) and 0x83 (IN),
each declared with `wMaxPacketSize` 4 in each of the three copies of the configuration descriptor the image carries (at 0x24, 0x123 and 0x222). Full-speed bulk
endpoints must use 8, 16, 32 or 64, and current macOS will not create the endpoints. The firmware
configures 8-byte hardware buffers for endpoint 3, so 8 is the value to use. Don't use 64.

| Offset | Stock | Patched | What |
|---|---|---|---|
| 0x00A | 0x67 | 0x7F | Checksum low byte (0x7267 → 0x727F, +24) |
| 0x10B | 0x04 | 0x08 | EP 0x03 wMaxPacketSize, copy 1 |
| 0x119 | 0x04 | 0x08 | EP 0x83 wMaxPacketSize, copy 1 |
| 0x20A | 0x04 | 0x08 | EP 0x03 wMaxPacketSize, copy 2 |
| 0x218 | 0x04 | 0x08 | EP 0x83 wMaxPacketSize, copy 2 |
| 0x309 | 0x04 | 0x08 | EP 0x03 wMaxPacketSize, copy 3 |
| 0x317 | 0x04 | 0x08 | EP 0x83 wMaxPacketSize, copy 3 |

SHA-256 of stock 1.26: `19cb34f028995ec011bb77585c09d3138b5c2c4de0113c9050e90867cae6b2e2`
SHA-256 after patch: `f7fe65ba554a4201de5206836b70a376b81600ba8deffb669c7e624e179432a6`
