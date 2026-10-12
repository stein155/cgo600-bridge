#pragma once

#ifdef USE_ESP32

#include <vector>

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "cgo600.h"

namespace esphome::cgo600 {

// Serves the bridge dashboard (embedded, gzipped) and its JSON API on the
// shared ESPHome web server:
//   GET  /api/state, /api/config, /api/rides
//   POST /api/light?on=0|1, /api/rides/clear, /api/rides/import?..., /api/restart
// Requests are handled on the HTTP server task; anything that touches the
// BLE client or flash is deferred to the main loop.
class CGO600WebUI : public Component, public AsyncWebHandler {
 public:
    CGO600WebUI(web_server_base::WebServerBase *base, CGO600Component *bike) : base_(base), bike_(bike) {}

    void setup() override;
    void dump_config() override;
    float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

    void add_asset(const char *path, const char *content_type, const uint8_t *data, size_t size) {
        this->assets_.push_back({path, content_type, data, size});
    }

    bool canHandle(AsyncWebServerRequest *request) const override;
    void handleRequest(AsyncWebServerRequest *request) override;

 protected:
    struct Asset {
        const char *path;
        const char *content_type;
        const uint8_t *data;
        size_t size;
    };

    const Asset *find_asset_(const StringRef &path) const;
    void send_json_(AsyncWebServerRequest *request, int code, const std::string &json);
    void handle_state_(AsyncWebServerRequest *request);
    void handle_config_(AsyncWebServerRequest *request);
    void handle_import_(AsyncWebServerRequest *request);

    web_server_base::WebServerBase *base_;
    CGO600Component *bike_;
    std::vector<Asset> assets_;
};

}  // namespace esphome::cgo600

#endif  // USE_ESP32
