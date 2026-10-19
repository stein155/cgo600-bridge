import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_LIGHT

from . import CGO_CHILD_SCHEMA, CONF_CGO_ID, cgo_ns

DEPENDENCIES = ["cgo"]

CGOLightSwitch = cgo_ns.class_("CGOLightSwitch", switch.Switch)

CONFIG_SCHEMA = CGO_CHILD_SCHEMA.extend(
    {
        cv.Optional(CONF_LIGHT): switch.switch_schema(
            CGOLightSwitch, icon="mdi:lightbulb", block_inverted=True
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_CGO_ID])
    if light_config := config.get(CONF_LIGHT):
        sw = await switch.new_switch(light_config)
        await cg.register_parented(sw, config[CONF_CGO_ID])
        cg.add(hub.set_light_switch(sw))
