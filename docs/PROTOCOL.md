# Protocol Reference

Commands and labels follow MyDPremote 3.2.5. Framing and additional diagnostics use
[mvroosmalen's ESPHome-Duepi](https://github.com/mvroosmalen1970/ESPHome-Duepi/tree/20171fd82f06662151f2857c5fd3bf4ac58a550b).
Support can vary by controller and firmware.

## Framing

A frame is ten bytes: `ESC` (`0x1B`), six ASCII payload characters, two checksum
characters, then `&` (`0x26`). The checksum is the payload byte sum modulo 256,
encoded as two uppercase hex digits.

Normal request payloads are `R` + four-character command + `0`.
Numeric replies contain six hex digits. Most values use the first four;
runtime counters use all six; board and installer values use the first two.
Firmware IDs use the first four characters and may contain non-hex letters.

Synthetic example, with spaces added for readability:

- Request: `ESC RD100057 &`
- Reply: `ESC 00D7003B &` = 21.5 C

Replies have no transaction ID or command identifier. Late replies can be
mistaken for a subsequent response despite checksum and value validation.

## Read Commands

Bodies below use the normal `Rxxxx0` request format.

| Body | Meaning | Value |
| --- | --- | --- |
| D900 | Phase | See phase table below |
| D100 | Room temperature | Value / 10, C; signed encoding unknown |
| D300 | Fan Speed | Raw value; unit unspecified |
| D400 | Auger Level | 0..5; ignition can report 6/7 |
| C500 | Power | Selected level, 1..5 |
| D000 | Flue temperature | C |
| EF00 | Exhaust speed | Value * 10, RPM |
| DA00 | Alarm | 0 = no alarm |
| C000 | Inputs | Raw code; interpretation is board-dependent |
| C600 | Target temperature | Whole C; component accepts 10..35 |
| DF00 | PCB temperature | C |
| DC00 | Firmware | Four-character ID, e.g. A060 or RE8I |
| C400 | Board type | 00 = Air, 01 = Hydro; mapping unverified |
| ED00 | Lifetime runtime | Full 24-bit counter; unit unverified |
| EE00 | Runtime since reset | Full 24-bit counter; unit unverified |

Climate presets follow **Power** (`C500`), not Auger Level or Fan Speed.
These are levels, not measured heat output or pellet flow. Disable `C500`
polling with `read_power_level: false` if unsupported.

Out-of-range `C500` or `C600` replies mark that reading unavailable without
triggering transport recovery. Missing or malformed replies use normal timeouts.
Retained display values cannot verify a write.

With `init_command` enabled, a separate `DC00` query completes before each
non-firmware transaction. It does not reset or start the stove.

## Phases

| Code | Phase |
| --- | --- |
| 0000 | Off |
| 0101..0105, 0132/0133 | Ignition |
| 010A | Delayed ignition |
| 0200 | Cleaning while on |
| 0201 | Heating |
| 0301 | Stopping |
| 0401 | Cleaning |
| 0801/0802 | Cooldown |
| 1000..1003 | Eco states |

During stopping and cooldown (`0301`, `0801`, `0802`), reads continue but all
writes are blocked and queued writes cancelled. Eco cooldown (`1001`/`1002`)
still permits target changes and Off. Unknown phases do not authorize writes.
Phase changes are detected by polling, not a separate query before every write.

## Alarms

Default codes: 1 ignition failure, 2 extraction sensor, 3 insufficient extraction,
4 water overtemperature, 5 no pellets, 6/7 pressure/safety thermostat,
8 power loss, 9 exhaust motor. Other nonzero codes display Unknown.

`RE8I` uses a different map, including 12 ignition failure, 15 power loss,
16 extraction sensor, 18 insufficient extraction, 19 board overtemperature,
20 low water pressure and 21 high water pressure. Raw codes remain available.

## Write Commands

| Intent | Body | Restrictions |
| --- | --- | --- |
| Off | F000 | Explicit user request only |
| Start | F001..F005 | Selected level 1..5; defaults to 1 until read or selected |
| Power | F001..F005 | Sent during ignition/heating |
| Target | F2xx | Whole C encoded as two hex digits |
| Alarm reset | D600 | Requires enable_reset |
| Legacy start: 9200 | F005 | Explicit firmware_profile override |
| Legacy start: E670/E693 | F006 | Explicit firmware_profile override |

Start requires fresh, synchronized readings, Off and no alarm. Firmware profile
overrides affect startup only.

Selecting Power while Off stores the next startup level in RAM without sending
a command. During ignition/heating it sends `F00n`. After acknowledgement,
polling refreshes the control prerequisites and reads `C500`. A matching reply
within 15 seconds verifies the selected level, not physical output. Mismatch or
unavailable feedback leaves the write unverified.

`000000` acknowledges a write; it does not prove the setting took effect.
A missing ACK leaves the outcome unknown. Writes are never retried automatically.

## Installer and Custom Commands

Installer payloads replace the complete six-character payload; do not prefix R.

| Operation | Payload | Value |
| --- | --- | --- |
| Read parameter | Lpp000 | pp = parameter byte in hex; reply uses first byte |
| Write parameter | Sppvv0 | pp = parameter byte, vv = value byte, both hex |

Installer writes require a zero-data ACK and matching readback for verification.
Parameter meanings and recommended values are not provided.

Custom commands use either a four-hex-digit body (`Rxxxx0`) or a complete
six-character raw payload of uppercase letters and digits.
They require Installer Access and retain shutdown and pending-Off protection.
They are never retried, even when used for reads.