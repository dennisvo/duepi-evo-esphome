import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import DEVICE_CLASS_TEMPERATURE, ENTITY_CATEGORY_DIAGNOSTIC, STATE_CLASS_MEASUREMENT, UNIT_CELSIUS
from . import DuepiHub

FIELDS = {
    "phase_code": (0, None, 0),
    "room_temperature": (1, UNIT_CELSIUS, 1),
    "d3000": (2, None, 0),
    "d4000": (3, None, 0),
    "flue_temperature": (4, UNIT_CELSIUS, 0),
    "exhaust_speed": (5, "rpm", 0),
    "alarm_code": (6, None, 0),
    "inputs_code": (7, None, 0),
    "target_temperature": (8, UNIT_CELSIUS, 0),
    "pcb_temperature": (9, UNIT_CELSIUS, 0),
    "total_runtime_counter": (12, None, 0),
    "service_runtime_counter": (13, None, 0),
    "power_level": (14, None, 0),
    "flue_extraction_speed": (15, None, 0),
    "extraction_sensor_level": (16, None, 0),
    "flue_motor_correction": (17, None, 0),
    "pellet_correction": (18, None, 0),
    "exhaust_fan_correction_d500": (20, None, 0),
}

DIAGNOSTIC_FIELDS = {
    "phase_code", "d3000", "flue_temperature", "exhaust_speed", "alarm_code", "inputs_code", "pcb_temperature",
    "total_runtime_counter", "service_runtime_counter", "flue_extraction_speed",
    "extraction_sensor_level", "flue_motor_correction", "pellet_correction", "exhaust_fan_correction_d500",
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID("duepi_id"): cv.use_id(DuepiHub),
        **{
            cv.Optional(key): sensor.sensor_schema(
                accuracy_decimals=decimals,
                state_class=STATE_CLASS_MEASUREMENT,
                **({"entity_category": ENTITY_CATEGORY_DIAGNOSTIC} if key in DIAGNOSTIC_FIELDS else {}),
                **({"unit_of_measurement": unit} if unit is not None else {}),
                **({"device_class": DEVICE_CLASS_TEMPERATURE} if unit == UNIT_CELSIUS else {}),
            )
            for key, (_, unit, decimals) in FIELDS.items()
        },
    }
)


async def to_code(config):
    hub = await cg.get_variable(config["duepi_id"])
    for key, (index, _, _) in FIELDS.items():
        if key in config:
            entity = await sensor.new_sensor(config[key])
            cg.add(hub.set_numeric_sensor(index, entity))