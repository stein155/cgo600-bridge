from esphome import automation
import esphome.codegen as cg
from esphome.components import esp32_ble, esp32_ble_client, esp32_ble_tracker, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ID, CONF_TRIGGER_ID
from esphome.core import ID, HexInt

from .web_assets import build_assets

AUTO_LOAD = [
    "esp32_ble_client",
    "esp32_ble_tracker",
    "sensor",
    "switch",
    "text_sensor",
    "web_server_base",
]
DEPENDENCIES = ["esp32", "wifi"]
CODEOWNERS = ["@stein155"]

CONF_CGO600_ID = "cgo600_id"
CONF_DEVICE_NAME = "device_name"
CONF_POLL_INTERVAL = "poll_interval"
CONF_PAUSE_SCAN_WHILE_CONNECTED = "pause_scan_while_connected"
CONF_MAX_RIDES = "max_rides"
CONF_WEB_UI = "web_ui"
CONF_WEB_UI_ID = "web_ui_id"
CONF_ON_RIDE = "on_ride"

cgo600_ns = cg.esphome_ns.namespace("cgo600")
CGO600Component = cgo600_ns.class_("CGO600Component", esp32_ble_client.BLEClientBase)
CGO600WebUI = cgo600_ns.class_("CGO600WebUI", cg.Component)
RideTrigger = cgo600_ns.class_(
    "RideTrigger", automation.Trigger.template(cg.float_, cg.uint32, cg.int_)
)


def _no_web_server(config):
    # ESPHome's own web_server also serves "/", which would shadow the dashboard.
    if config[CONF_WEB_UI] and "web_server" in fv.full_config.get():
        raise cv.Invalid(
            "The cgo600 dashboard and ESPHome's web_server both serve '/'. "
            "Remove web_server or set 'web_ui: false'."
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CGO600Component),
            cv.GenerateID(CONF_WEB_UI_ID): cv.declare_id(CGO600WebUI),
            cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
                web_server_base.WebServerBase
            ),
            cv.Optional(CONF_DEVICE_NAME, default="CGO600"): cv.string_strict,
            cv.Optional(CONF_POLL_INTERVAL, default="1s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=cv.TimePeriod(milliseconds=500), max=cv.TimePeriod(seconds=10)
                ),
            ),
            cv.Optional(CONF_PAUSE_SCAN_WHILE_CONNECTED, default=True): cv.boolean,
            cv.Optional(CONF_MAX_RIDES, default=20): cv.int_range(min=1, max=50),
            cv.Optional(CONF_WEB_UI, default=True): cv.boolean,
            cv.Optional(CONF_ON_RIDE): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(RideTrigger)}
            ),
        }
    )
    .extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    esp32_ble.consume_connection_slots(1, "cgo600"),
)

FINAL_VALIDATE_SCHEMA = _no_web_server

CGO600_CHILD_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_CGO600_ID): cv.use_id(CGO600Component),
    }
)


async def _web_ui_to_code(config, hub):
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    web = cg.new_Pvariable(config[CONF_WEB_UI_ID], base, hub)
    await cg.register_component(web, {})
    for index, (path, content_type, data) in enumerate(build_assets()):
        arr_id = ID(f"cgo600_web_asset_{index}", is_declaration=True, type=cg.uint8)
        arr = cg.progmem_array(arr_id, [HexInt(b) for b in data])
        cg.add(
            web.add_asset(
                cg.RawExpression(f'"{path}"'),
                cg.RawExpression(f'"{content_type}"'),
                arr,
                len(data),
            )
        )


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await esp32_ble_tracker.register_client(var, config)
    cg.add(var.set_device_name(config[CONF_DEVICE_NAME]))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_pause_scan_while_connected(config[CONF_PAUSE_SCAN_WHILE_CONNECTED]))
    cg.add(var.set_max_rides(config[CONF_MAX_RIDES]))

    for conf in config.get(CONF_ON_RIDE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [(cg.float_, "distance_km"), (cg.uint32, "duration_s"), (cg.int_, "battery_used")],
            conf,
        )

    if config[CONF_WEB_UI]:
        await _web_ui_to_code(config, var)
