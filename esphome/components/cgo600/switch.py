import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_LIGHT

from . import CGO600_CHILD_SCHEMA, CONF_CGO600_ID, cgo600_ns

DEPENDENCIES = ["cgo600"]

CGO600LightSwitch = cgo600_ns.class_("CGO600LightSwitch", switch.Switch)

CONFIG_SCHEMA = CGO600_CHILD_SCHEMA.extend(
    {
        cv.Optional(CONF_LIGHT): switch.switch_schema(
            CGO600LightSwitch, icon="mdi:lightbulb", block_inverted=True
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_CGO600_ID])
    if light_config := config.get(CONF_LIGHT):
        sw = await switch.new_switch(light_config)
        await cg.register_parented(sw, config[CONF_CGO600_ID])
        cg.add(hub.set_light_switch(sw))
