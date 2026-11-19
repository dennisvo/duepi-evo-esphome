# Stove Parameter Reference

Model-specific installer parameters and supplied reference values. These are not
universal Duepi defaults or recommended settings for another stove.

Distributors or installers may configure values differently for the installation,
including the pellets used, environment, flue arrangement and user preferences.
Check the documentation and existing settings for the actual stove before making
changes. Record the original values and consult a qualified installer for tuning.
Never change parameters during ignition; see the
[installer instructions and precautions](CONFIGURATION.md#installer-mode).

Parameter numbering and encoding are model-specific. Displayed menu values are
not necessarily raw bytes. Verify the model, parameter number, encoding and
valid range before using installer mode.

## Read Parameters from Home Assistant

Use the [Home Assistant parameter-read script](../examples/home-assistant-read-parameters.yaml)
to collect parameters sequentially. This is a Home Assistant script, not an
ESPHome device configuration. Its default range is **1 through 28**; both input
selectors and runtime validation restrict requests to that range. A smaller
subrange is allowed. Confirm the parameter map before using it on another model.

1. Enable [installer mode](CONFIGURATION.md#installer-mode) in ESPHome and install
	the firmware. The hub's configured parameter bounds must include the numbers
	you intend to read; both board examples use 1..28.
2. Create a Home Assistant script and use the example in its single-script YAML
	editor, then save it.
3. When calling the script, select this stove's **Installer Access** switch,
	**Installer Input: Parameter Number** number entity, **Installer Operation:
	Read** button and **Installer Result** sensor in the corresponding fields.
	Choose the first/last parameter and an optional snapshot name.
4. Turn on Installer Access and run the script. Do not start other installer
	operations while it is running. It waits for each fresh read result before
	continuing, pauses one second between parameters and stops on failure or
	timeout.
5. Read the results in the persistent Home Assistant notification and Home
	Assistant log (`duepi.parameter_scan`), then turn off Installer Access.

The script changes the parameter-number selector and presses only the Read
button; it does not write stove settings. Results are raw decimal bytes, not
automatically converted stove-menu values. Compare them with the model's table
below. It does not read cPELL/cASP corrections; enable their optional ESPHome
entities as described under [fine tuning](#manual-adjustment-and-fine-tuning).

## Artel Next Petit 6

Source: manual excerpts, manual checking between stove and read values from wifi
module, parameter list and pellet/exhaust-fan conversions. These are example values 
in the stove menu, not independently established factory defaults or a prescription 
for another installation.

**Reading the table:** Installer mode takes a decimal **Parameter Number** and a
separate decimal **Value to Write**. The **Parameter number in ESPHome** column
gives the number to enter (1 through 28 for this stove); the final column converts
the menu example to the value to write. Do not assume the same numbering on
other models. Pellet values use menu value multiplied by
ten; exhaust-fan values use `(rpm / 10) - 60`. Other numeric examples are shown
without scaling; verify their encoding on the actual controller before writing.
No hexadecimal entry is needed in installer mode.

| Parameter | Installer Parameter number in ESPHome | Description | Example value in stove menu | Decimal Installer value in ESPHome|
| --- | --- | --- | --- | --- |
| PR01 | 1 | Maximum startup-cycle time (minutes) | 16 | 16 |
| PR02 | 2 | Flame stabilization time in FIRE ON (minutes) | 04 | 4 |
| PR03 | 3 | Interval between cleaning/blow-out cycles (minutes) | 30 | 30 |
| PR04 | 4 | Pellet feed in LOAD WOOD | 1.8 | 18 |
| PR05 | 5 | Pellet feed in FIRE ON | 1.2 | 12 |
| PR06 | 6 | Pellet feed at power level 1 / Eco | 1.2 | 12 |
| PR07 | 7 | Pellet feed at power level 2 | 1.6 | 16 |
| PR08 | 8 | Pellet feed at power level 3 | 1.9 | 19 |
| PR09 | 9 | Pellet feed at power level 4 | 2.2 | 22 |
| PR10 | 10 | Pellet feed at power level 5 | 2.5 | 25 |
| PR11 | 11 | Alarm delay | 120 | 120 |
| PR12 | 12 | Cleaning/blow-out duration | 30 | 30 |
| PR13 | 13 | Minimum flue-gas temperature to proceed to FIRE ON (C) | 50 | 50 |
| PR14 | 14 | Maximum flue-gas temperature (C) | 240 | 240 |
| PR15 | 15 | Temperature at which the room fan starts (C) | 65 | 65 |
| PR16 | 16 | Exhaust-fan speed in LOAD WOOD (rpm) | 1800 | 120 |
| PR17 | 17 | Exhaust-fan speed in FIRE ON (rpm) | 1800 | 120 |
| PR18 | 18 | Exhaust-fan speed at power level 1 / Eco (rpm) | 1200 | 60 |
| PR19 | 19 | Exhaust-fan speed at power level 2 (rpm) | 1350 | 75 |
| PR20 | 20 | Exhaust-fan speed at power level 3 (rpm) | 1500 | 90 |
| PR21 | 21 | Exhaust-fan speed at power level 4 (rpm) | 1650 | 105 |
| PR22 | 22 | Exhaust-fan speed at power level 5 (rpm) | 1800 | 120 |
| PR23 | 23 | Room-fan setting at power level 1 / Eco | 12 | 12 |
| PR24 | 24 | Room-fan setting at power level 2 | 15 | 15 |
| PR25 | 25 | Room-fan setting at power level 3 | 18 | 18 |
| PR26 | 26 | Room-fan setting at power level 4 | 21 | 21 |
| PR27 | 27 | Room-fan setting at power level 5 | 20 | 20 |
| PR28 | 28 | Standby mode | OFF | 0 = OFF / 1 = ON |


### Exhaust-Fan Parameter Conversion

For this supplied parameter reference, PR16 through PR22 use:

```text
rpm = (raw byte + 60) * 10
raw byte = (rpm / 10) - 60
```

| Parameters | Supplied rpm | Corresponding raw byte |
| --- | --- | --- |
| PR16, PR17, PR22 | 1800 | 120 |
| PR18 | 1200 | 60 |
| PR19 | 1350 | 75 |
| PR20 | 1500 | 90 |
| PR21 | 1650 | 105 |

The offset is added **before** multiplying by ten. This conversion does not
establish serial addresses and must not be applied to PR23 through PR27, other
models or other fan readings without confirmation. In particular, live exhaust
speed telemetry (`EF00`) uses a different conversion, `raw * 10`, already handled
by the component; see the [protocol reference](PROTOCOL.md#read-commands).

### Standby

The example PR28 reference value is OFF. Serial address 28 with raw
values 0 (off) and 1 (on) was confirmed on the tested stove. This observation is
not a universal PR-to-serial mapping. See
[standby configuration](CONFIGURATION.md#standby-mode) before enabling the switch.

### Manual Adjustment and Fine Tuning

The stove manual distinguishes fine tuning from the individual PR
parameters used for coarse adjustment. Follow the stove manual and
installer guidance for access and adjustment; the button sequences are not
reproduced here. The availability of a menu setting is not a recommendation to
change it or proof that a serial write is safe.

| Manual setting | What it adjusts | Documented adjustment | Optional ESPHome reading |
| --- | --- | --- | --- |
| cPELL | Pellet feed across all power levels | -5..+5; each step changes feed by 5%, up to -25%..+25% | `pellet_correction` |
| cASP | Exhaust-fan speed across all power levels | -5..+5; each step changes speed by 5%, up to -25%..+25% | `flue_motor_correction` |
| cFAN | Room fan | Listed in section 19; range/scaling not specified in the supplied excerpt | No confirmed register yet |

cPELL and cASP can be read through the WiFi-module protocol and are exposed as
optional, read-only correction entities in the ESPHome configuration. Enable
`pellet_correction` and `flue_motor_correction` in the board example to poll them;
see [optional diagnostics](CONFIGURATION.md#polling-and-optional-diagnostics).
They expose raw correction bytes, not automatically decoded signed steps or
percentages. The signed-byte encoding remains unconfirmed. These entities do not
provide adjustment controls. The correct register for cFAN has not yet been found;
the existing Fan Speed reading is not a confirmed cFAN correction reading.

For coarse adjustment, section 20 describes PR01 through PR28. These are the parameters
that can be set in the installer mode. Changes are saved immediately, so record the existing
settings before entering the menu and plan changes in advance. Never adjust during startup; 
prefer a non-burning stove and minimize time in programming mode if changes are made while burning. 
The supplied manual warns that deviating from its recommended values on one's own initiative
voids the warranty; consult the applicable manual and supplier before tuning.

## Adding Another Stove

Add a separate model section with the source/manual edition, controller and
firmware where known, parameter descriptions, units and reference values.
Distinguish documented values from observed settings, and record serial addresses
and raw-byte conversions only when confirmed for that controller. Do not merge
similar model names or assume a common parameter map.