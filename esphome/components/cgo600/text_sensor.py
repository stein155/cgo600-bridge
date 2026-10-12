import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC, ICON_BLUETOOTH

from . import CGO600_CHILD_SCHEMA, CONF_CGO600_ID

DEPENDENCIES = ["cgo600"]

TEXT_SENSORS = {
    "serial_number": text_sensor.text_sensor_schema(
        icon="mdi:identifier",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    "ble_status": text_sensor.text_sensor_schema(
        icon=ICON_BLUETOOTH,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}

CONFIG_SCHEMA = CGO600_CHILD_SCHEMA.extend(
    {cv.Optional(key): schema for key, schema in TEXT_SENSORS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_CGO600_ID])
    for key in TEXT_SENSORS:
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_text_sensor")(sens))
