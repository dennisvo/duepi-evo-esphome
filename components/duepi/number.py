import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import UNIT_CELSIUS
from . import DuepiHub, duepi_ns

DuepiLevel = duepi_ns.class_("DuepiLevel", number.Number)
DuepiTemperatureLimit = duepi_ns.class_("DuepiTemperatureLimit", number.Number)
PARENT_SCHEMA = {cv.GenerateID("duepi_id"): cv.use_id(DuepiHub)}
CONFIG_SCHEMA = cv.typed_schema(
    {
        "level": number.number_schema(DuepiLevel).extend(PARENT_SCHEMA),
        **{
            kind: number.number_schema(
                DuepiTemperatureLimit,
                entity_category="config",
                device_class="temperature",
                unit_of_measurement=UNIT_CELSIUS,
            ).extend(PARENT_SCHEMA)
            for kind in ("min_temperature", "max_temperature")
        },
    },
    key="kind",
    default_type="level",
)


async def to_code(config):
    level = config["kind"] == "level"
    entity = await number.new_number(config, min_value=1 if level else 10, max_value=5 if level else 35, step=1)
    hub = await cg.get_variable(config["duepi_id"])
    cg.add(entity.set_parent(hub))
    if level:
        cg.add(hub.set_level_number(entity))
    else:
        minimum = config["kind"] == "min_temperature"
        cg.add(entity.set_minimum(minimum))
        cg.add(hub.set_temperature_limit_number(minimum, entity))