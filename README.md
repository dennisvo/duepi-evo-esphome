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
- [Features](#features)
- [Documentation](#documentation)
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
compiled for the **Seeed XIAO ESP32-C3/C6**, with ESPHome **2026.11.0** and ESP-IDF 
**5.5.6**; other ESP32 boards have not yet been build-tested. Controller
compatibility, wiring and electrical levels must be checked for each installation.
ESP8266 and vendor-app/TCP connections are not supported.
Each ESP device connects to one stove; configure one `duepi:` hub per device.

See [stove and integration behavior](docs/BEHAVIOR.md) for operating rules and
observations, [architecture](docs/ARCHITECTURE.md) for implementation, and the
[protocol reference](docs/PROTOCOL.md) for wire details.

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

1. Read the [hardware reference](docs/HARDWARE.md) before connecting anything.
   It covers controller identification, connector pinouts, wiring and electrical
   precautions; verify them for the actual installation.
2. Start from [examples/esp32-c3.yaml](examples/esp32-c3.yaml) or
   [examples/esp32-c6.yaml](examples/esp32-c6.yaml). Adapt the board, UART pins and
   private secrets using the [configuration guide](docs/CONFIGURATION.md#device-setup)
   and [secrets example](examples/secrets.yaml.example).
3. Validate, compile and install through ESPHome, then add the device using Home
   Assistant's ESPHome integration. Verify **Stove Communication** and readings
   before using controls. Do not bypass the stove's own safety systems.

## Features

- **Monitoring:** temperatures, operating phase, alarms, selected power, auger/fan
	readings and communication health, plus optional diagnostics.
- **Normal controls:** climate HEAT/OFF, target temperature and power presets.
	Command results distinguish acknowledgement from readback verification.
- **Standby:** an optional switch for a confirmed controller-specific parameter;
	the stove manages stopping and restarting.
- **Installer mode:** parameter reads/writes and custom commands. To enable
	installer mode, set `enable_installer: true`, rebuild and install, then turn on
	Installer Access. See the [installer guide](docs/CONFIGURATION.md#installer-mode)
	before using it. Alarm reset has its own separate opt-in.

Controls depend on fresh stove readings and operating-state permissions. Startup
and reconnection do not reapply desired settings, and writes are not automatically
retried. Use the stove's own safety controls; see
[operating behavior](docs/BEHAVIOR.md) for restrictions and limitations.

## Documentation

| Guide | What it covers |
| --- | --- |
| [Hardware](docs/HARDWARE.md) | Connector pinouts, wiring, electrical precautions and board references |
| [ESPHome configuration](docs/CONFIGURATION.md) | Current options/defaults, entities, controls, standby, installer usage, automation and logging |
| [Stove parameters](docs/PARAMETERS.md) | Model-specific parameter values and conversions, starting with the Artel Next Petit 6 |
| [Operating behavior](docs/BEHAVIOR.md) | Power/temperature behavior, phase permissions, freshness and command outcomes |
| [Protocol](docs/PROTOCOL.md) | Frames, commands, replies and telemetry decoding |
| [Architecture](docs/ARCHITECTURE.md) | Internal design, authorization, queue/recovery and development tooling |

## Development

Use Python 3.13 and [the pinned dependencies](requirements-dev.txt). See
[development details](docs/ARCHITECTURE.md#development) for test commands, build
variants and CI requirements, and [device setup](docs/CONFIGURATION.md#device-setup)
for loading unpublished local components. Validation binaries use public dummy
credentials and must not be deployed.

## Todo

In priority order:

- [ ] Fix remote power-level changes not applying; compare panel and remote changes
    against selected power level
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
- [ ] Investigate the TXB0104 logic-level shifter blocking UART signals; evaluate
	a [TXU0202](https://www.ti.com/product/TXU0202) as an alternative for 5 V/3.3 V UART.

Lower priority:

- [ ] Evaluate the newer app's local standby-power preference and establish
	resume behavior before considering equivalent functionality.
- [ ] Add support for an external temperature sensor to replace the inaccurate
	internal room temperature sensor.
- [ ] Add a timer option to turn the stove on and off at scheduled times.

## Thanks

A huge thanks to [aceindy](https://github.com/aceindy/Duepi_EVO) and
[mvroosmalen](https://github.com/mvroosmalen1970/ESPHome-Duepi) for sharing their
Duepi protocol work and implementations with the community. Aceindy's Duepi_EVO
inspired mvroosmalen's ESPHome-Duepi, which in turn inspired this project.

## License

This repository is licensed under the [MIT License](LICENSE).
