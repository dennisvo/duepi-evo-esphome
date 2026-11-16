# duepi-evo-esphome

An ESPHome external component that connects Duepi EVO pellet-stove controllers
to Home Assistant using an ESP32 and the controller's UART interface. Monitor
temperatures, operating state and alarms, with optional controls for HEAT/OFF,
target temperature and controller level.

The example starts in read-only mode. Normal controls and advanced installer/custom
commands must be explicitly enabled. The stove retains regulation; this component
is not a thermostat or a certified safety controller.

Inspired by [mvroosmalen's ESPHome-Duepi work](https://github.com/mvroosmalen1970/ESPHome-Duepi),
this project implements the protocol as a native C++ ESPHome component.
YAML configures the device and its Home Assistant entities.

**Use at your own risk.** The authors and contributors accept no responsibility
or liability for any damage, injury or loss arising from the use or misuse of
this project. The software is provided without warranty; see [LICENSE](LICENSE).

## Contents

- [Status and Scope](#status-and-scope)
- [Supported Pellet Stoves](#supported-pellet-stoves)
- [Setup](#setup)
- [Hub Configuration](#hub-configuration)
- [Normal Controls](#normal-controls)
- [Telemetry and Outcomes](#telemetry-and-outcomes)
- [Installer and Custom Commands](#installer-and-custom-commands)
- [Logging](#logging)
- [Development](#development)
- [License](#license)

## Status and Scope

The component is not tied to a particular ESP32 board: board selection and UART
pins are configured in YAML. Use an ESPHome-supported ESP32 board with the
ESP-IDF framework and suitable UART pins. The example has been validated and
compiled for the **Seeed XIAO ESP32-C3**, with ESPHome **2026.10.2** and ESP-IDF
**5.5.6**; other ESP32 boards have not yet been build-tested. Stove hardware,
controller firmware, wiring and electrical levels remain **unverified**.
ESP8266 and vendor-app/TCP connections are not supported.
Each ESP device connects to one stove; configure one `duepi:` hub per device.

See the [architecture](docs/ARCHITECTURE.md) and [protocol reference](docs/PROTOCOL.md)
for implementation and wire-protocol details.

## Supported Pellet Stoves

Because this component uses similar protocol logic to
[mvroosmalen's ESPHome-Duepi](https://github.com/mvroosmalen1970/ESPHome-Duepi),
it is expected to work with the pellet stoves below. Compatibility with this
component still needs to be confirmed for each model.

- Adler Hydro 25
- Artel MeNext
- Artel Next Petit Smart 6
- Artel Watt 9
- Duroflame Rembrand
- ETNA Cristallo Silenzio
- Eva Calor Thelma (DUEPI EVO VM88 board; upstream reports possible limitations)
- Interstove Milano
- King Mini 5S
- NextElite
- Qlima Delmara 88 S-Line
- Qlima Viola 85 S-Line

## Setup

### Hardware

See the [hardware reference](docs/HARDWARE.md) for collected connector pinouts,
cable color codes and connection notes, including the reported JP8 WiFi wiring.
Verify these details for your controller and cable before connecting.

For example hardware setups, see
[mvroosmalen's ESPHome-Duepi repository](https://github.com/mvroosmalen1970/ESPHome-Duepi).

**The tested stove's UART logic levels have been confirmed as 5 V with a
multimeter.** Although users report working setups with direct connections
between ESP boards and pellet stoves, those reports do not establish
electrical safety. ESP32 GPIO expects 3.3 V logic and is not 5 V tolerant, so a
suitable 5 V / 3.3 V UART logic-level shifter is advisable to avoid damaging the
ESP32 input. Verify the levels and wiring on your particular controller before
connecting, and start with read-only operation.

### Installation

1. Identify the controller model/firmware, connector orientation, supply and UART
	pinout. Do not assume a universal pinout or bypass stove safety systems.
2. Start from [examples/esp32-c3.yaml](examples/esp32-c3.yaml) as your ESPHome device configuration.
	For a different ESP32 board, change `esp32.board` to its ESPHome board ID and
	set `uart.tx_pin` and `uart.rx_pin` to suitable pins for that board. Keep the
	ESP-IDF framework and revalidate/recompile your configuration. The example's
	GPIO20/GPIO21 values are **unverified example pins**, not a
	verified stove wiring diagram or universal ESP32 pin choice. Review board pin
	conflicts and UART wiring before connecting.
3. Create a private `secrets.yaml` next to your configuration, with `wifi_ssid`,
	`wifi_password`, a unique fallback Wi-Fi `ap_password` and a unique per-device,
	base64-encoded 32-byte
	`device_encryption_key`. The values in [examples/secrets.yaml.example](examples/secrets.yaml.example)
	are placeholders; replace them before installing on a device.
4. Validate and compile the configuration in ESPHome, then install it on the ESP32.
	Add the device through Home Assistant's ESPHome integration using the same API
	encryption key. Verify communication
	health and readings before enabling any controls.

## Hub Configuration

The example includes telemetry and normal control entities but starts read-only.
Under `duepi:`, set `enable_writes: true` to allow normal controls. Alarm reset
additionally requires `enable_reset: true`; advanced commands require
`enable_installer: true`. All three flags default to false.

Enabling installer support includes its controls and API actions in the firmware
and generates one **Installer Access** switch. Access always starts off after
reboot and can be switched on/off in Home Assistant without rebuilding. Normal
controls do not depend on this switch. With installer support disabled, advanced
controls, actions and their implementation are excluded from the firmware.

`firmware_profile` defaults `auto`; alternatives are `"9200"`, `E670`, `E693`.
UART uses 115200 baud, 8N1; serial logging is disabled in the example.

Set room-temperature command limits on the hub:

```yaml
duepi:
  id: stove
  min_temperature: 10
  max_temperature: 25
  init_command: false
```

`min_temperature` and `max_temperature` default to 10 and 25 degrees Celsius.
They must be whole numbers within 10..35, with minimum less than maximum. They
limit selectable targets and actual commands; the controller may enforce its
own model-specific limits. These are **integration settings**, not installer
parameters: changing them sends no stove command and does not change the current
target. A valid target read from the stove remains visible outside these limits.

The example exposes **Minimum Target Temperature** and **Maximum Target
Temperature** under Configuration on the Home Assistant device page; see
[the configuration example](examples/esp32-c3.yaml) for their YAML definitions.
They work with stove writes disabled and obey the same limits as YAML. To move
both limits past an existing boundary, expand the range first. Rejected changes
leave both previous values intact.

Accepted changes apply immediately, including to queued targets, and are saved
for reboot restoration. Saved limits take precedence over YAML defaults. Allow
ESPHome's pending preference writes to finish before removing power. If Home
Assistant still displays the old selectable range, reload its ESPHome integration;
command enforcement does not wait for a reload. See
[State and Authorization](docs/ARCHITECTURE.md#state-and-authorization) for persistence
and entity-discovery details.

### Initialization Compatibility

The example exposes an **Initialization Compatibility** switch, default off.
Enable it only when the controller requires the extra initialization sequence.
It adds a firmware query before other requests, increasing protocol traffic. If
initialization times out, the unsent command is cancelled rather than replayed.

`init_command: true` opts into this mode at boot; otherwise runtime changes are
not restored after reboot. The switch is `platform: duepi`, `kind: initialization`
and `duepi_id: stove`. It works with writes disabled and sends nothing merely
because it is toggled. It does not reset alarms, reboot the stove or start ignition.
See [Queue and Recovery](docs/ARCHITECTURE.md#queue-and-recovery) for sequencing details.

### Polling and Timeouts

| Option | Default | Accepted range |
| --- | --- | --- |
| `update_interval` | 10s | 2s..1h |
| `runtime_interval` | 1h | 1min..24h |
| `response_timeout` | 800ms | 100ms..5s |
| `inter_transaction_delay` | 100ms | 20ms..2s |
| `quiet_time` | 250ms | 100ms..10s |
| `stale_after` | 45s | 5s..2h, greater than update interval |
| `arming_timeout` | 30s | 1s..120s |

Intervals use monotonic time, not HA/SNTP. Allow enough freshness margin for a full
poll cycle and missed optional replies.

## Normal Controls

The example includes climate HEAT/OFF, whole-Celsius targets (10..25 by default),
a last-requested controller level 1..5, and an explicit alarm-reset button.
Controls remain blocked until `enable_writes: true` is configured and the firmware
is rebuilt. Alarm reset additionally requires `enable_reset: true` on the hub;
it is never automatic. First check communication and readings with writes disabled.

Writes require fresh phase, room, alarm, firmware and board observations after
startup/recovery. Ignition requires observed Off and no alarm; level changes
require a heating phase. Outside ordinary shutdown, Off remains available with a
reported alarm. There is no independent fan control.

Automatic firmware selection chooses the corresponding start command. Override
`firmware_profile` only when appropriate for your controller; a profile does not
establish compatibility or change telemetry decoding or installer parameter
meanings. The [protocol reference](docs/PROTOCOL.md#write-commands) lists the mappings.

Boot, API reconnect and telemetry publication never restore or transmit desired
settings. Newer unsent target/level changes replace older ones. Cancelled or
timed-out writes are never automatically replayed. The component does not provide
emergency-stop guarantees; use the stove's own controls and safety procedures.

### Shutdown Protection

Requesting Off cancels older queued writes and blocks conflicting new ones.
Once ordinary stopping or cooldown is reported, all unsent writes are cancelled
and new writes are rejected, including redundant Off and custom/raw commands.
Recognized read-only queries continue under their normal access requirements.
Cancelled commands never replay; an already transmitted command cannot be recalled.

Eco cooldown is automatic temperature-driven cycling, not an explicit Off decision:
target changes and explicit Off remain available under normal authorization.
Their exact effect during eco cooldown is unverified. Shutdown initiated on the
stove's display is detected through periodic polling, not immediately. See
[Shutdown Protection](docs/ARCHITECTURE.md#shutdown-protection) for phase handling,
queue rules and detection limits.

## Telemetry and Outcomes

The [configuration example](examples/esp32-c3.yaml) lists the available entities
and their YAML keys. Rename or omit individual entities to suit your setup.
The binary `communication` sensor means fresh core stove replies, not merely a
connected Wi-Fi/API session. All entity platforms use `duepi_id: stove`.

Startup numeric telemetry is unknown; stale numeric telemetry becomes NaN/unknown and
text diagnostics become Stale. Unsupported optional telemetry degrades separately.
Runtime counters expire after twice their polling interval; their units are
unconfirmed. Reported power/pellet-feed levels are unitless, upstream-inferred
labels, not verified physical speeds; see the [protocol reference](docs/PROTOCOL.md#read-commands).
The numeric target sensor is observed
readback only. The level number is explicitly last-requested and
unverified. Alarm descriptions are source-derived hints; numeric codes remain.
`last_response` records wrapping uptime at the last accepted response, not UTC.

The climate target prefers a fresh controller reading. Without one, it retains
the most recent valid reading or acknowledged target request, whichever occurred
last. It starts unknown, has no invented default and is not restored after reboot.
The example's **Target Temperature Source** diagnostic (`target_source`) reports
`Reported`, `Retained reading (unverified)`, `Acknowledged request (unverified)` or
`Unknown`. Queued, rejected, cancelled and timed-out writes never change this
fallback; an ACK alone does not verify the stove's setpoint.

Unavailable target feedback makes the numeric sensor unknown while the climate
can retain its previous value. That retained value is not a new stove reading.

ESPHome climate has no unknown mode enum: before observation it has the framework
OFF placeholder, and stale/unknown phases retain the last mode/action. Always
consider the phase diagnostic, numeric freshness and communication sensor; OFF
alone is not proof of a stopped stove.

Transactions distinguish queued, sent, acknowledged/unverified, verified,
rejected, cancelled, timed out and indeterminate. Target/Off/ignition require
matching readback for verification; level/reset remain unverified. Custom responses
are diagnostic hex data, never telemetry. All writes, including apparently read-only
custom commands, are **never retried**. A missing ACK does not prove a command failed.
See [State and Authorization](docs/ARCHITECTURE.md#state-and-authorization) for
readback rules and response-correlation limits.

## Installer and Custom Commands

Installer support includes parameter reads/writes and custom/raw
commands. Enable it with `enable_installer: true` and `enable_writes: true`
under `duepi:`, then rebuild. Without this opt-in none of the advanced
entities, actions or command implementation are included. When included, all
advanced operations share **Installer Access**, which is off after every boot;
access is never restored. The parameter selectors, read/write buttons and result
sensors are generated automatically.
Native API actions are also registered automatically when `api:` is configured.

To inspect one parameter on demand in Home Assistant, enable **Installer Access**,
enter its numeric address (0..255) in **Installer Parameter Number**, then press
**Read Installer Parameter**. The raw returned value appears in **Installer
Result**, alongside the parameter number. Changing the selection alone sends
nothing; parameters are not scanned or periodically polled. No stove profile is
needed for a read, but Installer Access must be on for both reads
and writes. Confirm the address mapping in your controller documentation rather
than assuming a printed PR label is the serial address.

To write, first read the documented parameter and record its original value.
Enter the intended byte (0..255) in **Installer Proposed Raw Value**, press
**Write Installer Parameter**, then check **Installer Result**. The write uses
the currently selected parameter and proposed value. Editing
either field alone sends nothing. The proposed value starts at zero as a local
placeholder, not a recommended setting or an observed value, and is not restored
after reboot. Writes require Installer Access on, fresh observations
and the stove observed off. An ACK triggers a parameter-specific
readback: matching, mismatching and unverified outcomes remain distinct. A match
only verifies the returned byte, not physical operation or correct configuration.
No parameter meanings, recommended values or automated tuning are supplied.

If readback fails or is cancelled, the write remains unverified and is never
replayed. Fresh control observations are required before further commands.
Disabling Installer Access cancels pending advanced work, including readback,
but cannot undo an already transmitted write. It does not disable normal controls.

For custom use, enable Installer Access and invoke the relevant send action.
Authorization is automatic for that request and rechecked immediately before
transmission. Normal custom input is exactly four hexadecimal command
characters; raw input is exactly six uppercase alphanumeric payload characters.
The hub supplies framing/checksum. **Custom commands bypass normal firmware,
alarm, phase and installer parameter semantics**, including the off-state check,
**except for ordinary-shutdown and pending-Off protection**, which cannot be bypassed.
Treat either custom interface as unrestricted controller access, not safe reads.
Encrypted API access is device-wide, not per-action authorization; restrict who
can call it or omit these features entirely.

Custom Response shows `Pending` when a new command is queued or sent and
`Unavailable` if it is rejected, cancelled or times out, including rejection for
invalid input or disabled access. A valid reply replaces that marker with its
hexadecimal payload. An older request's completion cannot overwrite a newer
attempt's response diagnostic. These replies remain diagnostic,
not verification of physical operation.

| Automation action | Fields besides `id: stove` | Required configuration/runtime state |
| --- | --- | --- |
| `duepi.installer_read` | `parameter` integer 0..255 | `enable_writes`, `enable_installer`, Installer Access on, fresh observations |
| `duepi.installer_write` | `parameter`, `value` integers 0..255 | Same, observed off; authorization automatic |
| `duepi.send_command` | `command` four hex characters | Same access/freshness requirements; authorization automatic, off not required |
| `duepi.send_raw` | `command` six uppercase alphanumeric characters | Same as `duepi.send_command` |

The YAML automation actions and native API actions handle authorization internally,
just like the Write button; no separate arming step is needed. Fields support ESPHome lambdas.
For hub `id: stove`, the generated native API actions
are `stove_installer_read(parameter)`, `stove_installer_write(parameter,value)`,
`stove_send_command(command)` and `stove_send_raw(payload)`; Home Assistant prefixes
these with the node name. Another attempt after a failed write requires a new
write/send. See [State and Authorization](docs/ARCHITECTURE.md#state-and-authorization)
for authorization expiry and observation recovery.

## Logging

All component messages use the `duepi` tag. The example defaults to `INFO`, with
serial logging disabled (`baud_rate: 0`) so logs cannot corrupt the stove UART.
Use ESPHome's network logs for the device.

Detected firmware is logged at `INFO`, for example `Controller firmware: 0x9200`,
on first observation and whenever it changes, not on every poll. It is also
available through the **Controller Firmware** text sensor in the example.

- `INFO`: startup settings, controller identity, phase/alarm changes, communication
	restored, operator write outcomes and configuration changes. Routine successful
	polling is quiet. An ACK is explicitly unverified until supported readback succeeds.
- `WARN`: communication health lost, exhausted read attempts, rejected or
	indeterminate writes, reported alarms, installer readback mismatches and failed
	preference saves. Repeated read-timeout and identical rejection warnings are rate-limited.
- `DEBUG`: named transactions and tokens, arguments/parameter addresses, queue
	outcomes, gate details, unavailable target feedback, read retries, initialization
	and quiet recovery, plus summarized parser errors.
- `VERBOSE`: bounded transmitted-frame contents and accepted response payloads.
	Installer/custom data may appear in diagnostic logs; review before sharing.

To enable debugging, change the logger level and rebuild:

```yaml
logger:
  baud_rate: 0
  level: DEBUG
```

Use `VERBOSE` temporarily for frame diagnostics. In a custom configuration set
`logger.level` directly. The global level is a compile-time ceiling: setting only
`logger.logs.duepi: DEBUG` while global is `INFO` cannot recover omitted messages.
Raise the global level first, then lower noisy tags in `logger.logs` as needed.
Detailed startup configuration also requires `DEBUG` or above in the pinned
ESPHome version. Logging is not a durable audit record or proof of physical action.

## Development

Use Python 3.13 and the pinned development dependencies. Configure pip to use
your required package index before installing. For example, on Windows:

```text
py -3.13 -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
.venv\Scripts\python.exe tests/run_host_tests.py
.venv\Scripts\python.exe tests/test_config.py
.venv\Scripts\python.exe tests/validate_examples.py config
.venv\Scripts\python.exe tests/validate_examples.py compile
```

On Linux or macOS, create the environment with `python3.13 -m venv .venv` and
use `.venv/bin/python` in place of `.venv\Scripts\python.exe`.

Validation binaries use public dummy credentials and must not be deployed.
See [development details](docs/ARCHITECTURE.md#development) for build variants,
validation tooling and CI requirements.

## Thanks

A huge thanks to [aceindy](https://github.com/aceindy/Duepi_EVO) and
[mvroosmalen](https://github.com/mvroosmalen1970/ESPHome-Duepi) for sharing their
Duepi protocol work and implementations with the community. Aceindy's Duepi_EVO
inspired mvroosmalen's ESPHome-Duepi, which in turn inspired this project.

## License

This repository is licensed under the [MIT License](LICENSE).
