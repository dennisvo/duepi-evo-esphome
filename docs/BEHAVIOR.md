# Stove and Integration Behavior

This document separates stove concepts and installation observations from the
ESPHome component's behavior. Integration restrictions are software policy, not
proof that a controller cannot perform an operation. The stove retains regulation;
the ESP is not a thermostat or a safety controller.

**Evidence:** integration behavior below is established from the linked source.
User observations apply to the reported Artel Next Petit Smart 6 installation,
not every Duepi controller or firmware. Vendor-app command evidence does not
prove that a particular stove applies a command. Exact actuator relationships,
parameter meanings and undocumented state transitions remain unverified.

See the [protocol reference](PROTOCOL.md) for wire formats and register tables,
and [architecture](ARCHITECTURE.md) for queue and implementation mechanics.

## TCP Emulator Ownership

The optional [TCP Emulator switch](CONFIGURATION.md#tcp-emulator) transfers UART
ownership to a raw vendor-app connection. It starts off after reboot. While on,
all component polling and stove commands stop, including installer/custom and
standby operations. Normal controls remain visible but are rejected. Cached
telemetry is invalidated and Installer Access is disabled. A disconnected app
does not return control to the component; the switch must be turned off.

Turning it off closes sockets, discards buffered data and starts fresh
synchronization after a UART drain/quiet interval. No pending component command
is replayed. Switching modes cannot undo a command already sent to the stove.
The bridge forwards raw bytes without enforcing normal stove-command protections;
it is a trusted-LAN diagnostic tool, not a second concurrent controller.

## Power and Actuators

### Stove Behavior

Power is the selected stove setting. It influences both the pellet-feed auger
and ventilator speeds, but those speeds are separate quantities, not synonyms
for power. The auger is the mechanism that feeds pellets into the stove.

The user has observed the **Power** sensor changing when power is selected on
the stove panel. Remote power changes not applying remain unresolved; auger or
fan behavior is not evidence that the remote selection succeeded.

### ESPHome Behavior

The climate presets Min, Low, Medium, High and Max select levels 1 through 5.
The optional Power number uses the same request path, not an independent setting.
The Power sensor reports the controller's `C500` reading. Auger Level (`D400`)
and Fan Speed (`D300`) are read-only actuator telemetry, with no calibrated RPM
or flow conversion assigned to those registers by the component.

| Situation | Component response to a power selection |
| --- | --- |
| Off | Reject without sending or storing a choice; republish the reported selection |
| Ignition or heating, otherwise authorized | Send a level command without changing target temperature or requesting startup |
| Eco, standalone cleaning, unknown phase or ordinary shutdown | Reject the level command |
| Stale prerequisites, UART recovery, pending Off or installer write verification | Reject the selection |

An explicit HEAT request is required for remote startup. With the default
`firmware_profile: auto`, it uses fresh controller-reported power, or level 1
if that feedback is unavailable or stale. Explicit legacy firmware profiles override the startup
command; they do not change the normal level-write command.

A climate call containing OFF ignores any preset, target or HEAT work in that
call. Otherwise the adapter processes preset, target, then HEAT. A target-only
change never starts the stove; selecting HEAT while already running is rejected
by the Start policy rather than treated as a power change. This ordering is not
an atomic operation: a rejected preset or target does not prevent later fields
from being attempted. In particular, preset plus HEAT while Off rejects the
preset but still attempts startup at reported/default power. Automations should
request HEAT, wait for fresh synchronized ignition readings, then select power.

The preset/number consistently represents fresh controller-reported power, even
while Off, in eco or during shutdown; its presence does not authorize a write.
Valid `C500` readings update it, including panel changes. There is no local
next-start override, deferred selection or apply-on-resume command. The standard
Home Assistant controls remain clickable while changes are blocked; the component
republishes the reported selection and explains rejection in Transaction Result.

A request or acknowledgement never changes the displayed power. The last report
remains displayed only while fresh and synchronized core readings are healthy.
A fresh matching `C500` reply can verify a write; a mismatch displays the reported
level and records both values in Transaction Result. Auger and fan readings never
verify selected power. Values outside 1..5 make the Power sensor and controls
unknown without causing UART recovery. Disabling `read_power_level` prevents
this polling and power-write verification, not power commands; controls remain
unknown without fresh readback.

Recovery, incomplete synchronization, stale core readings or stale/invalid power
feedback clear the preset and make the number unknown. A readback-verification
timeout never promotes the request to reported state. Fresh readback is still
displayed even if it arrives too late to verify the command. No power selection
is persisted or automatically sent after reconnection.

Sources: [power requests](../components/duepi/duepi.cpp),
[control policy](../components/duepi/control.h),
[climate adapter](../components/duepi/duepi_climate.cpp).

## Operating States and Commands

### Stove Behavior

Reported states distinguish Off, ignition, heating, cleaning, stopping, cooldown
and eco. Do not infer a complete physical state machine from the integration's
labels. Eco is distinct from an explicit Off request; controller-specific eco
transitions and the physical effect of commands during eco are not established.

### ESPHome Behavior

Normal writes require fresh phase, room, alarm, firmware and board observations,
completed synchronization, and no UART recovery. A connected Wi-Fi/API session
alone is insufficient. Authorization is checked when submitted and before sending.

| Request | Additional component requirements |
| --- | --- |
| HEAT / Start | Observed Off and no reported alarm |
| Power on the wire | Recognized ignition/heating phase family and no reported alarm |
| Target temperature | Known phase outside ordinary shutdown, no reported alarm, configured target bounds |
| Off | Known phase outside ordinary shutdown; a reported alarm does not prevent it |
| Alarm reset | Explicit `enable_reset: true`; known phase outside ordinary shutdown |

Reset is never automatic. The component has no independent auger or fan control.
Phase `0200` (cleaning while on) belongs to the permitted heating family for
power changes; standalone cleaning `0401` does not. Eco permits target changes
and Off under normal authorization, but not Start or level writes. These are
component rules, not verified controller acceptance guarantees.

Requesting Off cancels older unsent writes and blocks conflicting new writes.
Observed stopping/cooldown (`0301`, `0801`, `0802`) cancels unsent writes and
blocks ordinary controls, including redundant Off and custom/raw commands.
Typed installer parameter writes, including Standby Mode, are exempt from the
observed-shutdown and pending-Off blocks; they use the non-ignition policy below.
Explicitly requesting Off still cancels older queued parameter writes. Recognized
reads continue under their own access requirements. An already sent command
cannot be recalled. Panel shutdown is detected by periodic polling, so detection
is not immediate. This is not an emergency-stop mechanism.

Climate maps Off and ordinary shutdown to OFF; eco to HEAT with idle action;
other recognized active phases to HEAT with heating action. These are UI mappings,
not measurements of flame or delivered heat. Before observation ESPHome has an
OFF placeholder; stale or unknown phases retain the last mode/action. OFF alone
therefore does not prove a stopped stove.

Sources: [control policy](../components/duepi/control.h),
[phase labels](../components/duepi/protocol.cpp),
[climate publication](../components/duepi/duepi_climate.cpp).

### Cycle Time

**Cycle Time** (`D800`) shows the controller's active countdown as minutes:seconds,
not elapsed burn time. Its purpose depends on the operating phase:

- During ignition, a timer can span multiple subphases or be replaced before
	reaching zero. In the observed sequence, it reloaded at fuel loading (`0x0102`),
	continued through flame (`0x0103`), then changed on entry to ventilation (`0x0104`).
- During heating (`0x0201`), it counts down to the next periodic cleaning cycle.
	The observed reload to about 45 minutes matched the configured 45-minute cleaning
	interval; this is a setting, not a universal default.

A phase-code change does not necessarily reset the timer, and the stove need not
remain in a phase until it reaches zero. Durations and timer behavior may vary
by controller and phase.

The component displays the returned minutes and seconds; it does not calculate
the countdown or use it to control stage transitions. Polling may not capture
the exact zero/transition instant.

## Temperature Targets

The stove regulates temperature. The component sends whole-Celsius targets within
its configured limits (10..25 by default, supported bounds 10..35). Changing the
integration's minimum/maximum limits sends no stove command and does not change
the stove's current target. These settings are not installer parameters.

Accepted limit changes apply immediately, including to queued targets, and are
saved for reboot restoration. Saved limits override YAML defaults; invalid changes
leave the previous limits intact. This persistence does not apply to power feedback
or retained temperature targets.

The numeric target sensor shows observed readback only. Climate prefers a fresh
valid report; otherwise it retains the latest valid report or acknowledged target
request in RAM. It has no invented temperature default and no reboot restoration.
Queued, rejected, cancelled or indeterminate writes cannot change that fallback.

| Target Temperature Source | Meaning |
| --- | --- |
| Reported | Fresh, usable controller reading |
| Retained reading (unverified) | Last valid report, no longer fresh or currently usable |
| Acknowledged request (unverified) | Last acknowledged target request, without a fresh usable report |
| Unknown | No usable report or acknowledged target retained |

An unavailable target reply makes the numeric sensor unknown while climate may
retain its value. It does not by itself mean the whole connection failed. A valid
reported target outside the configured command limits remains visible if within
the supported decoding range. A retained display is not a new stove observation.

Sources: [observed state](../components/duepi/state.h),
[feedback and publication](../components/duepi/duepi.cpp).

## Communication and Outcomes

Communication health means fresh phase, room and alarm readings, not Wi-Fi/API
connectivity. Firmware and board freshness are additional write prerequisites.
Startup numeric telemetry is unknown; stale numeric readings become unknown and
observed text fields become Stale. Runtime counters use a longer lifetime of
twice their polling interval. Their units remain unconfirmed.

Telemetry comes from polling, not from an unsolicited stove event stream. The
component accepts a response only while waiting for a request and only if its
framing, checksum and expected response domain are valid. Data received while
idle or recovering is discarded. Panel changes therefore become visible on a
later accepted poll, not immediately. Omitting a standard sensor entity does
not disable its polling; the five optional diagnostics are polled only when
configured. Unsupported optional reads can still cause normal timeout recovery.

Missing replies or malformed frames can trigger timeout and quiet recovery,
cancel unsent commands and require fresh synchronization before writes resume.
Reads have at most one retry; writes are never automatically retried, including
custom commands that a caller intends as reads. Boot, API reconnect and telemetry
publication do not issue desired settings. New unsent target/level requests
replace older ones; cancelled requests do not replay.

Unsent commands expire after five seconds, so queueing is not a promise of later
execution. With `init_command: true`, an extra firmware query precedes other
requests; an initialization timeout cancels the held command rather than sending
it after recovery.

Transaction Result distinguishes queued, sent, acknowledged/unverified, verified,
rejected, cancelled, timed out and indeterminate. An acknowledgement does not
prove application; a missing acknowledgement does not prove failure. Only the
latest normal command can be verified within the 15-second readback window:
target by target readback, power by `C500`, Off by reported Off, Start by reported
ignition. Reset remains unverified. These reports do not prove physical safety.
Successful background reads update telemetry and freshness without changing
Transaction Result. Before the first numbered normal or advanced request,
exhausted read timeouts can appear there; later background timeouts do not
overwrite command results. Parameter operations report to Installer Result;
custom outcomes use Transaction Result, with reply bytes in Custom Response Data.
Read timeouts also produce rate-limited warnings, and individual transactions
remain available in DEBUG logs.

General installer operations require build-time inclusion, Installer Access and
fresh synchronized prerequisites. Access starts off after reboot. Turning it off
cancels pending general advanced work, not an already transmitted write or
independent standby work. Custom responses remain diagnostic data, never
normal telemetry. Custom commands bypass normal alarm and known-phase checks,
but retain fresh synchronized prerequisites and ordinary-shutdown/pending-Off
protection; raw `S` writes additionally require a known non-ignition phase.
Sending a custom command invalidates observations and cancels
other unsent work. Installer write-readback completion also requires a fresh
observation cycle before normal controls resume.

Replies contain no usable request identifier, so even matching readback has
correlation limits. See [architecture](ARCHITECTURE.md#state-and-authorization).
The last-response diagnostic is wrapping device uptime at the last accepted
response, not UTC or response age. It updates on every accepted response and is
opt-in for debugging, commented out in both examples. Normal day-to-day monitoring
uses Stove Communication instead; one accepted response does not prove all core
readings are fresh.

Sources: [transaction handling](../components/duepi/transaction.cpp),
[installer handling](../components/duepi/privileged.cpp),
[publication](../components/duepi/duepi.cpp).

## Installer Parameters

`installer_parameter_min` and `installer_parameter_max` configure the inclusive
serial address range (defaults 0..255; both examples use 1..28). Reads and writes
outside it are rejected locally without UART transmission, including UI, API,
automation, standby and raw `L`/`S` parameter operations. Checks run at submission
and actual transmission. Standby configuration outside the range is invalid.
The parameter selector uses these bounds; values to write still accept 0..255.
The bounds do not establish safe values or constrain unknown custom commands.

Reads require Installer Access, fresh synchronized prerequisites and no active
parameter verification. They do not have the write-specific ignition guard.

Parameter writes accept byte addresses and values (0..255) in every known phase
except ignition (`01xx`), including heating, eco, cleaning and shutdown. Fresh
synchronized phase, room, alarm, firmware and board readings and an idle parameter
verification path are still required. The phase guard runs at submission and
actual transmission. Unknown/stale phase or recovery fails closed. A physical
phase change between polls can still escape detection; this is not an interlock.

Follow the [manufacturer's parameter-change guidance](CONFIGURATION.md#read-or-write-a-parameter):
never change parameters during startup because released gas can ignite and cause
overpressure; plan the changes first, prefer a non-burning stove, and minimize
programming-menu time if the stove is burning. Allowing a phase is a component
policy, not manufacturer certification of all live serial writes.

Input edits alone send nothing. Writes are never retried and are followed by
parameter-specific readback. An ACK alone is unverified; a matching byte does not
prove that a setting is appropriate. Completion requires a fresh observation cycle
before normal controls resume. Serial addresses and safe ranges are
controller-specific, not inferred from printed PR numbers.

## Standby Mode

The optional `duepi.standby_mode` block requires an explicit `parameter` byte and
creates one configuration switch (default name `Standby Mode`). Address 28 and
values 0/1 were confirmed as standby off/on on the tested installation only.
Other controllers need their own confirmed address and value mapping.

This feature authorizes only reads of that address and writes of 0 or 1. It does
not require or enable general Installer Access, arbitrary parameter operations,
custom commands or native installer API actions. It uses the same non-ignition
guard and write/readback/recovery path as general parameter writes. Disabling
general Installer Access does not cancel standby transactions.

Boot and reconnect never write a standby preference. The switch initially has no
reported state, then follows parameter reads after normal polling cycles and
write readback. A toggle or ACK does not change it; a mismatch shows the returned
0/1 value. Values above 1 are not interpreted. After invalid data, timeout or stale
communication, the switch retains its last confirmed value; it is not proof of a
fresh reading or successful write. Unsupported parameter reads can trigger normal
timeout/recovery, so leave this feature out on unconfirmed controllers.

The stove implements ECO-STOP and restart behavior. The supplied manual's example
uses target 20 C, stops above 21 C and restarts below 19 C with standby enabled.
Those thresholds are controller-specific. ESPHome implements no standby thermostat
and sends no automatic Start/Off commands to enforce them.