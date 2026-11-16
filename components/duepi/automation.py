from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE
from . import DuepiHub, duepi_ns

PrivilegedAction = duepi_ns.class_("PrivilegedAction", automation.Action)
Operation = duepi_ns.enum("PrivilegedOperation", is_class=True)

ACTIONS = {
    "duepi.installer_read": ("INSTALLER_READ", {"parameter": cg.int_}),
    "duepi.installer_write": ("INSTALLER_WRITE", {"parameter": cg.int_, "value": cg.int_}),
    "duepi.send_command": ("COMMAND", {"command": cg.std_string}),
    "duepi.send_raw": ("RAW", {"command": cg.std_string}),
}


def register_action(name, operation, fields):
    schema = cv.maybe_simple_value(
        {
            cv.Required(CONF_ID): cv.use_id(DuepiHub),
            **{
                cv.Required(key): cv.templatable(cv.int_range(min=0, max=255) if kind is cg.int_ else cv.string_strict)
                for key, kind in fields.items()
            },
        },
        key=CONF_ID,
    )

    @automation.register_action(name, PrivilegedAction, schema, synchronous=True)
    async def to_code(config, action_id, template_arg, args):
        hub_config = CORE.config["duepi"]
        if hub_config[CONF_ID] != config[CONF_ID] or not hub_config["enable_installer"]:
            raise cv.Invalid(f"{name} requires enable_installer: true on its duepi hub")
        hub = await cg.get_variable(config[CONF_ID])
        action = cg.new_Pvariable(action_id, template_arg, hub)
        cg.add(action.set_operation(getattr(Operation, operation)))
        for key, kind in fields.items():
            value = await cg.templatable(config[key], args, kind)
            cg.add(getattr(action, f"set_{key}")(value))
        return action


for action_name, (operation_name, action_fields) in ACTIONS.items():
    register_action(action_name, operation_name, action_fields)