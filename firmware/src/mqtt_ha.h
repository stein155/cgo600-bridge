#pragma once
#include <Arduino.h>
#include <AsyncMqttClient.h>
#include "bike_state.h"

typedef bool (*MqttLightCommandCallback)(bool on);

class MqttHA {
public:
    void begin(const char* broker, uint16_t port,
               const char* user, const char* pass,
               const char* prefix);
    void disconnect();
    void loop();
    void publishState(const BikeState& state);
    void publishBleStatus(const char* status);
    bool isConnected() const;
    void setLightCommandCallback(MqttLightCommandCallback cb) { _lightCommandCallback = cb; }

private:
    enum class ConnectionState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
    };

    void onConnect(bool sessionPresent);
    void onDisconnect(AsyncMqttClientDisconnectReason reason);
    void onMessage(char* topic, char* payload, size_t len, size_t index, size_t total);
    void startConnect();
    void publishDiscovery(const char* serial);
    void publishStateTopics(const BikeState& state);

    AsyncMqttClient _client;
    char _prefix[32] = "cgo600";
    char _broker[64] = {0};
    uint16_t _port = 1883;
    char _user[32] = {0};
    char _pass[64] = {0};
    char _lwtTopic[64] = {0};
    char _lightStateTopic[64] = {0};
    char _lightCommandTopic[64] = {0};
    char _clientId[32] = "cgo600-bridge";
    char _discoverySerial[32] = {0};
    bool _discoveryDone = false;
    bool _enabled = false;
    bool _hasCachedState = false;
    bool _hasPublishedState = false;
    bool _statePublishPending = false;
    unsigned long _lastReconnect = 0;
    unsigned long _connectStartedAt = 0;
    unsigned long _lastStatePublish = 0;
    ConnectionState _state = ConnectionState::DISCONNECTED;
    MqttLightCommandCallback _lightCommandCallback = nullptr;
    BikeState _cachedState;
    BikeState _publishedState;
    char _lastBleStatus[16] = {0};
};

extern MqttHA mqttHA;
