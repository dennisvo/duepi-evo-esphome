import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from . import duepi_ns

DuepiPrivilegeSwitch = duepi_ns.class_("DuepiPrivilegeSwitch", switch.Switch)

CONFIG_SCHEMA = cv.invalid(
    "Initialization compatibility is configured with duepi.init_command, not a switch."
)
