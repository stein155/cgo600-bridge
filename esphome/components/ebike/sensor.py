import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_DISTANCE,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_SPEED,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_KILOMETER,
    UNIT_KILOMETER_PER_HOUR,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_WATT,
)

from . import EBIKE_CHILD_SCHEMA, CONF_EBIKE_ID

DEPENDENCIES = ["ebike"]


def _speed(icon):
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOMETER_PER_HOUR,
        device_class=DEVICE_CLASS_SPEED,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
        icon=icon,
    )


def _distance(icon):
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOMETER,
        device_class=DEVICE_CLASS_DISTANCE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
        icon=icon,
    )


SENSORS = {
    "battery": sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        device_class=DEVICE_CLASS_BATTERY,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    "speed": _speed("mdi:speedometer"),
    "max_speed": _speed("mdi:speedometer-slow"),
    "avg_speed": _speed("mdi:speedometer-medium"),
    "trip_distance": _distance("mdi:map-marker-distance"),
    "trip_duration": sensor.sensor_schema(
        unit_of_measurement=UNIT_SECOND,
        device_class=DEVICE_CLASS_DURATION,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
        icon="mdi:timer-outline",
    ),
    "remaining_range": _distance("mdi:map-marker-radius"),
    # Never resets, so Home Assistant derives rides and distance per period
    # from it.
    "odometer": sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOMETER,
        device_class=DEVICE_CLASS_DISTANCE,
        state_class=STATE_CLASS_TOTAL_INCREASING,
        accuracy_decimals=1,
        icon="mdi:counter",
    ),
    "power": sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT,
        device_class=DEVICE_CLASS_POWER,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
}

CONFIG_SCHEMA = EBIKE_CHILD_SCHEMA.extend(
    {cv.Optional(key): schema for key, schema in SENSORS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_EBIKE_ID])
    for key in SENSORS:
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_sensor")(sens))
