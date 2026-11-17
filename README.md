# duepi-evo-esphome

An ESPHome external component that connects Duepi EVO **Air** pellet-stove
controllers to Home Assistant using an ESP32 and the controller's UART interface.
Monitor temperatures, operating state and alarms, with optional controls for HEAT/OFF,
target temperature and controller level.

Alarm reset and advanced installer/custom commands must be explicitly enabled. The stove
retains regulation; this component is not a thermostat or a certified safety controller.

Inspired by [mvroosmalen's ESPHome-Duepi work](https://github.com/mvroosmalen1970/ESPHome-Duepi),
this project implements the protocol as a native C++ ESPHome component.
YAML configures the device and its Home Assistant entities.

**Use at your own risk.** The authors and contributors accept no responsibility
or liability for any damage, injury or loss arising from the use or misuse of
this project. The software is provided without warranty; see [LICENSE](LICENSE).

## Contents

- [Status and Scope](#status-and-scope)
- [Stove Compatibility](#stove-compatibility)
- [Setup](#setup)
- [Hub Configuration](#hub-configuration)
- [Normal Controls](#normal-controls)
- [Telemetry and Outcomes](#telemetry-and-outcomes)
- [Installer and Custom Commands](#installer-and-custom-commands)
- [Logging](#logging)
- [Development](#development)
- [Todo](#todo)
- [License](#license)

## Status and Scope

Duepi distinguishes **Air** (room-air heating) and **Hydro** (water heating)
controllers. This project focuses on **Air**. Hydro-specific water/tank telemetry,
hydraulic controls and water-temperature targets are outside its scope. Reporting
the board type does not enable a Hydro profile or prevent commands on Hydro hardware.

The component is not tied to a particular ESP32 board: board selection and UART
pins are configured in YAML. Use an ESPHome-supported ESP32 board with the
ESP-IDF framework and suitable UART pins. The example has been validated and
compiled for the **Seeed XIAO ESP32-C3**, with ESPHome **2026.10.2** and ESP-IDF
**5.5.6**; other ESP32 boards have not yet been build-tested. Controller
compatibility, wiring and electrical levels must be checked for each installation.
ESP8266 and vendor-app/TCP connections are not supported.
Each ESP device connects to one stove; configure one `duepi:` hub per device.

See the [architecture](docs/ARCHITECTURE.md) and [protocol reference](docs/PROTOCOL.md)
for implementation and wire-protocol details.

## Stove Compatibility

The models below appear in
[mvroosmalen's ESPHome-Duepi compatibility reports](https://github.com/mvroosmalen1970/ESPHome-Duepi).
They are references, not a validated support list for this component. Confirm
that the actual controller is Air and check its firmware and behavior for each installation.

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

Upstream also lists **Adler Hydro 25**. Shared monitoring and control functions
may work on this and other Hydro models, but compatibility is unconfirmed and
Hydro-specific features are not implemented.

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
connecting, and verify telemetry before issuing commands.

### Installation

1. Identify the controller model/firmware, connector orientation, supply and UART
	pinout. Do not assume a universal pinout or bypass stove safety systems.
2. Start from [examples/esp32-c3.yaml](examples/esp32-c3.yaml) or
	[examples/esp32-c6.yaml](examples/esp32-c6.yaml) for your ESPHome device configuration.
	For a different ESP32 board, change `esp32.board` to its ESPHome board ID and
	set `uart.tx_pin` and `uart.rx_pin` to suitable pins for that board. Keep the
	ESP-IDF framework and revalidate/recompile your configuration. The examples'
	GPIO assignments are starting points, not verified stove wiring diagrams or
	universal ESP32 pin choices. Review board pin
	conflicts and UART wiring before connecting.
3. Create a private `secrets.yaml` next to your configuration, with `wifi_ssid`,
	`wifi_password`, a unique fallback Wi-Fi `ap_password` and a unique per-device,
	base64-encoded 32-byte
	`device_encryption_key`. The values in [examples/secrets.yaml.example](examples/secrets.yaml.example)
	are placeholders; replace them before installing on a device.
4. Validate and compile the configuration in ESPHome, then install it on the ESP32.
	Add the device through Home Assistant's ESPHome integration using the same API
	encryption key. Verify communication
	health and readings before using any controls.

## Hub Configuration

The examples include telemetry and normal control entities. Normal controls need
no write-enable setting. Alarm reset requires `enable_reset: true`; advanced
commands require `enable_installer: true`. Both flags default to false and are
independent. Remove the obsolete `enable_writes` option from existing YAML;
normal controls are now available without it, including configurations that
previously used `enable_writes: false`.

Enabling installer support includes its controls and API actions in the firmware
and generates one **Installer Access** switch. Access always starts off after
reboot and can be switched on/off in Home Assistant without rebuilding. Normal
controls do not depend on this switch. With installer support disabled, advanced
controls, actions and their implementation are excluded from the firmware.

`firmware_profile` defaults `auto`, following the vendor app: start with the
selected level using `F001`..`F005`, independently of the firmware identifier.
Alternatives `"9200"`, `E670`, `E693` retain explicit legacy start overrides only.
`read_power_level` defaults true and polls `C500`; set it false only for
controllers that do not support this read. Disabling it leaves power writes
available but prevents power-level readback verification.
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
They send no stove commands and obey the same limits as YAML. To move
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

Initialization compatibility is a YAML-only stove setting: `duepi.init_command`,
default false. Enable it only when the controller requires the extra initialization sequence.
It adds a firmware query before other requests, increasing protocol traffic. If
initialization times out, the unsent command is cancelled rather than replayed.

Set `init_command: true` and rebuild to opt into this mode. Remove any previous
`platform: duepi`, `kind: initialization` switch from your YAML. This setting
only adds initialization before existing transactions. It does not reset alarms, reboot the stove or start ignition.
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

The example includes climate HEAT/OFF, whole-Celsius targets (10..25 by default)
and the power presets **Min**, **Low**, **Medium**, **High** and **Max**, mapped
to controller levels 1, 2, 3, 4 and 5 respectively.
Normal controls are available without an opt-in. First check communication and
readings before issuing commands.

Select power directly from the climate entity's preset selector in Home Assistant;
no extra YAML entity is needed. While observed Off, selecting a preset changes
only a local choice and sends no stove command. A later explicit HEAT request
starts at that level. In ignition/heating, a preset sends a level request without
changing the target. An ACK updates the selected preset as unverified; a fresh
`C500` reply supplies the controller-reported power level and can verify a
matching level write. A mismatch displays the reported level and records both
requested and reported values in the transaction result. Invalid feedback or a
15-second verification expiry leaves the write unverified; it is never retried.

Following the vendor app, **Power** is the selected level (1..5): the climate
presets provide its control, and the optional number provides the same control
numerically. The **Power** sensor (`power_level`) exposes its `C500` readback, not
measured output. **Auger Level** (`D400`) is the read-only pellet-feed stage,
not measured RPM or pellet flow. **Fan Speed**
(`D300`) is another read-only diagnostic, not the selected power.
Write confirmation appears in the transaction result. A local off-state choice takes
precedence over polling until Start is acknowledged or the stove leaves Off.
This includes starts from the stove's panel. Otherwise new `C500`
readings update the selection, including panel changes if the firmware reports
them. A matching report verifies the selected level, not delivered
heat output. Panel tracking on A060 still requires hardware validation.

Selection is unavailable during UART recovery, stale core telemetry and ordinary
shutdown. No selection is restored from flash or automatically replayed. Before
any read/selection, an explicit Start uses level 1; valid controller readback can
initialize the desired level first.

The optional controller-level number remains available for existing dashboards.
It shares the same selection and readback rules as the climate presets; it is not
an independent setting. Add it only if needed:

```yaml
number:
- platform: duepi
  duepi_id: stove
  kind: level
  name: Power
  mode: box
```

The optional reset button requires `enable_reset: true` on the hub.
Adding it while reset is disabled is a configuration
error. Reset is never automatic. Omit this entire entry when reset is disabled:

```yaml
button:
  - platform: duepi
    duepi_id: stove
    name: Reset Stove Alarm
```

Writes require fresh phase, room, alarm, firmware and board observations after
startup/recovery. Ignition requires observed Off and no alarm; level changes
on the wire require ignition or heating. Outside ordinary shutdown, Off remains available with a
reported alarm. There is no independent fan control.

Automatic mode follows the app's selected-level startup. Override
`firmware_profile` only when a legacy start command is required; a profile does not
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

The [C3 example](examples/esp32-c3.yaml#L77) and
[C6 example](examples/esp32-c6.yaml#L75) define standard telemetry and commented
opt-in diagnostics. Optional operating controls are described under
[Normal Controls](#normal-controls); installer entities are generated when
[installer support](#installer-and-custom-commands) is enabled.
Rename or omit individual YAML entities to suit your setup.
The binary `communication` sensor means fresh core stove replies, not merely a
connected Wi-Fi/API session. All entity platforms use `duepi_id: stove`.

Default Home Assistant Organization:

| Home Assistant section | Entities |
| --- | --- |
| Sensors | Room/target temperatures, Power, Auger Level, phase and alarm text |
| Controls | Climate, optional power-level number and alarm-reset button |
| Configuration | Target-temperature limits, installer access/parameter tools and Installer Result |
| Diagnostics | Flue/PCB temperatures, fan/exhaust speeds, runtime counters, communication, raw codes, identity, transaction result, target source, last response, Custom Response Data and the five optional readings below |

These are component schema defaults. For entities declared in your YAML,
override with `entity_category`; `entity_category: ""` restores normal placement.
Auto-generated installer entities have fixed categories. Diagnostic
categorization does not disable an entity.

Startup numeric telemetry is unknown; stale numeric telemetry becomes NaN/unknown.
Stale observed text fields are marked Stale.
Runtime counters expire after twice their polling interval; their units are
unconfirmed. **Fan Speed** uses `D300`; **Auger Level** uses `D400`.
Neither is measured heat output or selected-level verification;
`D400` can also contain ignition-stage values 6 and 7. Existing YAML keys
`d3000`/`d4000` remain supported. See the [protocol reference](docs/PROTOCOL.md#read-commands).
The numeric target and power-level sensors are observed readback only.
Omitting a standard sensor entity does not stop its register being polled.
Use `read_power_level: false` to disable `C500` polling. The five optional
diagnostic readings below are polled only when their entities are configured:

| YAML key | Platform | Reading |
| --- | --- | --- |
| `flue_extraction_speed` | `sensor` | Raw flue-extraction speed, separate from exhaust RPM |
| `extraction_sensor_level` | `sensor` | Raw extraction-sensor level |
| `flue_motor_correction` | `sensor` | Raw correction byte |
| `pellet_correction` | `sensor` | Raw correction byte |
| `cycle_time` | `text_sensor` | Two decimal fields, e.g. `10:11`; duration units unverified |

Uncomment the desired entries in either board example to enable them. These are
read-only sensors, not correction controls. Missing replies use normal read
timeout/recovery handling; leave unsupported readings out of the configuration.

Alarm descriptions follow the app's default or `RE8I` firmware dictionary;
unknown codes stay numeric and labels are not universal OEM safety diagnoses.
`last_response` records wrapping device uptime at the last accepted response,
formatted as days, hours, minutes and seconds, not UTC or time since that response.
Existing Home Assistant entity names/categories may
retain registry customizations; obsolete entities can remain unavailable after
firmware updates until removed from the entity registry.

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
consider the reported phase, numeric freshness and communication sensor; OFF
alone is not proof of a stopped stove.

Transactions distinguish queued, sent, acknowledged/unverified, verified,
rejected, cancelled, timed out and indeterminate. Target/Off/ignition and level require
matching readback for verification; level uses `C500`, not `D300`/`D400`. Reset remains unverified. Custom responses
are diagnostic hex data, never telemetry. All writes, including apparently read-only
custom commands, are **never retried**. A missing ACK does not prove a command failed.
See [State and Authorization](docs/ARCHITECTURE.md#state-and-authorization) for
readback rules and response-correlation limits.

### Read First, Then Test Power

1. Deploy the examples' `power_level` sensor.
	Observe it without changing any controls. Reads never send desired settings.
2. Compare panel-selected levels with **Power** (`C500`). Keep
	**Auger Level** (`D400`) separate; modulation and ignition stages can differ.
3. When ready, use the existing climate presets or optional Power
	number. While Off this only selects the next startup level; during
	ignition/heating it sends `F00n`. Check the transaction result and `C500`
	rather than treating an ACK as success.
4. Do not write fan/feed telemetry registers or installer parameters based merely
	on readable values. Only documented consumer commands have semantic controls.

## Installer and Custom Commands

Set `enable_writes: true` and `enable_installer: true` under `duepi:`, then
rebuild and install. The installer controls appear under Configuration in Home
Assistant. All installer and custom operations require **Installer Access** on
and fresh stove readings. Access is off after every reboot.

### Read or Write a Parameter

1. Turn on **Installer Access**.
2. Enter the documented serial address (0..255) in
	**Installer Input: Parameter Number**, then press **Installer Operation: Read**.
	The raw value appears in **Installer Result**.
3. To change it, first record the original value and ensure the stove is off.
	Enter the raw value (0..255) in **Installer Input: Value to Write**, then press
	**Installer Operation: Write**.
4. Check **Installer Result** for the readback outcome. Wait for each operation
	to finish before starting another; stop on failure or timeout.
5. Turn off **Installer Access** when finished.

Changing an input alone sends nothing. The value input starts at zero, which is
not a reading or recommended setting. Printed PR numbers are not verified serial
addresses; use controller-specific documentation for addresses, units and ranges.

A matching readback confirms only the returned byte, not that the setting is
appropriate. Failed readback leaves the write unverified; writes are not retried
automatically. Turning access off cannot undo an already transmitted write and
cancels pending installer work, including readback.

### Automation and Custom Commands

| YAML action | Fields besides `id: stove` | Additional requirement |
| --- | --- | --- |
| `duepi.installer_read` | `parameter` integer 0..255 | None |
| `duepi.installer_write` | `parameter`, `value` integers 0..255 | Stove observed off |
| `duepi.send_command` | `command` four hex characters | None |
| `duepi.send_raw` | `command` six uppercase alphanumeric characters | None |

Fields support ESPHome lambdas. With `api:` configured and hub `id: stove`, the
native API actions are `stove_installer_read(parameter)`,
`stove_installer_write(parameter,value)`, `stove_send_command(command)` and
`stove_send_raw(payload)`. Home Assistant prefixes these with the node name.

The hub adds framing and checksum to custom commands. **Custom commands bypass
normal firmware, alarm, phase and parameter checks, including the off-state
requirement.** Ordinary-shutdown and pending-Off protections still apply.
Restrict API access: these interfaces are not limited to safe reads.

**Custom Response Data** shows `Pending`, a hexadecimal reply, or `Unavailable`
on rejection, cancellation or timeout. A reply does not verify physical operation.
See [State and Authorization](docs/ARCHITECTURE.md#state-and-authorization) for
internal authorization and recovery behavior.

## Logging

All component messages use the `duepi` tag. The example defaults to `INFO`, with
serial logging disabled (`baud_rate: 0`) so logs cannot corrupt the stove UART.
Use ESPHome's network logs for the device.

Detected firmware is logged at `INFO`, for example `Controller firmware: A060`,
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

The examples load published code from GitHub `main`. To use unpublished workspace
changes, point `external_components` at the local `components` directory instead.
`refresh: 0s` refreshes the remote source; it does not publish local edits.

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

## Todo

In priority order:

- [ ] Fix remote power-level changes not applying; compare panel and remote changes
    against selected power level
- [ ] Fix installer parameter writes and verify whether reads are also affected,
	including readback verification.
- [ ] Investigate the TXB0104 logic-level shifter blocking UART signals; evaluate
	a [TXU0202](https://www.ti.com/product/TXU0202) as an alternative for 5 V/3.3 V UART.
- [ ] Complete the protocol reference with full alarm and phase tables,
	numeric-code explanations, known but unimplemented reads and newer-app findings.
- [ ] Verify alarm and input meanings for identified controllers and firmware,
	including the observed input value `0x0800`.
- [ ] Confirm diagnostic units, correction signedness, cycle-time interpretation
	and runtime-counter units.
- [ ] Establish installer parameter mappings with documented addresses, scaling,
	ranges and firmware applicability; neither analyzed app provides a catalogue.
- [ ] Record verified controller capabilities, including supported reads and
	demonstrated firmware differences.

Lower priority:

- [ ] Evaluate the newer app's local standby-power preference and establish
	resume behavior before considering equivalent functionality.

## Thanks

A huge thanks to [aceindy](https://github.com/aceindy/Duepi_EVO) and
[mvroosmalen](https://github.com/mvroosmalen1970/ESPHome-Duepi) for sharing their
Duepi protocol work and implementations with the community. Aceindy's Duepi_EVO
inspired mvroosmalen's ESPHome-Duepi, which in turn inspired this project.

## License

This repository is licensed under the [MIT License](LICENSE).
