#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include "bike_state.h"

enum class BleStatus : uint8_t {
    IDLE,
    SCANNING,
    CONNECTING,
    HANDSHAKE,
    POLLING,
    DISCONNECTED
};

typedef void (*StateChangeCallback)(const BikeState& state);

class BleClient : public NimBLEClientCallbacks {
public:
    void begin(const char* deviceName);
    void loop();
    void setCallback(StateChangeCallback cb) { _callback = cb; }
    void setPollInterval(float seconds) { _pollInterval = (unsigned long)(seconds * 1000); }
    void setWifiReady(bool ready);  // call once WiFi is connected

    BleStatus getStatus() const { return _status; }
    const BikeState& getState() const { return _state; }
    const char* getStatusStr() const;
    bool deviceFound() const { return _deviceFound; }
    bool hasStoredAddress() const { return _hasStoredAddr; }
    unsigned long getReconnectDelayMs() const { return _reconnectDelay; }
    unsigned long getLastReconnectMs() const { return _lastReconnect; }
    unsigned long getLastNotifyMs() const { return _lastNotify; }
    bool getLastScanStartOk() const { return _lastScanStartOk; }
    int getLastScanEndReason() const { return _lastScanEndReason; }
    unsigned long getLastScanStartedMs() const { return _lastScanStartedMs; }
    unsigned long getLastScanEndedMs() const { return _lastScanEndedMs; }
    int getLastConnectError() const { return _lastConnectError; }
    int getLastDisconnectReason() const { return _lastDisconnectReason; }
    bool getLastTargetConnectable() const { return _lastTargetConnectable; }
    unsigned long getLastSeenMs() const { return _lastSeenMs; }
    uint32_t getSeenConnectableCount() const { return _seenConnectableCount; }
    uint32_t getSeenNonConnectableCount() const { return _seenNonConnectableCount; }
    uint32_t getConnectAttemptCount() const { return _connectAttemptCount; }
    uint32_t getConnectFailCount() const { return _connectFailCount; }
    bool enable();
    void forgetPairedDevice();
    bool setLight(bool on);

    // Called from scan callback
    void onDeviceFound(const NimBLEAddress& addr);
    void onScanDevice(const NimBLEAdvertisedDevice* device);
    void onScanFinished(bool foundTarget, int reason);

private:
    void setStatus(BleStatus status, const char* reason = nullptr);
    void onConnect(NimBLEClient* client) override;
    void onConnectFail(NimBLEClient* client, int reason) override;
    void onDisconnect(NimBLEClient* client, int reason) override;
    void startScan();
    bool connectAndSetup();
    void saveAddress();
    bool loadAddress();
    bool requestStaticFields();
    void runHandshake();
    void pollData();
    bool writeCmd(const uint8_t* data, size_t len);
    void resetFrameBuffer();
    void appendNotifyData(const uint8_t* data, size_t len);
    void processFrameBuffer();

    static void notifyCallback(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify);

    static BleClient* _instance;

    NimBLEClient* _client = nullptr;
    NimBLERemoteCharacteristic* _rxChar = nullptr;

    BikeState _state;
    static constexpr size_t FRAME_BUFFER_SIZE = 512;
    uint8_t _buffer[FRAME_BUFFER_SIZE] = {0};
    size_t _bufferLen = 0;
    BleStatus _status = BleStatus::IDLE;
    StateChangeCallback _callback = nullptr;

    NimBLEAddress _targetAddr{};
    volatile bool _deviceFound = false;
    char _deviceName[32] = "CGO600";
    unsigned long _pollInterval = 1000;
    unsigned long _lastPoll = 0;
    unsigned long _lastStaticRequest = 0;
    unsigned long _lastNotify = 0;
    unsigned long _lastNotifyWarning = 0;
    unsigned long _lastReconnect = 0;
    unsigned long _reconnectDelay = 2000;
    unsigned long _lastScanStartedMs = 0;
    unsigned long _lastScanEndedMs = 0;
    bool _hasStoredAddr = false;
    bool _lastScanStartOk = false;
    int _lastScanEndReason = 0;
    int _lastConnectError = 0;
    int _lastDisconnectReason = 0;
    bool _lastTargetConnectable = false;
    unsigned long _lastSeenMs = 0;
    uint32_t _seenConnectableCount = 0;
    uint32_t _seenNonConnectableCount = 0;
    uint32_t _connectAttemptCount = 0;
    uint32_t _connectFailCount = 0;
};

extern BleClient bleClient;
