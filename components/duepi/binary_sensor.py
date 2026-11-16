import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_CONNECTIVITY
from . import DuepiHub

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID("duepi_id"): cv.use_id(DuepiHub),
        cv.Required("communication"): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config["duepi_id"])
    entity = await binary_sensor.new_binary_sensor(config["communication"])
    cg.add(hub.set_communication_sensor(entity))