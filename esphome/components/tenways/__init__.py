import esphome.codegen as cg
from esphome.components import esp32_ble, esp32_ble_client, esp32_ble_tracker
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["esp32_ble_client", "esp32_ble_tracker", "sensor", "switch", "text_sensor"]
DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@stein155"]
# One block per bike; each takes one BLE connection.
MULTI_CONF = True

CONF_TENWAYS_ID = "tenways_id"
CONF_DEVICE_NAMES = "device_names"
CONF_POLL_INTERVAL = "poll_interval"

# Matched case-insensitively as substrings of the advertised name, so CGO800
# also finds "CGO800 PLUS". Plain "AGO" is left out because it also matches
# unrelated names such as "Chicago".
DEFAULT_DEVICE_NAMES = [
    "CGO600",
    "CGO800",
    "CGO009",
    "CGO001",
    "CGO COMPACT",
    "Longtail Duo",
    "AGO AIR",
    "AGOX",
    "AGOT",
    "CARGO",
    "Wayfarer",
]

tenways_ns = cg.esphome_ns.namespace("tenways")
TenwaysComponent = tenways_ns.class_("TenwaysComponent", esp32_ble_client.BLEClientBase)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TenwaysComponent),
            cv.Optional(CONF_DEVICE_NAMES, default=DEFAULT_DEVICE_NAMES): cv.All(
                cv.ensure_list(cv.string_strict), cv.Length(min=1)
            ),
            cv.Optional(CONF_POLL_INTERVAL, default="1s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(milliseconds=500), max=cv.TimePeriod(seconds=10)),
            ),
        }
    )
    .extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    esp32_ble.consume_connection_slots(1, "tenways"),
)

TENWAYS_CHILD_SCHEMA = cv.Schema({cv.GenerateID(CONF_TENWAYS_ID): cv.use_id(TenwaysComponent)})


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await esp32_ble_tracker.register_client(var, config)
    cg.add(var.set_storage_key(str(config[CONF_ID])))
    cg.add(var.set_device_names(config[CONF_DEVICE_NAMES]))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
