#include "web_ui.h"

#ifdef USE_ESP32

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"
#include "esphome/components/network/util.h"

#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#ifdef USE_CAPTIVE_PORTAL
#include "esphome/components/captive_portal/captive_portal.h"
#endif

namespace esphome::cgo {

static const char *const TAG = "cgo.web";
static const char *const JSON_TYPE = "application/json";
// ESPHome's ESP-IDF server only emits 200, 404 and 409; any other status code
// goes out as 500. Invalid requests therefore answer 409.
static constexpr int HTTP_OK = 200;
static constexpr int HTTP_NOT_FOUND = 404;
static constexpr int HTTP_INVALID = 409;

void CGOWebUI::setup() {
    this->base_->init();
    this->base_->add_handler(this);
}

void CGOWebUI::dump_config() {
    size_t total = 0;
    for (const auto &asset : this->assets_)
        total += asset.size;
    ESP_LOGCONFIG(TAG, "CGO web UI:");
    ESP_LOGCONFIG(TAG, "  Assets: %u (%u bytes gzipped)", (unsigned) this->assets_.size(), (unsigned) total);
}

const CGOWebUI::Asset *CGOWebUI::find_asset_(const StringRef &path) const {
    const StringRef lookup = path == "/" ? StringRef("/index.html") : path;
    for (const auto &asset : this->assets_) {
        if (lookup == asset.path)
            return &asset;
    }
    return nullptr;
}

bool CGOWebUI::canHandle(AsyncWebServerRequest *request) const {
#ifdef USE_CAPTIVE_PORTAL
    // While the WiFi setup hotspot is up, the captive portal owns every page.
    if (captive_portal::global_captive_portal != nullptr && captive_portal::global_captive_portal->is_active())
        return false;
#endif
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    const StringRef url = request->url_to(url_buf);
    if (request->method() == HTTP_POST)
        return url.size() > 5 && strncmp(url.c_str(), "/api/", 5) == 0;
    if (request->method() != HTTP_GET)
        return false;
    return (url.size() > 5 && strncmp(url.c_str(), "/api/", 5) == 0) || this->find_asset_(url) != nullptr;
}

void CGOWebUI::send_json_(AsyncWebServerRequest *request, int code, const std::string &json) {
    auto *response = request->beginResponse(code, JSON_TYPE, json);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

void CGOWebUI::handleRequest(AsyncWebServerRequest *request) {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    const StringRef url = request->url_to(url_buf);

    if (request->method() == HTTP_GET) {
        if (url == "/api/state") {
            this->handle_state_(request);
        } else if (url == "/api/config") {
            this->handle_config_(request);
        } else if (url == "/api/rides") {
            this->send_json_(request, HTTP_OK, this->bike_->rides().to_json());
        } else if (const Asset *asset = this->find_asset_(url)) {
            auto *response = request->beginResponse(HTTP_OK, asset->content_type, asset->data, asset->size);
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "no-cache");
            request->send(response);
        } else {
            this->send_json_(request, HTTP_NOT_FOUND, "{\"error\":\"not found\"}");
        }
        return;
    }

    // POST: everything below touches BLE or flash, so it runs on the main loop.
    if (url == "/api/light") {
        if (!request->hasArg("on")) {
            this->send_json_(request, HTTP_INVALID, "{\"error\":\"missing 'on'\"}");
            return;
        }
        const bool on = request->arg("on") == "1" || request->arg("on") == "true";
        CGOComponent *bike = this->bike_;
        this->defer([bike, on]() { bike->set_light(on); });
        this->send_json_(request, HTTP_OK, on ? "{\"ok\":true,\"light\":\"on\"}" : "{\"ok\":true,\"light\":\"off\"}");
    } else if (url == "/api/rides/clear") {
        CGOComponent *bike = this->bike_;
        this->defer([bike]() { bike->rides().clear(); });
        this->send_json_(request, HTTP_OK, "{\"ok\":true}");
    } else if (url == "/api/rides/import") {
        this->handle_import_(request);
    } else if (url == "/api/restart") {
        this->send_json_(request, HTTP_OK, "{\"ok\":true}");
        this->set_timeout(500, []() { App.safe_reboot(); });
    } else {
        this->send_json_(request, HTTP_NOT_FOUND, "{\"error\":\"not found\"}");
    }
}

void CGOWebUI::handle_state_(AsyncWebServerRequest *request) {
    const protocol::BikeState s = this->bike_->bike_state();
    std::string json = "{";
    char buf[96];
    auto add = [&](const char *fmt, auto value) {
        snprintf(buf, sizeof(buf), fmt, value);
        json += buf;
    };
    if (s.battery >= 0)
        add("\"battery\":%d,", s.battery);
    if (s.trip_duration >= 0)
        add("\"trip_duration\":%d,", s.trip_duration);
    if (s.speed_kmh >= 0)
        add("\"speed\":%.1f,", s.speed_kmh);
    if (s.max_speed_kmh >= 0)
        add("\"max_speed\":%.1f,", s.max_speed_kmh);
    if (s.avg_speed_kmh >= 0)
        add("\"avg_speed\":%.1f,", s.avg_speed_kmh);
    if (s.trip_distance_km >= 0)
        add("\"trip_distance\":%.1f,", s.trip_distance_km);
    if (s.remaining_range_km >= 0)
        add("\"remaining_range\":%.1f,", s.remaining_range_km);
    if (s.total_range_km >= 0)
        add("\"range\":%.1f,", s.total_range_km);
    if (s.light_valid)
        add("\"light\":\"%s\",", s.light_on ? "on" : "off");
    if (s.serial_number[0] != '\0')
        add("\"serial\":\"%s\",", s.serial_number);  // printable ASCII without quotes
    if (const uint32_t at = this->bike_->last_connected_at()) {
        const time_t t = at;
        struct tm utc;
        gmtime_r(&t, &utc);
        char iso[24];
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &utc);
        add("\"last_connected\":\"%s\",", iso);
    }
    bool api_connected = false;
#ifdef USE_API
    api_connected = api::global_api_server != nullptr && api::global_api_server->is_connected();
#endif
    add("\"ble_status\":\"%s\",", this->bike_->ble_status());
    add("\"api_connected\":%s}", api_connected ? "true" : "false");
    this->send_json_(request, HTTP_OK, json);
}

void CGOWebUI::handle_config_(AsyncWebServerRequest *request) {
    char ip[network::IP_ADDRESS_BUFFER_SIZE] = "";
    for (const auto &addr : network::get_ip_addresses()) {
        if (addr.is_set() && addr.is_ip4()) {
            addr.str_to(ip);
            break;
        }
    }
    char buf[384];
    snprintf(buf, sizeof(buf),
             "{\"device_name\":\"%s\",\"poll_interval\":%.1f,\"max_rides\":%u,\"rides_stored\":%u,"
             "\"esphome_version\":\"%s\",\"name\":\"%s\",\"hostname\":\"%s.local\",\"ip\":\"%s\"}",
             this->bike_->device_name().c_str(), this->bike_->poll_interval_ms() / 1000.0f,
             (unsigned) this->bike_->rides().max_rides(), (unsigned) this->bike_->rides().count(), ESPHOME_VERSION,
             App.get_friendly_name().c_str(), App.get_name().c_str(), ip);
    this->send_json_(request, HTTP_OK, buf);
}

// One ride per request, as query parameters: start, end (epoch seconds),
// distance (km), duration (s) and optionally battery_start, battery_end,
// avg and max (km/h). Small requests stay within the server's header limits.
void CGOWebUI::handle_import_(AsyncWebServerRequest *request) {
    if (!request->hasArg("start") || !request->hasArg("end") || !request->hasArg("distance") ||
        !request->hasArg("duration")) {
        this->send_json_(request, HTTP_INVALID, "{\"error\":\"missing ride fields\"}");
        return;
    }
    auto int_arg = [request](const char *name, long fallback) {
        return request->hasArg(name) ? strtol(request->arg(name).c_str(), nullptr, 10) : fallback;
    };
    auto float_arg = [request](const char *name, float fallback) {
        return request->hasArg(name) ? strtof(request->arg(name).c_str(), nullptr) : fallback;
    };

    RideRecord ride;
    ride.start = (uint32_t) int_arg("start", 0);
    ride.end = (uint32_t) int_arg("end", 0);
    ride.distance_km = float_arg("distance", 0);
    ride.duration_s = (int32_t) int_arg("duration", 0);
    ride.battery_start = (int8_t) int_arg("battery_start", -1);
    ride.battery_end = (int8_t) int_arg("battery_end", -1);
    const float avg = float_arg("avg", -1);
    const float max = float_arg("max", -1);
    ride.avg_speed_x10 = avg >= 0 ? (int16_t) (avg * 10 + 0.5f) : -1;
    ride.max_speed_x10 = max >= 0 ? (int16_t) (max * 10 + 0.5f) : -1;

    CGOComponent *bike = this->bike_;
    this->defer([bike, ride]() { bike->rides().import_ride(ride); });
    this->send_json_(request, HTTP_OK, "{\"ok\":true}");
}

}  // namespace esphome::cgo

#endif  // USE_ESP32
