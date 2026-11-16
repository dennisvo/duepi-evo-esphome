import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from . import DuepiHub, duepi_ns

DuepiReset = duepi_ns.class_("DuepiReset", button.Button)
CONFIG_SCHEMA = button.button_schema(DuepiReset).extend(
    {cv.GenerateID("duepi_id"): cv.use_id(DuepiHub)}
)


async def to_code(config):
    entity = await button.new_button(config)
    hub = await cg.get_variable(config["duepi_id"])
    cg.add(entity.set_parent(hub))