import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC, ICON_BLUETOOTH

from . import EBIKE_CHILD_SCHEMA, CONF_EBIKE_ID

DEPENDENCIES = ["ebike"]

TEXT_SENSORS = {
    "serial_number": text_sensor.text_sensor_schema(
        icon="mdi:identifier",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    "firmware_version": text_sensor.text_sensor_schema(
        icon="mdi:chip",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    "ble_status": text_sensor.text_sensor_schema(
        icon=ICON_BLUETOOTH,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}

CONFIG_SCHEMA = EBIKE_CHILD_SCHEMA.extend(
    {cv.Optional(key): schema for key, schema in TEXT_SENSORS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_EBIKE_ID])
    for key in TEXT_SENSORS:
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_text_sensor")(sens))
