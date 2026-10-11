import esphome.codegen as cg
from esphome.components import esp32_ble, esp32_ble_client, esp32_ble_tracker
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["esp32_ble_client", "esp32_ble_tracker", "sensor", "switch", "text_sensor"]
DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@stein155"]

CONF_CGO600_ID = "cgo600_id"
CONF_DEVICE_NAME = "device_name"
CONF_POLL_INTERVAL = "poll_interval"
CONF_PAUSE_SCAN_WHILE_CONNECTED = "pause_scan_while_connected"

cgo600_ns = cg.esphome_ns.namespace("cgo600")
CGO600Component = cgo600_ns.class_("CGO600Component", esp32_ble_client.BLEClientBase)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CGO600Component),
            cv.Optional(CONF_DEVICE_NAME, default="CGO600"): cv.string_strict,
            cv.Optional(CONF_POLL_INTERVAL, default="1s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=cv.TimePeriod(milliseconds=500), max=cv.TimePeriod(seconds=10)
                ),
            ),
            cv.Optional(CONF_PAUSE_SCAN_WHILE_CONNECTED, default=True): cv.boolean,
        }
    )
    .extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    esp32_ble.consume_connection_slots(1, "cgo600"),
)

CGO600_CHILD_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_CGO600_ID): cv.use_id(CGO600Component),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await esp32_ble_tracker.register_client(var, config)
    cg.add(var.set_device_name(config[CONF_DEVICE_NAME]))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_pause_scan_while_connected(config[CONF_PAUSE_SCAN_WHILE_CONNECTED]))
