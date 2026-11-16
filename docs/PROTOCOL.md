# Protocol Reference

This reference describes the protocol used by the component, based on
[mvroosmalen's ESPHome-Duepi](https://github.com/mvroosmalen1970/ESPHome-Duepi/tree/20171fd82f06662151f2857c5fd3bf4ac58a550b).
The framing, command meanings and units have not yet been verified against
hardware captures.

## Framing

The upstream transmitter in `components/script.yaml` uses ESC (`0x1B`), a
six-character ASCII payload, a checksum, and `&` (`0x26`). The checksum is the
sum of the payload's ASCII bytes modulo 256, represented as hexadecimal.
Normal requests have payload `R` + four-character command + `0`; raw requests
already contain their complete payload. This component pads
checksums to two uppercase hex digits; this padding is not hardware-verified
for low checksums. All documented normal requests already have two-digit sums.

The response parser in `components/uart.yaml` expects ten bytes. Positions
1..6 (zero-based) are ASCII hex data and positions 7..8 hold the checksum over
those six data bytes. Ordinary values consume the first four data digits;
runtime counters consume all six; board and installer values consume the first
two. This component validates the response delimiters as ESC and `&`;
those delimiters still need hardware confirmation.

Example room-temperature request: `ESC RD100057 &` (spaces for readability).
Example response: `ESC 00D7003B &`, representing 21.5 degrees Celsius.
These are synthetic examples, **not hardware captures**.

Requests and responses do not expose an identifiable transaction ID.
Six hex data characters do not identify their query.
A late checksum-valid reply can be indistinguishable from a subsequent reply.
Quiet recovery, read retries and domain validation reduce but cannot eliminate
that ambiguity; reply correlation is not guaranteed.

## Read Commands

Commands below are four-character normal command bodies. `components/time.yaml`
supplies requests; `components/uart.yaml` supplies decoding. Units and labels
are inferred from that implementation and remain subject to hardware confirmation.

| Body | Meaning | Decoding / uncertainty |
| --- | --- | --- |
| D900 | Controller phase | First four hex digits; see phase families below |
| D100 | Room temperature | First four / 10, Celsius; signed encoding unknown |
| D300 | Power level | First four, unitless; also described as room-fan level |
| D400 | Pellet feed level | First four, unitless; also mapped to climate fan levels |
| D000 | Flue temperature | First four, Celsius |
| EF00 | Exhaust speed | First four * 10, RPM |
| DA00 | Alarm code | First four; 0 means no reported alarm |
| C000 | Inputs | First four; source interprets 0100/0300 as sealed/pressure loss; board dependent |
| C600 | Target temperature | First four, whole Celsius; support must be observed |
| DF00 | PCB temperature | First four, Celsius |
| DC00 | Firmware | First four digits, hex identifier |
| C400 | Board type | First two; 00 air / 01 hydro per source |
| ED00 | Lifetime runtime | Six hex digits; counter units need hardware confirmation |
| EE00 | Runtime since reset | Six hex digits; counter units need hardware confirmation |

The example labels these readings **Reported Power Level (D300)** and
**Reported Pellet Feed Level (D400)**, retaining the `d3000`/`d4000` YAML keys.
These names follow upstream interpretations, not a universal firmware specification.
[Duepi_EVO's definitions](https://github.com/aceindy/Duepi_EVO/blob/main/custom_components/duepi_evo/const.py)
also label them power level and pellet speed, but the inspected protocol PDF
does not document either read. Neither reading is established as measured fan
RPM, pellet flow, a percentage or heating power in kW. They must not be used as
verified readback of the requested controller level without model-specific evidence.

A checksum-valid `C600` response completes the target read even when the first
four digits are zero or outside 10..35 C. Those values indicate unavailable
feedback, not a valid observed target or a verified write. They invalidate the
observed target without causing transport recovery. Missing or malformed frames
still use normal timeout/recovery handling. Climate display retention is separate
from protocol observations and cannot satisfy target-write verification.

An optional compatibility preamble uses the existing `DC00` firmware query, not
an alarm-reset or ignition command. The idea is also described in
[aceindy/Duepi_EVO](https://github.com/aceindy/Duepi_EVO) as `init_command` before
a TCP polling/control session. This component optionally sends and completes
that query before each non-firmware transaction.
Whether a particular directly attached controller needs the preamble remains
unverified. Its reply must be consumed separately from the following request.

Source phase families: 0000 off; 0101..0105 and 0132/0133 ignition;
010A delayed ignition; 0200 cleaning while on; 0201 heating; 0301 stopping;
0401 cleaning; 0801/0802 cooldown; 1000..1003 eco states. Unknown phase values
must not be treated as known safe control prerequisites. Preserve the numeric
phase so new firmware is diagnosable.

The component conservatively treats `0301`, `0801` and `0802` as ordinary
shutdown: recognized reads continue, but queued writes are cancelled and new
writes are blocked, including redundant Off and custom/raw commands. Eco cooldown
`1001`/`1002` does not trigger this blanket lockout; target changes and an explicit
Off retain their normal permissions. Cleaning `0401` is not classified as shutdown.
These are integration rules, not a verified controller state-transition model.
Stage differences between cooldown 1 and 2, and the effect of target changes
during eco cooldown, remain unverified.

The [EVO LCD 7 community protocol notes](https://github.com/aceindy/Duepi_EVO/blob/main/InterstoveRemoteCmd.pdf)
describe `0801`/`0802` as cooling after switch-off and `0000` as ready for ignition
(page 2). The [Thelma/Kali manual](https://pelletkings.ie/wp-content/uploads/2023/03/EN-INSTRUCTION-MANUAL-3-BUTTON-DISPLAY-KALI-THELMA.pdf)
describes controlled cooling before Off and temperature-driven economy cycling
(page 4). Neither establishes a universal serial busy/rejection response or
proves that every Duepi firmware refuses every write during shutdown. The hub
uses periodic polling, not a new phase query before each write, so panel changes
can precede their observation.

Source alarm meanings for codes 0..14 include no alarm, ignition failure,
suction failure, insufficient air, water temperature, no pellets, pressure
switch, unknown, power loss, exhaust motor, board surge, expiry, unknown,
suction sensor and overheating. Unknown codes remain numeric diagnostics.

## Write Commands

`components/climate.yaml` indicates these normal command bodies:

| Intent | Body | Restrictions |
| --- | --- | --- |
| Off | F000 | Explicit user request only |
| Start, firmware 9200 | F005 | Selected by auto-detection or explicit profile |
| Start, firmware E670 or E693 | F006 | Selected by auto-detection or explicit profile |
| Other firmware start | F001 | Upstream fallback for other valid firmware IDs in auto mode |
| Requested level | F001..F005 | Overlaps ignition bodies; do not infer independent fan semantics |
| Target | F2xx | Whole Celsius represented by two hex digits |

All start commands require enabled writes, fresh synchronized observations,
observed off and no reported alarm. The fallback retains the same send-time
checks, no-retry behavior and ignition-state readback verification as the known
profiles. Firmware profiles select start commands only; they do not establish
compatibility for telemetry or installer parameters.

Upstream interprets response data `000000` as acknowledgement of writes.
Acknowledgement is not proof of resulting physical state. Missing ACK is an
indeterminate write, not proof it failed; non-idempotent writes must not retry.

Alarm reset uses body `D600`, installer reads use payload `Lpp000`, and
installer writes use payload `Sppvv0`, where `pp` and `vv` are two-digit hex parameter
and value bytes. These installer payloads replace the complete six-character
request payload; they are not prefixed with R. Installer reads consume the first
response byte. Writes require the ordinary zero-data ACK and matching readback
before byte-level verification. No hardware evidence or recommended parameter
values is supplied.

Custom normal bodies use the normal Rxxxx0 encoder; custom raw input replaces
the complete six-character payload, with strict uppercase alphanumeric bounds.
Both are treated as non-retryable potentially mutating transactions, regardless
of what their caller thinks the command does. They bypass semantic controls but
not ordinary-shutdown or pending-Off protection, and require Installer Access.
The component does not provide an app/TCP bridge.