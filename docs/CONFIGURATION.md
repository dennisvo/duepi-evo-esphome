# ESPHome Configuration

Current configuration options and operating instructions for the Duepi component.
Start from [the C3 example](../examples/esp32-c3.yaml) or
[the C6 example](../examples/esp32-c6.yaml). For wiring, connector orientation and
electrical precautions, use the [hardware reference](HARDWARE.md).

## Contents

- [Device Setup](#device-setup)
- [Hub Options](#hub-options)
- [Timing Options](#timing-options)
- [Normal Controls](#normal-controls)
- [Entities](#entities)
- [Standby Mode](#standby-mode)
- [Installer Mode](#installer-mode)
- [TCP Emulator](#tcp-emulator)
- [Automation and Custom Commands](#automation-and-custom-commands)
- [Logging](#logging)

## Device Setup

Choose an ESPHome-supported ESP32 board with the ESP-IDF framework. For a board
other than the examples, change `esp32.board` and choose suitable `uart.tx_pin`
and `uart.rx_pin` values. Check pin conflicts and validate/recompile the result.
The example GPIO assignments are not universal stove wiring diagrams.

The UART must use 115200 baud, 8 data bits, no parity and 1 stop bit. Keep serial
logging disabled with `logger.baud_rate: 0`; use network logs instead. Each ESP
connects to one stove, with one `duepi:` hub.

Create a private `secrets.yaml` next to the device configuration containing
`wifi_ssid`, `wifi_password`, a unique fallback Wi-Fi `ap_password` and a unique
per-device base64-encoded 32-byte `device_encryption_key`. The
[secrets example](../examples/secrets.yaml.example) contains placeholders only.
The examples use the same key for the encrypted native API and OTA updates.

The examples load published component code from GitHub `main`. For unpublished
workspace changes, point `external_components` at the local `components`
directory. `refresh: 0s` refreshes a remote source; it does not publish local edits.

## Hub Options

All options below belong under `duepi:`. Standard ESPHome component/UART options
also apply, including `id` and `uart_id` when selecting a UART explicitly.
Normal climate and power controls are available without an opt-in.

| Option | Default | Purpose |
| --- | --- | --- |
| `id` | Generated | Hub identifier; the examples use `stove` |
| `enable_reset` | `false` | Allow the optional alarm-reset button |
| `enable_installer` | `false` | Include installer tools, custom commands and their API actions |
| `firmware_profile` | `auto` | Startup command selection: `auto`, `"9200"`, `E670` or `E693` |
| `read_power_level` | `true` | Poll reported power with `C500` |
| `min_temperature` | `10` | Minimum selectable/commanded whole-Celsius target, within 10..35 |
| `max_temperature` | `25` | Maximum selectable/commanded whole-Celsius target, within 10..35 |
| `init_command` | `false` | Send an initialization firmware query before other requests |
| `installer_parameter_min` | `0` | Lowest permitted serial parameter address, within 0..255 |
| `installer_parameter_max` | `255` | Highest permitted serial parameter address, within 0..255 |
| `standby_mode` | Not configured | Optional exact-address standby switch; see [Standby Mode](#standby-mode) |
| `tcp_emulator` | Not configured | Optional raw UART bridge controlled by a Home Assistant configuration switch |

### Temperature Limits

```yaml
duepi:
  id: stove
  min_temperature: 10
  max_temperature: 25
```

The minimum must be less than the maximum. These limits apply to target commands,
including queued commands; the stove may enforce additional limits. They are
integration settings, not installer parameters. Changing them sends no stove
command and does not change the current target. Valid reported targets remain
visible even outside the configured range.

The examples expose **Minimum Target Temperature** and **Maximum Target
Temperature** as Configuration number entities. Accepted changes apply
immediately and are saved for reboot restoration, taking precedence over YAML
defaults. Allow pending ESPHome preference writes to finish before removing power.
To move both limits past an existing boundary, expand the range first. Rejected
changes preserve both limits. If Home Assistant still shows the old selectable
range, reload its ESPHome integration; command enforcement already uses the new
limits. See [state and authorization](ARCHITECTURE.md#state-and-authorization).

### Startup Profile and Power Feedback

`firmware_profile: auto` starts with fresh reported power or level 1 if that is
unavailable, using `F001`..`F005` independently of the firmware identifier. Use
an explicit profile only when the controller requires a legacy startup command;
see the [command mappings](PROTOCOL.md#write-commands). A profile does not establish
compatibility or change telemetry decoding or installer parameter meanings.

Set `read_power_level: false` only for controllers without `C500` support. This
leaves power writes available, but removes power-level readback verification and
leaves power controls unknown without fresh feedback.

### Initialization Compatibility

Enable `init_command: true` only when the controller needs this extra sequence.
It is a YAML-only setting and requires rebuilding. It adds a firmware query before
other requests, increasing traffic. An initialization timeout cancels the unsent
request rather than replaying it. It does not reset alarms, reboot the stove or
start ignition. See [queue and recovery](ARCHITECTURE.md#queue-and-recovery).

## TCP Emulator

For vendor-app diagnostics, add this block to the existing hub configuration:

```yaml
duepi:
  tcp_emulator:
    name: TCP Emulator
    port: 2000
```

Omitting the block excludes the bridge. `name` and `port` are optional; the defaults
are shown above. Ports must be 1..65535. The configuration switch always starts
off after reboot; inversion and restoring an on-state are not supported.
Keep `logger.baud_rate: 0` (required when this feature is configured).

Turn **TCP Emulator** on in Home Assistant, wait for the network log message
`UART handover complete: TCP emulator ready`, then point the vendor app's local
connection at the ESP's LAN IP and configured TCP port. This does not emulate
cloud services, device discovery or authentication, and app/controller compatibility
still needs hardware testing. No original Wi-Fi module is needed for raw forwarding.

The switch gives the app exclusive UART ownership, including when no app is
connected. Normal polling, initialization queries, standby polling and all
component stove commands are blocked. Observed telemetry becomes unavailable;
the last target may remain displayed as an explicitly unverified retained value.
Wi-Fi, the native API and OTA remain available. Turn the switch off to close
the sockets and resume fresh synchronization; pending commands are discarded,
not replayed. Installer Access is also turned off and must be explicitly re-enabled.

Each switch transition drains UART input for at least `response_timeout` and
waits for `quiet_time` without incoming bytes. An in-flight command may already
have reached the stove and cannot be undone. This quarantine is only for changing
UART owners: app traffic gets no added protocol delays, initialization, retries,
checksum processing or rewriting. One client is accepted; additional clients are
closed. Per-loop work and buffers are bounded; overflow, I/O errors or three
seconds of blocked TCP output disconnect the client and discard buffered output.
Disconnected-session buffers are never sent to another client. TCP read boundaries
and logged chunks are not protocol frame boundaries.

**Security:** this is an unauthenticated, unencrypted raw TCP endpoint that bypasses
the component's stove-command protections, including installer opt-ins and limits.
Use only on a trusted LAN. Do not expose or forward this port to the Internet.
The encrypted native API does not protect the raw bridge.

For timestamped directional hex capture, set global `logger.level: VERBOSE` and
retain `logger.baud_rate: 0`. Capture network logs for `duepi.tcp_emulator` while
making one controlled app change. A per-tag VERBOSE setting cannot override a
lower global compile-time log level. Normal INFO logs contain connection and
handover events, not raw byte dumps.

## Timing Options

| Option | Default | Accepted range | Purpose |
| --- | --- | --- | --- |
| `update_interval` | 10s | 2s..1h | Start the regular polling cycle |
| `runtime_interval` | 1h | 1min..24h | Poll lifetime/service counters |
| `response_timeout` | 800ms | 100ms..5s | Wait for a transaction response |
| `inter_transaction_delay` | 100ms | 20ms..2s | Space transactions |
| `quiet_time` | 250ms | 100ms..10s | Quiet recovery period |
| `stale_after` | 45s | 5s..2h | Regular-reading freshness lifetime; must exceed `update_interval` |
| `arming_timeout` | 30s | 1s..120s | Lifetime of internal one-shot advanced-operation authorization |

Intervals use monotonic device time, not HA/SNTP. Allow enough freshness margin
for a full poll cycle and missed optional replies. Runtime counters use twice
`runtime_interval` as their freshness lifetime. The advanced-operation timeout
does not add a separate manual arming step. See
[communication and outcomes](BEHAVIOR.md#communication-and-outcomes).

## Normal Controls

The examples include climate HEAT/OFF, whole-Celsius targets and the presets
**Min**, **Low**, **Medium**, **High** and **Max**, mapped to power levels 1..5.
First verify Stove Communication and the reported readings.

Select power from the climate preset selector; no extra entity is needed.
Power changes while Off are rejected without sending or storing a choice.
Press HEAT, wait for fresh ignition readings, then select power. Presets and the
optional power number follow fresh readback, not a requested value or ACK.
**Power** is distinct from auger and fan speeds. There is no independent auger
or fan control. Check Transaction Result for command outcomes.

For a separate power number, add:

```yaml
number:
  - platform: duepi
    duepi_id: stove
    kind: level
    name: Power
    mode: box
```

It shares the climate preset's selection and readback rules. The other supported
number kinds, `min_temperature` and `max_temperature`, expose the integration's
temperature limits; both are included in the board examples.

To enable alarm reset, set `enable_reset: true` on the hub and add:

```yaml
button:
  - platform: duepi
    duepi_id: stove
    name: Reset Stove Alarm
```

The button is a configuration error while reset is disabled. Reset is never
automatic. Writes require fresh synchronized readings and phase-specific
permission. Boot and reconnection do not automatically send desired settings;
writes are never automatically retried. Off is not an emergency-stop guarantee.
Use the stove's own controls and safety procedures. See
[operating rules](BEHAVIOR.md#operating-states-and-commands),
[power behavior](BEHAVIOR.md#power-and-actuators) and
[temperature targets](BEHAVIOR.md#temperature-targets).

## Entities

Declare entities using `platform: duepi` and `duepi_id: stove`, as in the board
examples. Rename or omit individual entities as needed. The platforms accept
their standard ESPHome entity options.

| Platform | Supported keys/kinds |
| --- | --- |
| `sensor` | `room_temperature`, `target_temperature`, `power_level`, `d3000` (fan), `d4000` (auger), `flue_temperature`, `exhaust_speed`, `pcb_temperature`, `phase_code`, `alarm_code`, `inputs_code`, `total_runtime_counter`, `service_runtime_counter`, `flue_extraction_speed`, `extraction_sensor_level`, `flue_motor_correction`, `pellet_correction` |
| `text_sensor` | `phase`, `alarm`, `firmware`, `board_type`, `transaction`, `target_source`, `last_response`, `cycle_time`, `installer`, `custom_response` |
| `binary_sensor` | `communication` |
| `climate` | Climate entity with target and power presets |
| `number` | `kind: level`, `kind: min_temperature`, `kind: max_temperature` |
| `button` | Alarm reset, requiring `enable_reset: true` |

Installer mode generates its access switch, inputs, buttons and result entities;
do not add duplicate installer entities merely to enable the mode. Standby is
configured on the hub, not through a separate switch platform.

### Home Assistant Organization

| Section | Entities |
| --- | --- |
| Sensors | Room/target temperatures, Power, Auger Level, phase and alarm text |
| Controls | Climate, optional power number and reset button |
| Configuration | Target-temperature limits, standby and installer tools/access |
| Diagnostics | Flue/PCB temperatures, fan/exhaust readings, optional diagnostics, counters, communication, raw codes, identity and outcome/provenance entities |

These are schema defaults. Set `entity_category` on declared entities to override
placement; `entity_category: ""` restores normal placement. Auto-generated
installer entities have fixed categories. Diagnostic categorization does not
disable an entity or exclude its history. Home Assistant can retain registry
customizations and obsolete unavailable entities after firmware changes.

### Polling and Optional Diagnostics

Omitting a standard sensor does not stop its register being polled. The exception
is power feedback, explicitly controlled by `read_power_level`. These five
optional reads are polled only when their entities are configured:

| YAML key | Platform | Reading |
| --- | --- | --- |
| `flue_extraction_speed` | `sensor` | Raw extraction speed, separate from exhaust RPM |
| `extraction_sensor_level` | `sensor` | Raw extraction-sensor level |
| `flue_motor_correction` | `sensor` | Raw correction byte |
| `pellet_correction` | `sensor` | Raw correction byte |
| `cycle_time` | `text_sensor` | Active countdown as minutes:seconds, e.g. `10:11`; during heating, time until periodic cleaning |

Enable the desired entries in the examples. These are read-only sensors, not
correction controls. Missing replies use normal timeout/recovery handling;
leave unsupported readings out. See [read commands](PROTOCOL.md#read-commands)
for decoding. Alarm descriptions follow the default or `RE8I` dictionary and
are not universal OEM safety diagnoses.

### Health and Outcomes

**Stove Communication** means fresh phase, room and alarm readings, not merely a
Wi-Fi/API connection. Startup and stale telemetry can be unknown. Climate may
retain an unverified target; **Target Temperature Source** explains that display.
Climate OFF alone is not proof of a stopped stove.

**Transaction Result** reports normal command outcomes. An ACK is not verification;
a missing ACK does not prove failure. Successful background reads update values
and freshness without changing this entity. Read timeouts can appear before the
first numbered normal or advanced request; later background timeouts do not
overwrite command results.
Individual transactions remain available in DEBUG logs. Parameter operations use
Installer Result; custom outcomes use Transaction Result, with reply data in
Custom Response Data. See
[communication and outcomes](BEHAVIOR.md#communication-and-outcomes).

`last_response` is an opt-in debugging diagnostic, commented out in both examples.
It records wrapping device uptime at the last accepted response, formatted as
days, hours, minutes and seconds, not UTC or response age. It changes on every
accepted response and is not needed for normal use; use Stove Communication.

## Standby Mode

For a controller with a confirmed standby parameter, add under `duepi:`:

```yaml
standby_mode: {parameter: 28, name: Standby Mode}
```

`parameter` is required and must be within 0..255 and the configured installer
address bounds. `name` defaults to Standby Mode; standard ESPHome switch options
also apply, subject to disabled restore and no inversion. Serial address 28 with
raw values 0 (off) and 1 (on) was confirmed on the tested stove, not universally.
See the [model-specific parameter reference](PARAMETERS.md).

This switch works without installer mode or Installer Access. It periodically
reads the parameter, displays the last confirmed value and sends no startup
write. Changes require fresh synchronized readings and a known phase other than
ignition. The stove manages standby stopping/restarting. A retained switch value
is not proof of current communication. See [standby behavior](BEHAVIOR.md#standby-mode).

## Installer Mode

To enable installer mode, set `enable_installer: true` under `duepi:`, rebuild and
install. Its controls appear under Configuration in Home Assistant; Installer
Result and Custom Response Data appear under Diagnostics. All general installer
and custom operations require **Installer Access** on and fresh readings. Access
starts off after every reboot. Normal controls do not depend on this switch.
With installer mode disabled, general installer/custom tools and API actions are
not included; an independently configured standby switch remains available.

Restrict serial parameter addresses for the controller:

```yaml
duepi:
  id: stove
  enable_installer: true
  installer_parameter_min: 1
  installer_parameter_max: 28
```

The bounds are inclusive, default to 0..255 and require minimum <= maximum.
Both examples use 1..28. The selector follows the bounds, while write values
remain raw bytes 0..255. Out-of-range typed, API, automation, standby and raw
`L`/`S` parameter operations are rejected before transmission. Rebuild/install
after changing the bounds. Bounds restrict addresses, not safe values or unknown
custom-command effects. Printed PR numbers are not automatically serial addresses.
Consult the [parameter reference](PARAMETERS.md) and controller documentation.

### Read or Write a Parameter

1. Turn on **Installer Access**.
2. Enter the confirmed serial address in **Installer Input: Parameter Number**
   and press **Installer Operation: Read**. Check **Installer Result**.
3. Before changing anything, record the original raw value. Prefer a stopped
   stove and never change parameters during ignition. Enter a raw byte in
   **Installer Input: Value to Write**, then press **Installer Operation: Write**.
4. Wait for the readback outcome in **Installer Result** before another operation.
   Stop on failure or timeout.
5. Turn off **Installer Access** when finished.

Changing an input alone sends nothing. The value input starts at zero; it is not
a reading or recommended setting. The component blocks writes during ignition,
unknown phases or stale/unsynchronized readings, but permits other known phases.

**Manufacturer precautions:** never change installer parameters during startup: heated,
unignited pellets can release gas that later ignites and causes overpressure.
Decide the changes before entering programming mode, prefer a non-burning stove
and minimize time in programming mode if changing settings while burning.
Serial access is not established to have the same effects as the panel menu.
Phase guards use polling, not instantaneous detection.

A matching readback confirms only the returned byte, not an appropriate setting.
Failed readback leaves the write unverified; writes are not retried. Disabling
access cannot undo a transmitted write and cancels pending installer work,
including readback. See [installer behavior](BEHAVIOR.md#installer-parameters).

## Automation and Custom Commands

Installer mode and Installer Access are required for all actions below.

| YAML action | Fields besides `id: stove` | Additional requirement |
| --- | --- | --- |
| `duepi.installer_read` | `parameter` integer 0..255 | Configured address bounds |
| `duepi.installer_write` | `parameter`, `value` integers 0..255 | Address bounds and known phase outside ignition |
| `duepi.send_command` | `command` four hex characters | Custom-command safeguards |
| `duepi.send_raw` | `command` six uppercase alphanumeric characters | Custom-command safeguards; address bounds for `L`/`S` |

Fields support ESPHome lambdas. With `api:` and hub `id: stove`, native API actions
are `stove_installer_read(parameter)`, `stove_installer_write(parameter,value)`,
`stove_send_command(command)` and `stove_send_raw(payload)`. Home Assistant prefixes
them with the node name.

The hub adds framing/checksum. **Custom commands bypass normal firmware-profile,
alarm and command-specific checks, including known-phase checks except for raw
`S` writes.** Fresh synchronized core, firmware and board readings are still
required. Ordinary-shutdown and pending-Off protections apply; raw `S` writes
retain the ignition/known-phase guard. These interfaces are not restricted to safe
reads. Restrict API access.

**Custom Response Data** shows Pending, a hexadecimal reply or Unavailable after
rejection/cancellation/timeout. A reply is not physical verification. See
[state and authorization](ARCHITECTURE.md#state-and-authorization) for safeguards.

## Logging

Messages use the `duepi` tag. The examples default to INFO and disable serial
logging (`baud_rate: 0`) to avoid corrupting the stove UART. Use network logs.

| Level | Content |
| --- | --- |
| `INFO` | Startup, identity, phase/alarm/health changes, operator write outcomes and configuration changes; routine successful polling is quiet |
| `WARN` | Lost communication, exhausted reads, rejected/indeterminate writes, alarms, readback mismatches and failed preference saves |
| `DEBUG` | Named transactions/tokens, arguments, queue/gate details, retries, unavailable feedback, initialization/recovery and parser summaries |
| `VERBOSE` | Bounded transmitted frames and accepted response payloads |

Read-timeout and identical consecutive rejection warnings are rate-limited to
30 seconds. Firmware is logged at INFO on first observation/change and is also
available as an entity. Installer/custom data can appear in diagnostic logs;
review before sharing. Logs are not a durable audit record or proof of action.

For debugging, change the global logger level and rebuild:

```yaml
logger:
  baud_rate: 0
  level: DEBUG
```

Use VERBOSE temporarily for frame diagnostics. The global level is a compile-time
ceiling: `logger.logs.duepi: DEBUG` cannot recover messages omitted by global INFO.
Raise the global level first, then lower noisy tags in `logger.logs`. Detailed
startup configuration also needs DEBUG or above in the tested ESPHome versions.