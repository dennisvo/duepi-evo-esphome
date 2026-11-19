# Protocol Reference

Commands and app findings are based on analysis of supplied copies of Duepi's
MyDPremote 3.2.5 and MyDPRemote Evo 1.0.1 manufacturer apps. Framing, phase detail
and additional diagnostics also reference
[mvroosmalen's ESPHome-Duepi](https://github.com/mvroosmalen1970/ESPHome-Duepi/tree/20171fd82f06662151f2857c5fd3bf4ac58a550b).
The tables describe current component decoding and known app commands, not an
exhaustive vendor specification. App analysis is not hardware verification;
support and meanings can vary by controller and firmware.

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
encoded as two uppercase hex digits. It is an additive checksum, not a polynomial CRC.

Normal request payloads are `R` + four-character command + `0`.
Numeric replies contain six hex digits. Most values use the first four;
runtime counters use all six; board, correction and installer values use the first two.
Firmware IDs use the first four characters and may contain non-hex letters.

Synthetic example, with spaces added for readability:

- Request: `ESC RD100057 &`
- Reply: `ESC 00D7003B &` = 21.5 C

Replies have no transaction ID or command identifier. Late replies can be
mistaken for a subsequent response despite checksum and value validation.

## Numeric Formats and Codes

Wire numbers are ASCII hexadecimal, with the most significant byte first.
For numeric replies, the six data characters represent three bytes:

| Format | Data used | Representation range |
| --- | --- | --- |
| U8 | First two hex characters | 0..255 |
| U16 | First four hex characters | 0..65535 |
| U24 | All six hex characters | 0..16777215 |

These are encoding ranges, not safe operating limits. Unless stated otherwise,
the read table uses U16. For example, reply data `00D700` contains U16 `0x00D7`
= 215; dividing by ten gives room temperature 21.5 C. Cycle Time instead formats
the first two bytes as minutes and seconds: `0A0B00` becomes `10:11` remaining
on the controller's active countdown. See [cycle time](BEHAVIOR.md#cycle-time).
Firmware is text: `RE8I00` identifies `RE8I`, not a hexadecimal number.

Keep the different numeric domains separate:

- A command body such as `D900` identifies a request, not the returned phase.
- Phase `0x0201` is decimal 513: family `0x02`, subphase `0x01`. Eco phase
	`0x1000` is decimal 4096; its family `0x10` is decimal 16. The older app uses
	only the family byte; the component retains the full word.
- Alarm code 12 is decimal (`0x000C`); its meaning depends on the alarm profile.
	Inputs are a separate raw word: `0x0800` is decimal 2048, with meaning unverified.
- A status such as `Read 8: timed out` uses an internal command index, not a wire
	address, alarm or installer parameter. Currently index 8 is target (`C600`),
	and index 19 is Cycle Time (`D800`); these indices are implementation details.
- Installer selectors use decimal addresses and values; their payload bytes are
	hex. For example, address 28 is encoded as `1C`, not `28`.

## Read Commands

Bodies below use the normal `Rxxxx0` request format.
An `R` prefix does not mean read-only: `RD6000` resets alarms (`D600` below).

| Body | Meaning | Value |
| --- | --- | --- |
| D900 | Phase | See phase table below |
| D100 | Room temperature | Value / 10, C; signed encoding unknown |
| D300 | Fan Speed | Raw value; unit unspecified |
| D200 | Flue Extraction Speed | First four hex digits; raw value |
| DB00 | Extraction Sensor Level | First four hex digits; raw value |
| DD00 | Flue Motor Correction | First byte; raw value |
| DE00 | Pellet Correction | First byte; raw value |
| D800 | Cycle Time | First byte minutes, second byte seconds of the active countdown; during heating, time until periodic cleaning |
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

`D200`, `DB00`, `DD00`, `DE00` and `D800` are polled only when their entities
are configured. They default to diagnostic. Correction values have no assigned
unit or signed conversion. Cycle Time is a phase-dependent countdown, published as
minutes:seconds text rather than elapsed burn time.

Out-of-range `C500` or `C600` replies mark that reading unavailable without
triggering transport recovery. Missing or malformed replies use normal timeouts.
Retained display values cannot verify a write.

With `init_command` enabled, a separate `DC00` query completes before each
non-firmware transaction. It does not reset or start the stove.

### Known but Unimplemented Reads

MyDPremote 3.2.5 contains these additional read commands and decoders. They are
not implemented or polled by this component; their presence in the app does not
establish support on a particular controller or a corresponding write command.

| Body | App meaning | App decoding / qualification |
| --- | --- | --- |
| D700 | S2 water/tank temperature | U16, degrees; Hydro-related and outside current scope |
| E000 | Hours | U8; controller clock behavior unverified |
| E100 | Minutes | U8; controller clock behavior unverified |
| E200 | Seconds | U8; controller clock behavior unverified |
| E300 | Day | U8; calendar behavior unverified |
| E400 | Month | U8; calendar behavior unverified |
| E500 | Year | U8; epoch/century unknown |

No additional BCD conversion was found in the inspected clock decoders. The
clock constants do not establish normal app polling or validated calendar limits.
The implemented `ED00`/`EE00` runtime reads come from the upstream reference,
not either app's recovered command catalogue; their units remain unverified.

## Phases

These are all phase words currently recognized by the component. Firmware names
within labels are retained identifiers, not a universal compatibility map.

| Hex word | Decimal | Component label |
| --- | --- | --- |
| `0x0000` | 0 | Off |
| `0x0101` | 257 | Ignition |
| `0x0102` | 258 | Ignition: fuel loading |
| `0x0103` | 259 | Ignition: flame |
| `0x0104` | 260 | Ignition: ventilation |
| `0x0105` | 261 | Ignition: E693 |
| `0x010A` | 266 | Delayed ignition |
| `0x0132` | 306 | Ignition: V100 fuel loading |
| `0x0133` | 307 | Ignition: V100 |
| `0x0200` | 512 | Heating: cleaning |
| `0x0201` | 513 | Heating |
| `0x0301` | 769 | Stopping |
| `0x0401` | 1025 | Cleaning |
| `0x0801` | 2049 | Cooldown 1 |
| `0x0802` | 2050 | Cooldown 2 |
| `0x1000` | 4096 | Eco idle |
| `0x1001` | 4097 | Eco cooldown 1 |
| `0x1002` | 4098 | Eco cooldown 2 |
| `0x1003` | 4099 | Eco off |
| Other | Other | Unknown phase |

Phase-dependent command restrictions are integration policy, not manufacturer
guarantees; see [operating behavior](BEHAVIOR.md). Unknown phases block normal
controls and typed installer writes; custom commands use the separate safeguards
below. Phase changes are detected by polling, not a separate query before every write.

## Alarms

The component follows the older app's profile distinction: firmware text `RE8I`
selects the alternate map; all other firmware uses the default. These labels are
not verified fault diagnoses for every stove. Raw codes remain available.

| Decimal code | Hex word | Default label | RE8I label |
| --- | --- | --- | --- |
| 0 | `0x0000` | No reported alarm | No reported alarm |
| 1 | `0x0001` | Ignition failure | Water thermostat |
| 2 | `0x0002` | Extraction sensor fault | Pressure/safety thermostat |
| 3 | `0x0003` | Insufficient extraction | No pellets |
| 4 | `0x0004` | Water overtemperature | Pressure/safety thermostat |
| 5 | `0x0005` | No pellets | Generic alarm |
| 6 | `0x0006` | Pressure/safety thermostat | Pressure/safety thermostat |
| 7 | `0x0007` | Pressure/safety thermostat | Exhaust motor fault |
| 8 | `0x0008` | Power loss | Generic alarm |
| 9 | `0x0009` | Exhaust motor fault | Generic alarm |
| 10 | `0x000A` | Unknown | Generic alarm |
| 11 | `0x000B` | Unknown | Generic alarm |
| 12 | `0x000C` | Unknown | Ignition failure |
| 13 | `0x000D` | Unknown | Generic alarm |
| 14 | `0x000E` | Unknown | Generic alarm |
| 15 | `0x000F` | Unknown | Power loss |
| 16 | `0x0010` | Unknown | Extraction sensor fault |
| 17 | `0x0011` | Unknown | Unknown |
| 18 | `0x0012` | Unknown | Insufficient extraction |
| 19 | `0x0013` | Unknown | Board overtemperature |
| 20 | `0x0014` | Unknown | Low water pressure |
| 21 | `0x0015` | Unknown | High water pressure |
| Other | Other | Unknown | Unknown |

`Generic alarm` is a mapped non-specific alarm; `Unknown` means no description
is mapped for that code/profile. Neither is equivalent to code 0.

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

`installer_parameter_min` and `installer_parameter_max` restrict the inclusive
address range before enqueue and transmission (defaults 0..255). They also apply
to standby and raw `L`/`S` parameter payloads. Out-of-range operations send nothing
to the stove. Values remain bytes (0..255); these bounds do not prove value safety
or cover unknown custom-command effects.

Installer writes require a zero-data ACK and matching readback for verification.
They require a fresh known phase outside ignition (`01xx`), checked at enqueue
and send; heating, cleaning, eco and shutdown are not excluded. Explicit Off
submission cancels older queued writes. General operations require Installer
Access. Optional `standby_mode` uses an independent exact-address 0/1 permission;
address 28 was confirmed on the tested stove, not established as universal.
See [parameter-change guidance](CONFIGURATION.md#read-or-write-a-parameter).

Custom commands use either a four-hex-digit body (`Rxxxx0`) or a complete
six-character raw payload of uppercase letters and digits.
They require Installer Access and retain shutdown and pending-Off protection.
Fresh synchronized readings are still required, but custom commands bypass normal
alarm and known-phase checks except for the raw `S` guard below.
Raw installer-write payloads beginning with `S` also retain the non-ignition guard.
They are never retried, even when used for reads.

## App Findings and Limitations

Analysis of MyDPRemote Evo 1.0.1, a different app generation from MyDPremote 3.2.5,
adds context but does not establish new controller capabilities:

- Power/start still uses `RF00n0`, Off `RF0000`, and selected-power feedback
	`RC5000`. No alternative power opcode or fix for unapplied level changes was found.
- The app can retain a locally requested temperature when readback is missing or
	zero. That is explicitly unconfirmed, not evidence that the controller applied it.
- Startup and standby power preferences are stored locally in inspected paths;
	the standby phase-family-16 path saves without sending. Resume behavior is not
	established. This is separate from the component's parameter-based `standby_mode` switch.
- A technician read-only diagnostic view was identified, not an installer/EEPROM
	editor. Neither analyzed app provides a verified installer parameter catalogue
	or a complete model-to-capability map.

The older app also contains optional extended framing beginning with `0x1C`,
but its inspected configuration UI leaves that option disabled. This component
implements only the ordinary `0x1B` frame. Vendor TCP/cloud connections and their
encryption are separate from the direct-UART protocol used here.