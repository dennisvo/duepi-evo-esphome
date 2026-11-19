import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import final_validate as fv
from esphome.components import socket, switch as esphome_switch, uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
def AUTO_LOAD(config):
    components = ["sensor", "text_sensor", "binary_sensor", "climate", "number", "button", "switch"]
    return components + (["socket", "network"] if "tcp_emulator" in config else [])
MULTI_CONF = False

duepi_ns = cg.esphome_ns.namespace("duepi")
DuepiHub = duepi_ns.class_("DuepiHub", cg.Component, uart.UARTDevice)
FirmwareProfile = duepi_ns.enum("FirmwareProfile", is_class=True)
DuepiStandbySwitch = duepi_ns.class_("DuepiStandbySwitch", esphome_switch.Switch)
DuepiTCPEmulatorSwitch = duepi_ns.class_("DuepiTCPEmulatorSwitch", esphome_switch.Switch)
TCP_EMULATOR_SCHEMA = cv.All(esphome_switch.switch_schema(
    DuepiTCPEmulatorSwitch, default_restore_mode="DISABLED", block_inverted=True, entity_category="config"
).extend({
    cv.Optional("name", default="TCP Emulator"): cv.string_strict,
    cv.Optional("port", default=2000): cv.All(cv.port, cv.Range(min=1)),
    cv.Optional("restore_mode", default="DISABLED"): cv.enum(
        {"DISABLED": esphome_switch.RESTORE_MODES["DISABLED"]}, upper=True
    ),
}), socket.consume_sockets(2, "duepi.tcp_emulator"),
    socket.consume_sockets(1, "duepi.tcp_emulator", socket.SocketType.TCP_LISTEN))
STANDBY_SCHEMA = esphome_switch.switch_schema(
    DuepiStandbySwitch, default_restore_mode="DISABLED", block_inverted=True, entity_category="config"
).extend({
    cv.Required("parameter"): cv.int_range(min=0, max=255),
    cv.Optional("name", default="Standby Mode"): cv.string_strict,
})
PROFILES = {
    "auto": FirmwareProfile.AUTO,
    "9200": FirmwareProfile.FW_9200,
    "E670": FirmwareProfile.FW_E670,
    "E693": FirmwareProfile.FW_E693,
}

TIMINGS = {
    "update_interval": ("10s", 2000, 3600000),
    "runtime_interval": ("1h", 60000, 86400000),
    "response_timeout": ("800ms", 100, 5000),
    "inter_transaction_delay": ("100ms", 20, 2000),
    "quiet_time": ("250ms", 100, 10000),
    "stale_after": ("45s", 5000, 7200000),
    "arming_timeout": ("30s", 1000, 120000),
}


def validate_timings(config):
    if config["min_temperature"] >= config["max_temperature"]:
        raise cv.Invalid("min_temperature must be less than max_temperature")
    if config["installer_parameter_min"] > config["installer_parameter_max"]:
        raise cv.Invalid("installer_parameter_min must not exceed installer_parameter_max")
    if "standby_mode" in config and not (
        config["installer_parameter_min"] <= config["standby_mode"]["parameter"] <= config["installer_parameter_max"]
    ):
        raise cv.Invalid("standby_mode.parameter must be within the configured installer parameter range")
    if config["stale_after"] <= config["update_interval"]:
        raise cv.Invalid("stale_after must exceed update_interval")
    if config["enable_installer"]:
        from .installer import entity_config

        config["_installer_entities"] = entity_config(str(config[CONF_ID]))
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(DuepiHub),
            cv.Optional("enable_reset", default=False): cv.boolean,
            cv.Optional("init_command", default=False): cv.boolean,
            cv.Optional("read_power_level", default=True): cv.boolean,
            cv.Optional("min_temperature", default=10): cv.int_range(min=10, max=35),
            cv.Optional("max_temperature", default=25): cv.int_range(min=10, max=35),
            cv.Optional("firmware_profile", default="auto"): cv.enum(PROFILES),
            cv.Optional("enable_installer", default=False): cv.boolean,
            cv.Optional("installer_parameter_min", default=0): cv.int_range(min=0, max=255),
            cv.Optional("installer_parameter_max", default=255): cv.int_range(min=0, max=255),
            cv.Optional("standby_mode"): STANDBY_SCHEMA,
            cv.Optional("tcp_emulator"): TCP_EMULATOR_SCHEMA,
            cv.Optional("enable_custom_commands"): cv.invalid("Use enable_installer to include all advanced capabilities"),
            cv.Optional("enable_raw_commands"): cv.invalid("Use enable_installer to include all advanced capabilities"),
            **{
                cv.Optional(key, default=default): cv.All(
                    cv.positive_time_period_milliseconds,
                    cv.Range(min=cv.TimePeriod(milliseconds=minimum), max=cv.TimePeriod(milliseconds=maximum)),
                )
                for key, (default, minimum, maximum) in TIMINGS.items()
            },
        }
    ).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA),
    validate_timings,
)

def validate_tcp_emulator_logger(config):
    if "tcp_emulator" in config and fv.full_config.get().get("logger", {}).get("baud_rate", 0) != 0:
        raise cv.Invalid("tcp_emulator requires logger.baud_rate: 0; use network logs, not the stove UART")
    return config


FINAL_VALIDATE_SCHEMA = cv.All(uart.final_validate_device_schema(
    "duepi", baud_rate=115200, require_tx=True, require_rx=True,
    data_bits=8, parity="NONE", stop_bits=1,
), validate_tcp_emulator_logger)

from .automation import ACTIONS as DUEPI_ACTIONS


async def to_code(config):
    hub = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(hub, config)
    await uart.register_uart_device(hub, config)
    cg.add(hub.set_enable_reset(config["enable_reset"]))
    cg.add(hub.set_init_command(config["init_command"]))
    cg.add(hub.set_read_power_level(config["read_power_level"]))
    cg.add(hub.set_min_temperature(config["min_temperature"]))
    cg.add(hub.set_max_temperature(config["max_temperature"]))
    cg.add(hub.set_firmware_profile(config["firmware_profile"]))
    if "tcp_emulator" in config:
        cg.add_define("USE_DUEPI_TCP_EMULATOR")
        emulator = await esphome_switch.new_switch(config["tcp_emulator"])
        cg.add(emulator.set_parent(hub))
        cg.add(hub.set_tcp_emulator(emulator, config["tcp_emulator"]["port"]))
    if config["enable_installer"] or "standby_mode" in config:
        cg.add_define("USE_DUEPI_INSTALLER")
        cg.add(hub.set_installer_parameter_range(config["installer_parameter_min"], config["installer_parameter_max"]))
    if "standby_mode" in config:
        standby = await esphome_switch.new_switch(config["standby_mode"])
        cg.add(standby.set_parent(hub))
        cg.add(hub.set_standby_mode(standby, config["standby_mode"]["parameter"]))
    if config["enable_installer"]:
        from .installer import to_code as installer_to_code

        cg.add(hub.set_enable_installer(True))
        cg.add(hub.set_enable_custom_commands(True))
        cg.add(hub.set_enable_raw_commands(True))
        await installer_to_code(
            config["_installer_entities"], hub, str(config[CONF_ID]),
            config["installer_parameter_min"], config["installer_parameter_max"],
        )
    for key in TIMINGS:
        if key == "arming_timeout" and not config["enable_installer"] and "standby_mode" not in config:
            continue
        cg.add(getattr(hub, f"set_{key}")(config[key]))
