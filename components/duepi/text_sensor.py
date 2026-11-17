import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC
from . import DuepiHub

FIELDS = ("phase", "firmware", "board_type", "alarm", "transaction", "installer", "custom_response", "last_response", "target_source", "cycle_time")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID("duepi_id"): cv.use_id(DuepiHub),
        **{
            cv.Optional(key): text_sensor.text_sensor_schema(
                **({"entity_category": ENTITY_CATEGORY_DIAGNOSTIC} if key not in ("phase", "alarm") else {}),
            )
            for key in FIELDS
        },
    }
)


async def to_code(config):
    hub = await cg.get_variable(config["duepi_id"])
    for index, key in enumerate(FIELDS):
        if key in config:
            entity = await text_sensor.new_text_sensor(config[key])
            cg.add(hub.set_text_sensor(index, entity))