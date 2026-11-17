import importlib
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock

import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import automation
from esphome.core import CORE
from importlib.metadata import version

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "components"))
duepi = importlib.import_module("duepi")
duepi_number = importlib.import_module("duepi.number")
duepi_text_sensor = importlib.import_module("duepi.text_sensor")


class ConfigTests(unittest.TestCase):
    def setUp(self):
        CORE.reset()

    def config(self, **overrides):
        return duepi.CONFIG_SCHEMA({"id": "stove", "uart_id": "stove_uart", **overrides})

    def test_pinned_environment(self):
        self.assertEqual(version("esphome"), "2026.10.2")
        self.assertEqual(version("ziglang"), "0.15.2")
        self.assertEqual(version("cbor2"), "6.1.2")

    def test_feature_defaults(self):
        config = self.config()
        for key in ("enable_reset", "enable_installer", "init_command"):
            self.assertFalse(config[key])
        self.assertNotIn("enable_writes", config)
        self.assertNotIn("_installer_entities", config)

    def test_single_stove_configuration(self):
        self.assertFalse(duepi.MULTI_CONF)
        with self.assertRaises(cv.Invalid):
            duepi.CONFIG_SCHEMA([{"id": "stove"}, {"id": "second_stove"}])

    def test_power_level_configuration(self):
        self.assertTrue(self.config()["read_power_level"])
        self.assertFalse(self.config(read_power_level=False)["read_power_level"])
        sensors = importlib.import_module("duepi.sensor")
        self.assertEqual(sensors.FIELDS["power_level"], (14, None, 0))
        config = sensors.CONFIG_SCHEMA({
            "duepi_id": "stove",
            "power_level": {"name": "Power", "entity_category": "diagnostic"},
        })
        self.assertEqual(config["power_level"]["entity_category"], "diagnostic")
        self.assertEqual(duepi_text_sensor.FIELDS[9], "cycle_time")

    def test_optional_diagnostics(self):
        sensors = importlib.import_module("duepi.sensor")
        fields = {
            "flue_extraction_speed": 15,
            "extraction_sensor_level": 16,
            "flue_motor_correction": 17,
            "pellet_correction": 18,
        }
        config = sensors.CONFIG_SCHEMA({
            "duepi_id": "stove",
            **{key: {"name": key} for key in fields},
        })
        for key, index in fields.items():
            self.assertEqual(sensors.FIELDS[key], (index, None, 0))
            self.assertEqual(config[key]["entity_category"], "diagnostic")
        cycle = duepi_text_sensor.CONFIG_SCHEMA({
            "duepi_id": "stove", "cycle_time": {"name": "Cycle Time"},
        })
        self.assertEqual(cycle["cycle_time"]["entity_category"], "diagnostic")

    def test_sensor_entity_categories(self):
        sensors = importlib.import_module("duepi.sensor")
        primary = {"room_temperature", "target_temperature", "flue_temperature", "power_level", "d4000"}
        config = sensors.CONFIG_SCHEMA({
            "duepi_id": "stove", **{key: {"name": key} for key in sensors.FIELDS},
        })
        for key in sensors.FIELDS:
            with self.subTest(key=key):
                if key in primary:
                    self.assertFalse(config[key].get("entity_category"))
                else:
                    self.assertEqual(config[key]["entity_category"], "diagnostic")

    def test_text_entity_categories(self):
        config = duepi_text_sensor.CONFIG_SCHEMA({
            "duepi_id": "stove", **{key: {"name": key} for key in duepi_text_sensor.FIELDS},
        })
        for key in duepi_text_sensor.FIELDS:
            with self.subTest(key=key):
                if key in ("phase", "alarm"):
                    self.assertFalse(config[key].get("entity_category"))
                elif key == "installer":
                    self.assertEqual(config[key]["entity_category"], "config")
                else:
                    self.assertEqual(config[key]["entity_category"], "diagnostic")

    def test_entity_category_overrides(self):
        sensors = importlib.import_module("duepi.sensor")
        numeric = sensors.CONFIG_SCHEMA({
            "duepi_id": "stove",
            "pcb_temperature": {"name": "PCB", "entity_category": ""},
            "room_temperature": {"name": "Room", "entity_category": "diagnostic"},
        })
        self.assertFalse(numeric["pcb_temperature"]["entity_category"])
        self.assertEqual(numeric["room_temperature"]["entity_category"], "diagnostic")
        text = duepi_text_sensor.CONFIG_SCHEMA({
            "duepi_id": "stove", "firmware": {"name": "Firmware", "entity_category": ""},
        })
        self.assertFalse(text["firmware"]["entity_category"])
        schema = importlib.import_module("duepi.binary_sensor").CONFIG_SCHEMA
        communication = schema({"duepi_id": "stove", "communication": {"name": "Communication"}})
        self.assertEqual(communication["communication"]["entity_category"], "diagnostic")
        override = schema({
            "duepi_id": "stove", "communication": {"name": "Communication Override", "entity_category": ""},
        })
        self.assertFalse(override["communication"]["entity_category"])

    def test_operating_controls_are_not_configuration(self):
        level = duepi_number.CONFIG_SCHEMA({"name": "Level", "duepi_id": "stove"})
        reset = importlib.import_module("duepi.button").CONFIG_SCHEMA({"name": "Reset", "duepi_id": "stove"})
        self.assertFalse(level.get("entity_category"))
        self.assertFalse(reset.get("entity_category"))

    def test_installer_entities_only_when_included(self):
        config = self.config(enable_installer=True)
        self.assertFalse(config["enable_reset"])
        entities = config["_installer_entities"]
        self.assertEqual(set(entities), {"api", "access", "parameter", "value", "read", "write", "result", "response"})
        self.assertEqual(entities["access"]["restore_mode"], "ALWAYS_OFF")
        self.assertEqual(entities["access"]["name"], "Installer Access")
        for key in ("access", "parameter", "value", "read", "write", "result"):
            self.assertEqual(entities[key]["entity_category"], "config")
        self.assertEqual(entities["response"]["entity_category"], "diagnostic")

    def test_arming_is_not_user_facing(self):
        self.assertEqual(set(duepi.DUEPI_ACTIONS), {
            "duepi.installer_read", "duepi.installer_write", "duepi.send_command", "duepi.send_raw",
        })
        self.assertNotIn("duepi.arm_installer_write", automation.ACTION_REGISTRY)
        self.assertNotIn("duepi.arm_custom_command", automation.ACTION_REGISTRY)

    def test_old_advanced_flags_rejected(self):
        for key in ("enable_custom_commands", "enable_raw_commands"):
            with self.subTest(key=key), self.assertRaises(cv.Invalid):
                self.config(**{key: True})

    def test_invalid_freshness(self):
        with self.assertRaises(cv.Invalid):
            self.config(stale_after="10s", update_interval="10s")

    def test_initialization_configuration(self):
        config = self.config(init_command=True)
        self.assertTrue(config["init_command"])

    def test_initialization_switch_rejected(self):
        schema = importlib.import_module("duepi.switch").CONFIG_SCHEMA
        with self.assertRaisesRegex(cv.Invalid, "duepi.init_command"):
            schema({"name": "Initialization Compatibility", "kind": "initialization", "duepi_id": "stove"})

    def test_temperature_limits(self):
        defaults = self.config()
        self.assertEqual(defaults["min_temperature"], 10)
        self.assertEqual(defaults["max_temperature"], 25)
        configured = self.config(min_temperature=12, max_temperature=30)
        self.assertEqual(configured["max_temperature"], 30)

    def test_invalid_temperature_limits(self):
        for minimum, maximum in ((25, 25), (26, 25), (9, 25), (10, 36), (10, 25.5)):
            with self.subTest(minimum=minimum, maximum=maximum), self.assertRaises(cv.Invalid):
                self.config(min_temperature=minimum, max_temperature=maximum)

    def test_temperature_number_entities(self):
        for kind in ("min_temperature", "max_temperature"):
            config = duepi_number.CONFIG_SCHEMA({"name": kind, "duepi_id": "stove", "kind": kind})
            self.assertEqual(config["kind"], kind)
            self.assertEqual(config["entity_category"], "config")
            self.assertEqual(config["device_class"], "temperature")

    def test_level_number_compatibility(self):
        config = duepi_number.CONFIG_SCHEMA({"name": "Requested Level", "duepi_id": "stove"})
        self.assertEqual(config["kind"], "level")

    def test_control_entity_feature_dependencies(self):
        reset = importlib.import_module("duepi.button")
        for enabled in (False, True):
            parent = self.config(enable_reset=enabled)
            full_config = Mock()
            full_config.get_path_for_id.return_value = ["duepi", "id"]
            full_config.get_config_for_path.return_value = parent
            token = fv.full_config.set(full_config)
            try:
                suffix = str(enabled)
                button = reset.CONFIG_SCHEMA({"name": f"Reset {suffix}", "duepi_id": "stove"})
                if enabled:
                    reset.FINAL_VALIDATE_SCHEMA(button)
                else:
                    with self.assertRaisesRegex(cv.Invalid, "enable_reset"):
                        reset.FINAL_VALIDATE_SCHEMA(button)
                for kind in ("level", "min_temperature", "max_temperature"):
                    duepi_number.CONFIG_SCHEMA({
                        "name": f"{kind} {suffix}", "kind": kind, "duepi_id": "stove",
                    })
            finally:
                fv.full_config.reset(token)

    def test_target_source_diagnostic(self):
        config = duepi_text_sensor.CONFIG_SCHEMA({
            "duepi_id": "stove",
            "target_source": {"name": "Target Temperature Source", "entity_category": "diagnostic"},
        })
        self.assertEqual(duepi_text_sensor.FIELDS[8], "target_source")
        self.assertEqual(config["target_source"]["entity_category"], "diagnostic")

    def test_unknown_number_kind(self):
        with self.assertRaises(cv.Invalid):
            duepi_number.CONFIG_SCHEMA({"name": "Invalid", "duepi_id": "stove", "kind": "stove_parameter"})

    def test_invalid_deadlines(self):
        for value in ("0ms", "10s", "-1ms"):
            with self.subTest(value=value), self.assertRaises(cv.Invalid):
                self.config(response_timeout=value)

    def test_interval_bounds(self):
        for value in ("1ms", "100d"):
            with self.subTest(value=value), self.assertRaises(cv.Invalid):
                self.config(update_interval=value)

    def test_arming_bounds(self):
        for value in ("0s", "121s"):
            with self.subTest(value=value), self.assertRaises(cv.Invalid):
                self.config(arming_timeout=value)

    def test_unknown_profile(self):
        with self.assertRaises(cv.Invalid):
            self.config(firmware_profile="guess")

    def test_independent_reset_opt_in(self):
        config = self.config(enable_reset=True)
        self.assertTrue(config["enable_reset"])
        self.assertFalse(config["enable_installer"])

    def test_removed_write_setting_rejected(self):
        for value in (False, True):
            with self.subTest(value=value), self.assertRaises(cv.Invalid):
                self.config(enable_writes=value)

    def test_actions_registered_before_codegen(self):
        for name in duepi.DUEPI_ACTIONS:
            with self.subTest(name=name):
                self.assertIn(name, automation.ACTION_REGISTRY)

    def test_static_installer_ranges(self):
        schema = automation.ACTION_REGISTRY["duepi.installer_write"].schema
        for parameter, value in ((-1, 0), (256, 0), (1, -1), (1, 256)):
            with self.subTest(parameter=parameter, value=value), self.assertRaises(cv.Invalid):
                schema({"id": "stove", "parameter": parameter, "value": value})

    def test_static_custom_command_strings(self):
        for name, command in (("duepi.send_command", "D100"), ("duepi.send_raw", "RD1000")):
            with self.subTest(action=name):
                schema = automation.ACTION_REGISTRY[name].schema
                self.assertEqual(schema({"id": "stove", "command": command})["command"], command)
                with self.assertRaises(cv.Invalid):
                    schema({"id": "stove", "command": 123})


if __name__ == "__main__":
    unittest.main(verbosity=2)