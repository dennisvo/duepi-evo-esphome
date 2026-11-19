import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, number, switch, text_sensor
from esphome.core import CORE
from esphome.const import CONF_ID
from . import duepi_ns
from .switch import DuepiPrivilegeSwitch

DuepiInstallerValue = duepi_ns.class_("DuepiInstallerValue", number.Number)
DuepiInstallerButton = duepi_ns.class_("DuepiInstallerButton", button.Button)
DuepiInstallerAPI = duepi_ns.class_("DuepiInstallerAPI", cg.Component)


def entity_config(hub_id):
    values = {
        "api": cv.Schema({cv.GenerateID(): cv.declare_id(DuepiInstallerAPI)}).extend(cv.COMPONENT_SCHEMA)(
            {"id": f"{hub_id}_installer_api"}
        )
    }
    for key, name in (
        ("parameter", "Installer Input: Parameter Number"),
        ("value", "Installer Input: Value to Write"),
    ):
        values[key] = number.number_schema(DuepiInstallerValue, entity_category="config")(
            {"id": f"{hub_id}_installer_{key}", "name": name, "mode": "BOX"}
        )
    values["access"] = switch.switch_schema(
        DuepiPrivilegeSwitch, default_restore_mode="ALWAYS_OFF", block_inverted=True, entity_category="config"
    )({"id": f"{hub_id}_installer_access", "name": "Installer Access"})
    for key, name in (("read", "Installer Operation: Read"), ("write", "Installer Operation: Write")):
        values[key] = button.button_schema(DuepiInstallerButton, entity_category="config")(
            {"id": f"{hub_id}_installer_{key}", "name": name}
        )
    for key, name, category in (
        ("result", "Installer Result", "diagnostic"),
        ("response", "Custom Response Data", "diagnostic"),
    ):
        values[key] = text_sensor.text_sensor_schema()(
            {"id": f"{hub_id}_installer_{key}", "name": name, "entity_category": category}
        )
    return values


async def to_code(config, hub, hub_id, parameter_min, parameter_max):
    for key in ("parameter", "value"):
        entity = await number.new_number(
            config[key], min_value=parameter_min if key == "parameter" else 0,
            max_value=parameter_max if key == "parameter" else 255, step=1,
        )
        cg.add(hub.set_installer_number(key == "value", entity))
    access = await switch.new_switch(config["access"])
    cg.add(access.set_parent(hub))
    cg.add(hub.set_privilege_switch(0, access))
    for operation, key in enumerate(("read", "write")):
        entity = await button.new_button(config[key])
        cg.add(entity.set_parent(hub))
        cg.add(entity.set_operation(operation))
    for index, key in ((5, "result"), (6, "response")):
        entity = await text_sensor.new_text_sensor(config[key])
        cg.add(hub.set_text_sensor(index, entity))
    if "api" in CORE.config:
        cg.add_define("USE_API_USER_DEFINED_ACTIONS")
        cg.add_define("USE_API_CUSTOM_SERVICES")
        api_id = CORE.config["api"][CONF_ID]
        await cg.get_variable(api_id)
        service = cg.new_Pvariable(config["api"][CONF_ID])
        await cg.register_component(service, config["api"])
        cg.add(service.set_parent(hub))
        cg.add(service.set_action_prefix(hub_id))