# Protocol Reference

Commands and labels follow MyDPremote 3.2.5. Framing and additional diagnostics use
[mvroosmalen's ESPHome-Duepi](https://github.com/mvroosmalen1970/ESPHome-Duepi/tree/20171fd82f06662151f2857c5fd3bf4ac58a550b).
Support can vary by controller and firmware.

See [Stove and Integration Behavior](BEHAVIOR.md) for the distinction between
selected power, actuator telemetry, observed stove behavior and integration policy.

## Controller Families

Duepi distinguishes **Air** controllers for room-air heating from **Hydro**
controllers for water heating. The `C400` board-type byte identifies these as
0 and 1 respectively. This project focuses on Air; Hydro water/tank telemetry,
hydraulic controls and water-temperature targets are not implemented. The
10..35 C target range is for room-air control, not a Hydro temperature profile.

Board-type reporting is identification, not a compatibility check or command
lockout. Firmware variants are a separate distinction: `RE8I` selects different
alarm labels, while legacy startup overrides are explicitly configured. Neither
firmware identity nor shared commands establish model-specific capabilities or
installer parameter meanings.

## Framing

A frame is ten bytes: `ESC` (`0x1B`), six ASCII payload characters, two checksum
characters, then `&` (`0x26`). The checksum is the payload byte sum modulo 256,
encoded as two uppercase hex digits.

Normal request payloads are `R` + four-character command + `0`.
Numeric replies contain six hex digits. Most values use the first four;
runtime counters use all six; board, correction and installer values use the first two.
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
| D200 | Flue Extraction Speed | First four hex digits; raw value |
| DB00 | Extraction Sensor Level | First four hex digits; raw value |
| DD00 | Flue Motor Correction | First byte; raw value |
| DE00 | Pellet Correction | First byte; raw value |
| D500 | Exhaust Fan Correction D500 (experimental; meaning unconfirmed) | Full six-hex-digit numeric reply; no scaling or units |
| D800 | Cycle Time | First two bytes formatted as decimal fields, e.g. 10:11 |
| D400 | Auger Level | 0..5; ignition can report 6/7 |
| C500 | Power | Selected level, 1..5 |
| D000 | Flue temperature | C |
| EF00 | Exhaust speed | Value * 10, RPM |
| DA00 | Alarm | 0 = no alarm |
| C000 | Inputs | Raw code; interpretation is board-dependent |
| C600 | Target temperature | Whole C; component accepts 10..35 |
| DF00 | PCB temperature | C |
| DC00 | Firmware | Four-character ID, e.g. A060 or RE8I |
| C400 | Board type | First byte: 00 = Air, 01 = Hydro; identification, not support |
| ED00 | Lifetime runtime | Full 24-bit counter; unit unverified |
| EE00 | Runtime since reset | Full 24-bit counter; unit unverified |

Climate presets follow **Power** (`C500`), not Auger Level or Fan Speed.
These are levels, not measured heat output or pellet flow. Disable `C500`
polling with `read_power_level: false` if unsupported.

`D200`, `DB00`, `DD00`, `DE00`, `D500` and `D800` are polled only when their entities
are configured. They default to diagnostic. Correction values have no assigned
unit or signed conversion; Cycle Time is not interpreted as minutes/seconds.

`exhaust_fan_correction_d500` is an experimental probe, not a confirmed correction
or cFAN mapping. It preserves all 24 numeric reply bits to avoid assuming a byte
width. Zero is published as zero; missing replies use normal timeout/recovery
handling, not a fabricated zero. Both examples enable this probe for investigation.

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
| Start | F001..F005 | Fresh reported level 1..5; defaults to 1 if unavailable/stale |
| Power | F001..F005 | Sent during ignition/heating |
| Target | F2xx | Whole C encoded as two hex digits |
| Alarm reset | D600 | Requires enable_reset |
| Legacy start: 9200 | F005 | Explicit firmware_profile override |
| Legacy start: E670/E693 | F006 | Explicit firmware_profile override |

Start requires fresh, synchronized readings, Off and no alarm. Firmware profile
overrides affect startup only.

Selecting Power while Off is rejected without sending or storing a choice:
`F00n` is also the startup command, so it must not be sent as an Off-state power
adjustment. During ignition/heating it sends `F00n`. After acknowledgement,
polling refreshes the control prerequisites and reads `C500`. A matching reply
within 15 seconds verifies the selected level, not physical output. Mismatch or
unavailable feedback leaves the write unverified. Requests and ACKs do not change
displayed power; only fresh controller feedback does. Stale/invalid feedback makes
the power controls unknown. See [behavior](BEHAVIOR.md#power-and-actuators).

`000000` acknowledges a write; it does not prove the setting took effect.
A missing ACK leaves the outcome unknown. Writes are never retried automatically.

## Installer and Custom Commands

Installer payloads replace the complete six-character payload; do not prefix R.

| Operation | Payload | Value |
| --- | --- | --- |
| Read parameter | Lpp000 | pp = parameter byte in hex; reply uses first byte |
| Write parameter | Sppvv0 | pp = parameter byte, vv = value byte, both hex |

Installer writes require a zero-data ACK and matching readback for verification.
They require a fresh known phase outside ignition (`01xx`), checked at enqueue
and send; heating, cleaning, eco and shutdown are not excluded. Explicit Off
submission cancels older queued writes. General operations require Installer
Access. Optional `standby_mode` uses an independent exact-address 0/1 permission;
address 28 was confirmed on the tested stove, not established as universal.
See [parameter-change guidance](../README.md#read-or-write-a-parameter).

Custom commands use either a four-hex-digit body (`Rxxxx0`) or a complete
six-character raw payload of uppercase letters and digits.
They require Installer Access and retain shutdown and pending-Off protection.
Raw installer-write payloads beginning with `S` also retain the non-ignition guard.
They are never retried, even when used for reads.