#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "bike_state.h"

class WebUI {
public:
    void begin();
    void loop();
    void queueLightCommand(bool on);
    bool takeLightCommand(bool& on);

private:
    void broadcastStateIfChanged();

    AsyncWebServer _server{80};
    AsyncWebSocket _ws{"/ws"};
    volatile bool _pendingLightCmd = false;
    volatile bool _pendingLightOn = false;
    unsigned long _lastWsBroadcastMs = 0;
    char _lastWsPayload[512] = {0};
};

extern WebUI webUI;
