import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from . import DuepiHub, duepi_ns

DuepiPrivilegeSwitch = duepi_ns.class_("DuepiPrivilegeSwitch", switch.Switch)
CONFIG_SCHEMA = switch.switch_schema(
    DuepiPrivilegeSwitch, default_restore_mode="ALWAYS_OFF", block_inverted=True,
).extend(
    {
        cv.GenerateID("duepi_id"): cv.use_id(DuepiHub),
        cv.Required("kind"): cv.one_of("initialization"),
    }
)


async def to_code(config):
    entity = await switch.new_switch(config)
    hub = await cg.get_variable(config["duepi_id"])
    cg.add(entity.set_parent(hub))
    cg.add(entity.set_initialization(True))
    cg.add(hub.set_privilege_switch(2, entity))