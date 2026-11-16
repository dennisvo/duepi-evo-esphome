import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from . import DuepiHub, duepi_ns

DuepiClimate = duepi_ns.class_("DuepiClimate", climate.Climate)
CONFIG_SCHEMA = climate.climate_schema(DuepiClimate).extend(
    {cv.GenerateID("duepi_id"): cv.use_id(DuepiHub)}
)


async def to_code(config):
    entity = await climate.new_climate(config)
    hub = await cg.get_variable(config["duepi_id"])
    cg.add(entity.set_parent(hub))
    cg.add(hub.set_climate(entity))