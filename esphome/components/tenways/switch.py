import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_LIGHT

from . import CONF_TENWAYS_ID, TENWAYS_CHILD_SCHEMA, tenways_ns

DEPENDENCIES = ["tenways"]

TenwaysLightSwitch = tenways_ns.class_("TenwaysLightSwitch", switch.Switch)

CONFIG_SCHEMA = TENWAYS_CHILD_SCHEMA.extend(
    {
        cv.Optional(CONF_LIGHT): switch.switch_schema(TenwaysLightSwitch, icon="mdi:lightbulb", block_inverted=True),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_TENWAYS_ID])
    if light_config := config.get(CONF_LIGHT):
        sw = await switch.new_switch(light_config)
        await cg.register_parented(sw, config[CONF_TENWAYS_ID])
        cg.add(hub.set_light_switch(sw))
