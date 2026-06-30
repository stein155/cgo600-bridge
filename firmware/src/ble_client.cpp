#include "ble_client.h"
#include "protocol.h"
#include <NimBLEUtils.h>
#include <WiFi.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include "logger.h"

// NUS UUIDs — identical to Python connection.py
static const NimBLEUUID NUS_SERVICE_UUID("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static const NimBLEUUID NUS_RX_UUID("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static const NimBLEUUID NUS_TX_UUID("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
static constexpr unsigned long DISCONNECTED_SCAN_RETRY_MS = 200;
static constexpr unsigned long DIRECT_RETRY_WHEN_SEEN_MS = 200;  // retry scan fast after failed connect
static constexpr unsigned long TARGET_SCAN_RESTART_MS = 100;
static constexpr unsigned long NOTIFY_STALE_WARNING_MS = 5000;
static constexpr uint8_t CONNECT_MAX_ATTEMPTS = 1;    // fail fast, re-scan immediately
static constexpr uint32_t CONNECT_TIMEOUT_MS = 3000;  // NimBLE v2 setConnectTimeout() takes ms

#ifndef BLE_GAP_DEBUG
#define BLE_GAP_DEBUG 0
#endif

BleClient bleClient;
BleClient* BleClient::_instance = nullptr;

// Set to true once WiFi is up. Guards restoreWifiAfterBle() from being called
// before WiFi is initialized (BLE scan may start before WiFi connects).
static bool s_wifiReady = false;

static const char* ownAddrTypeToStr(uint8_t type) {
    switch (type) {
        case BLE_OWN_ADDR_PUBLIC: return "public";
        case BLE_OWN_ADDR_RANDOM: return "random";
        case BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT: return "rpa-public";
        case BLE_OWN_ADDR_RPA_RANDOM_DEFAULT: return "rpa-random";
        default: return "unknown";
    }
}

static int gapDebugHandler(ble_gap_event* event, void* arg) {
    (void)arg;

#if BLE_GAP_DEBUG
    LOGF("[BLE][GAP] event=%s(%u)\n", NimBLEUtils::gapEventToString(event->type), event->type);

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            LOGF("[BLE][GAP]   connect status=%d (%s) handle=%u\n",
                event->connect.status,
                NimBLEUtils::returnCodeToString(event->connect.status),
                event->connect.conn_handle);
            break;

        case BLE_GAP_EVENT_LINK_ESTAB:
            LOGF("[BLE][GAP]   link_estab status=%d (%s) handle=%u\n",
                event->link_estab.status,
                NimBLEUtils::returnCodeToString(event->link_estab.status),
                event->link_estab.conn_handle);
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            LOGF("[BLE][GAP]   disconnect reason=%d (%s) handle=%u\n",
                event->disconnect.reason,
                NimBLEUtils::returnCodeToString(event->disconnect.reason),
                event->disconnect.conn.conn_handle);
            break;

        case BLE_GAP_EVENT_TERM_FAILURE:
            LOGF("[BLE][GAP]   term_failure status=%d (%s) handle=%u\n",
                event->term_failure.status,
                NimBLEUtils::returnCodeToString(event->term_failure.status),
                event->term_failure.conn_handle);
            break;

        case BLE_GAP_EVENT_CONN_UPDATE:
            LOGF("[BLE][GAP]   conn_update status=%d (%s) handle=%u\n",
                event->conn_update.status,
                NimBLEUtils::returnCodeToString(event->conn_update.status),
                event->conn_update.conn_handle);
            break;

        case BLE_GAP_EVENT_CONN_UPDATE_REQ:
            LOGF("[BLE][GAP]   conn_update_req min=%u max=%u latency=%u timeout=%u handle=%u\n",
                event->conn_update_req.peer_params->itvl_min,
                event->conn_update_req.peer_params->itvl_max,
                event->conn_update_req.peer_params->latency,
                event->conn_update_req.peer_params->supervision_timeout,
                event->conn_update_req.conn_handle);
            break;

        case BLE_GAP_EVENT_DISC_COMPLETE:
            LOGF("[BLE][GAP]   disc_complete reason=%d (%s)\n",
                event->disc_complete.reason,
                NimBLEUtils::returnCodeToString(event->disc_complete.reason));
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            LOGF("[BLE][GAP]   enc_change status=%d (%s) handle=%u\n",
                event->enc_change.status,
                NimBLEUtils::returnCodeToString(event->enc_change.status),
                event->enc_change.conn_handle);
            break;

        case BLE_GAP_EVENT_IDENTITY_RESOLVED:
            LOGF("[BLE][GAP]   identity_resolved handle=%u\n",
                event->identity_resolved.conn_handle);
            break;

        case BLE_GAP_EVENT_MTU:
            LOGF("[BLE][GAP]   mtu value=%u handle=%u\n",
                event->mtu.value,
                event->mtu.conn_handle);
            break;

        default:
            break;
    }

#else
    (void)event;
#endif

    return 0;
}

// ── Scan callbacks ──────────────────────────────────────────────────────────

class ScanCB : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice* device) override {
        bleClient.onScanDevice(device);
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        LOGF("[BLE] Scan ended (reason=%d, found=%d devices)\n", reason, results.getCount());
        bleClient.onScanFinished(bleClient.deviceFound(), reason);
    }
};

static ScanCB scanCB;  // Single persistent instance

static void restoreWifiAfterBle() {
    if (!s_wifiReady) {
        return;  // WiFi not yet initialized — don't touch it
    }
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }
    LOGLN("[BLE] WiFi not connected after BLE activity, requesting reconnect...");
    WiFi.mode(WIFI_STA);
    WiFi.reconnect();
}

static bool namesMatch(const char* candidate, const char* targetName) {
    if (!candidate || !candidate[0]) return false;
    if (!targetName || !targetName[0]) return false;

    const size_t candidateLen = strlen(candidate);
    const size_t targetLen = strlen(targetName);
    if (targetLen > candidateLen) {
        return false;
    }

    for (size_t i = 0; i + targetLen <= candidateLen; ++i) {
        bool match = true;
        for (size_t j = 0; j < targetLen; ++j) {
            const char a = (char)tolower((unsigned char)candidate[i + j]);
            const char b = (char)tolower((unsigned char)targetName[j]);
            if (a != b) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }

    return false;
}

static bool advertisesNus(const NimBLEAdvertisedDevice* device) {
    if (!device) {
        return false;
    }

    for (uint8_t i = 0; i < device->getServiceUUIDCount(); ++i) {
        if (device->getServiceUUID(i).equals(NUS_SERVICE_UUID)) {
            return true;
        }
    }

    return false;
}

static const char* statusToStr(BleStatus status) {
    switch (status) {
        case BleStatus::IDLE:         return "idle";
        case BleStatus::SCANNING:     return "scanning";
        case BleStatus::CONNECTING:   return "connecting";
        case BleStatus::HANDSHAKE:    return "handshake";
        case BleStatus::POLLING:      return "connected";
        case BleStatus::DISCONNECTED: return "disconnected";
    }
    return "unknown";
}

// ── BleClient ───────────────────────────────────────────────────────────────

void BleClient::begin(const char* deviceName) {
    _instance = this;
    strncpy(_deviceName, deviceName, sizeof(_deviceName) - 1);
    _deviceName[sizeof(_deviceName) - 1] = '\0';

    NimBLEDevice::init("CGO600-Bridge");
    if (!NimBLEDevice::setCustomGapHandler(gapDebugHandler)) {
        LOGLN("[BLE] Failed to install GAP debug handler");
    }
    const uint8_t ownAddrType = BLE_OWN_ADDR_PUBLIC;
    if (!NimBLEDevice::setOwnAddrType(ownAddrType)) {
        LOGF("[BLE] Failed to set own addr type to %s\n", ownAddrTypeToStr(ownAddrType));
    } else {
        LOGF("[BLE] Own addr type: %s\n", ownAddrTypeToStr(ownAddrType));
    }
    NimBLEDevice::setPower(9);  // max power
    NimBLEDevice::setMTU(185);

    // Configure scan once.
    // interval=160 (100ms), window=156 (97.5ms) → near-100% duty cycle.
    // The small gap (2.5ms) lets the coexistence controller schedule WiFi
    // without starving it entirely, while keeping BLE coverage near 100%.
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCB, false);
    scan->setActiveScan(true);
    scan->setMaxResults(0);
    scan->setInterval(160);
    scan->setWindow(156);

    _hasStoredAddr = loadAddress();

    LOGLN("[BLE] Initialized");
    _reconnectDelay = TARGET_SCAN_RESTART_MS;
    _lastReconnect = millis() - _reconnectDelay;
    setStatus(BleStatus::DISCONNECTED, _hasStoredAddr ? "restored paired device" : "auto search enabled");
    startScan();
}

void BleClient::setStatus(BleStatus status, const char* reason) {
    if (_status == status) {
        return;
    }

    if (reason && reason[0]) {
        LOGF("[BLE] Status %s -> %s (%s)\n", statusToStr(_status), statusToStr(status), reason);
    } else {
        LOGF("[BLE] Status %s -> %s\n", statusToStr(_status), statusToStr(status));
    }
    _status = status;
}

void BleClient::setWifiReady(bool ready) {
    s_wifiReady = ready;
}

bool BleClient::enable() {
    if (_client && _client->isConnected()) {
        _client->disconnect();
    }
    saveAddress();         // saves the "paired" bool flag in NVS
    _hasStoredAddr = true;
    _deviceFound = false;
    _reconnectDelay = DISCONNECTED_SCAN_RETRY_MS;
    _lastReconnect = millis() - _reconnectDelay;
    setStatus(BleStatus::DISCONNECTED, "enabled");
    LOGLN("[BLE] Enabled — will search for bike by name or NUS service");
    return true;
}

void BleClient::forgetPairedDevice() {
    if (_client && _client->isConnected()) {
        _client->disconnect();
    }

    Preferences prefs;
    prefs.begin("ble", false);
    prefs.remove("paired");
    prefs.remove("addr");      // legacy key, clean up if present
    prefs.remove("addrType");  // legacy key, clean up if present
    prefs.end();

    _hasStoredAddr = false;
    _targetAddr = NimBLEAddress();
    _deviceFound = false;
    setStatus(BleStatus::IDLE, "paired device forgotten");
    _lastReconnect = millis();
    LOGLN("[BLE] Forgot paired bike");
}

void BleClient::onScanDevice(const NimBLEAdvertisedDevice* device) {
    std::string name = device->getName();
    const bool hasName = !name.empty();
    const bool nameMatches = hasName && namesMatch(name.c_str(), _deviceName);
    const bool nusMatches = advertisesNus(device);

    // Match by advertised name first. If the bike omits its name, NUS service
    // still identifies the UART bridge used by the Tenways controller.
    if (!nameMatches && !nusMatches) {
        return;
    }

    const NimBLEAddress addr = device->getAddress();
    _lastTargetConnectable = device->isConnectable();
    _lastSeenMs = millis();
    if (!_lastTargetConnectable) {
        ++_seenNonConnectableCount;
        LOGF("[BLE] Matched non-connectable target: name='%s' nus=%s [%s]\n",
            hasName ? name.c_str() : "",
            nusMatches ? "yes" : "no",
            addr.toString().c_str());
        return;
    }

    ++_seenConnectableCount;
    LOGF("[BLE] Matched connectable target: name='%s' nus=%s [%s]\n",
        hasName ? name.c_str() : "",
        nusMatches ? "yes" : "no",
        addr.toString().c_str());
    onDeviceFound(addr);
    // Do NOT call scan->stop() from within this callback — it runs on
    // the NimBLE host task and stop() may deadlock if called there.
    // The main loop detects _deviceFound and calls stop() safely.
}

void BleClient::onScanFinished(bool foundTarget, int reason) {
    _lastScanEndedMs = millis();
    _lastScanEndReason = reason;
    restoreWifiAfterBle();

    // If we're already connecting or connected, the main loop picked up the
    // device and started a connection before this callback fired. Don't
    // override that state — leave the connecting flow undisturbed.
    if (_status == BleStatus::CONNECTING ||
        _status == BleStatus::HANDSHAKE  ||
        _status == BleStatus::POLLING) {
        LOGF("[BLE] Scan ended (reason=%d) while status=%s — not resetting\n",
            reason, statusToStr(_status));
        return;
    }

    if (!foundTarget) {
        LOGLN("[BLE] Scan complete, device not found");
        _lastReconnect = millis();
        _reconnectDelay = TARGET_SCAN_RESTART_MS;
        setStatus(BleStatus::DISCONNECTED, "scan complete, target missing");
    }
}

void BleClient::onDeviceFound(const NimBLEAddress& addr) {
    _targetAddr = addr;
    _deviceFound = true;
}

void BleClient::loop() {
    unsigned long now = millis();

    switch (_status) {
        case BleStatus::IDLE:
        case BleStatus::DISCONNECTED:
            {
                NimBLEScan* scan = NimBLEDevice::getScan();
                if (!scan->isScanning() && now - _lastReconnect >= _reconnectDelay) {
                    startScan();
                }
                break;
            }

        case BleStatus::SCANNING: {
            NimBLEScan* scan = NimBLEDevice::getScan();
            if (_deviceFound) {
                // NimBLE-Arduino v2 startConnectionAttempt() already handles
                // BLE_HS_EBUSY (scan active) by stopping the scan internally
                // and retrying connect — no need to stop manually.
                LOGF("[BLE] Device found — connecting (scanActive=%s)\n",
                    scan->isScanning() ? "yes" : "no");
                _deviceFound = false;
                _reconnectDelay = DISCONNECTED_SCAN_RETRY_MS;
                setStatus(BleStatus::CONNECTING, "target found, connecting");
                if (!connectAndSetup()) {
                    setStatus(BleStatus::DISCONNECTED, "connect after scan failed");
                    _lastReconnect = millis();
                    _reconnectDelay = DIRECT_RETRY_WHEN_SEEN_MS;
                }
                break;
            } else if (!scan->isScanning()) {
                LOGLN("[BLE] Target scan stopped, scheduling restart");
                setStatus(BleStatus::DISCONNECTED, "scan stopped without match");
                _lastReconnect = millis();
                _reconnectDelay = TARGET_SCAN_RESTART_MS;
                break;
            }
            break;
        }

        case BleStatus::CONNECTING:
        case BleStatus::HANDSHAKE:
            break;

        case BleStatus::POLLING:
            if (_client && _client->isConnected()) {
                if (!_state.last_connected_at[0] && _state.setLastConnectedNow()) {
                    LOGF("[BLE] Last connected at %s\n", _state.last_connected_at);
                    if (_callback) {
                        _callback(_state);
                    }
                }
                if (now - _lastPoll >= _pollInterval) {
                    pollData();
                    _lastPoll = now;
                }
                if (_lastNotify && now - _lastNotify >= NOTIFY_STALE_WARNING_MS && now - _lastNotifyWarning >= NOTIFY_STALE_WARNING_MS) {
                    _lastNotifyWarning = now;
                    LOGF("[BLE] Warning: no notify for %lu ms while connected\n", now - _lastNotify);
                }
            } else {
                LOGLN("[BLE] Connection lost");
                setStatus(BleStatus::DISCONNECTED, "client lost while polling");
                _lastReconnect = now;
                _reconnectDelay = DISCONNECTED_SCAN_RETRY_MS;
            }
            break;
    }
}

bool BleClient::loadAddress() {
    // We no longer persist the MAC address — only a "paired" flag.
    // Matching is always done by device name, so any address works.
    Preferences prefs;
    prefs.begin("ble", true);
    const bool paired = prefs.getBool("paired", false);
    prefs.end();
    LOGLN(paired
        ? "[BLE] Paired device flag found — auto scan enabled"
        : "[BLE] No paired flag — auto scan enabled");
    return paired;
}

void BleClient::saveAddress() {
    // Only persist the paired flag, not the MAC address.
    Preferences prefs;
    prefs.begin("ble", false);
    prefs.putBool("paired", true);
    prefs.end();
    _hasStoredAddr = true;
    LOGLN("[BLE] Paired flag saved");
}

void BleClient::startScan() {
    if (_client && !_client->isConnected()) {
        NimBLEDevice::deleteClient(_client);
        _client = nullptr;
        _rxChar = nullptr;
    }

    LOGLN("[BLE] Scanning for bike by name or NUS service...");
    setStatus(BleStatus::SCANNING, "target scan start");
    _deviceFound = false;
    _lastStaticRequest = 0;
    resetFrameBuffer();

    LOGLN("[BLE] Starting scan with WiFi left enabled");

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->stop();
    scan->clearResults();
    _lastScanStartedMs = millis();
    _lastScanStartOk = false;
    _lastScanStartOk = scan->start(0, false, true);
    if (!_lastScanStartOk) {
        LOGLN("[BLE] Failed to start continuous scan");
    }
}

bool BleClient::connectAndSetup() {
    LOGF("[BLE] Connecting to %s (peerType=%u) ownAddr=%s scanActive=%s...\n",
        _targetAddr.toString().c_str(), _targetAddr.getType(),
        ownAddrTypeToStr(BLE_OWN_ADDR_PUBLIC),
        NimBLEDevice::getScan()->isScanning() ? "yes" : "no");

    // Clean up old client
    if (_client) {
        NimBLEDevice::deleteClient(_client);
        _client = nullptr;
        _rxChar = nullptr;
    }

    _lastNotify = 0;
    _lastNotifyWarning = 0;
    _lastConnectError = 0;
    resetFrameBuffer();

    _client = NimBLEDevice::createClient();
    _client->setClientCallbacks(this, false);
    _client->setConnectTimeout(CONNECT_TIMEOUT_MS);

    LOGLN("[BLE] Starting connect with WiFi left enabled");

    bool connected = false;
    for (uint8_t attempt = 1; attempt <= CONNECT_MAX_ATTEMPTS; ++attempt) {
        LOGF("[BLE] Connect attempt %u/%u (timeout=%lums)...\n",
            attempt,
            CONNECT_MAX_ATTEMPTS,
            CONNECT_TIMEOUT_MS);
        ++_connectAttemptCount;
        if (_client->connect(_targetAddr)) {
            connected = true;
            break;
        }
        const int rc = _client->getLastError();
        _lastConnectError = rc;
        ++_connectFailCount;
        LOGF("[BLE] Connect failed: rc=%d (%s)\n", rc, NimBLEUtils::returnCodeToString(rc));
    }

    restoreWifiAfterBle();

    if (!connected) {
        LOGLN("[BLE] All connect attempts failed");
        return false;
    }

    LOGLN("[BLE] Connected, discovering services...");

    // Find NUS service
    NimBLERemoteService* nus = _client->getService(NUS_SERVICE_UUID);
    if (!nus) {
        LOGLN("[BLE] NUS service not found!");
        // Print all services for debugging
        auto& services = _client->getServices(true);
        for (auto& svc : services) {
            LOGF("[BLE]   Service: %s\n", svc->getUUID().toString().c_str());
        }
        _client->disconnect();
        return false;
    }

    // Get characteristics
    NimBLERemoteCharacteristic* txChar = nus->getCharacteristic(NUS_TX_UUID);
    _rxChar = nus->getCharacteristic(NUS_RX_UUID);

    if (!txChar || !_rxChar) {
        LOGLN("[BLE] NUS characteristics not found!");
        if (txChar) LOGLN("[BLE]   TX: found");
        else LOGLN("[BLE]   TX: MISSING");
        if (_rxChar) LOGLN("[BLE]   RX: found");
        else LOGLN("[BLE]   RX: MISSING");
        _client->disconnect();
        return false;
    }

    LOGF("[BLE] TX char found, RX char found\n");

    // Subscribe to TX notifications
    if (!txChar->subscribe(true, notifyCallback)) {
        LOGLN("[BLE] Subscribe failed!");
        _client->disconnect();
        return false;
    }

    LOGLN("[BLE] Subscribed, starting handshake...");
    setStatus(BleStatus::HANDSHAKE, "notifications subscribed");
    resetFrameBuffer();
    _lastStaticRequest = 0;
    runHandshake();
    return _status == BleStatus::POLLING;
}

void BleClient::resetFrameBuffer() {
    _bufferLen = 0;
}

void BleClient::appendNotifyData(const uint8_t* data, size_t len) {
    if (!data || len == 0) {
        return;
    }

    if (len >= FRAME_BUFFER_SIZE) {
        memcpy(_buffer, data + (len - FRAME_BUFFER_SIZE), FRAME_BUFFER_SIZE);
        _bufferLen = FRAME_BUFFER_SIZE;
        LOGLN("[BLE] Notify frame buffer replaced after oversized packet");
        return;
    }

    const size_t freeSpace = FRAME_BUFFER_SIZE - _bufferLen;
    if (len > freeSpace) {
        const size_t drop = len - freeSpace;
        if (drop >= _bufferLen) {
            _bufferLen = 0;
        } else {
            memmove(_buffer, _buffer + drop, _bufferLen - drop);
            _bufferLen -= drop;
        }
        LOGLN("[BLE] Notify frame buffer trimmed to avoid overflow");
    }

    memcpy(_buffer + _bufferLen, data, len);
    _bufferLen += len;
}

void BleClient::processFrameBuffer() {
    bool changed = false;
    bool sawTrip = false;

    while (_bufferLen >= 6) {
        size_t start = 0;
        while (start < _bufferLen && _buffer[start] != FRAME_START) {
            ++start;
        }

        if (start >= _bufferLen) {
            _bufferLen = 0;
            break;
        }

        if (start > 0) {
            memmove(_buffer, _buffer + start, _bufferLen - start);
            _bufferLen -= start;
        }

        if (_bufferLen < 2) {
            break;
        }

        const uint8_t payloadLen = _buffer[1];
        const size_t frameLen = 2 + payloadLen + 2 + 1;
        if (_bufferLen < frameLen) {
            break;
        }

        if (_buffer[frameLen - 1] == FRAME_END) {
            const uint16_t expectedCrc = ((uint16_t)_buffer[2 + payloadLen] << 8) |
                                         (uint16_t)_buffer[3 + payloadLen];
            const uint16_t actualCrc = crc16(_buffer, 2 + payloadLen);
            if (actualCrc != expectedCrc) {
                LOGF("[BLE] Dropping frame with bad CRC actual=0x%04X expected=0x%04X len=%u\n",
                    actualCrc, expectedCrc, frameLen);
            } else {
                size_t pos = 2;
                const size_t payloadEnd = 2 + payloadLen;
                while (pos + 2 <= payloadEnd) {
                    const uint8_t fieldId = _buffer[pos++];
                    const uint8_t fieldLen = _buffer[pos++];
                    if (pos + fieldLen > payloadEnd) {
                        break;
                    }
                    if (fieldId == FIELD_TRIP_DISTANCE || fieldId == FIELD_TRIP_DURATION) {
                        sawTrip = true;
                    }
                    if (_state.updateField(fieldId, _buffer + pos, fieldLen)) {
                        changed = true;
                    }
                    pos += fieldLen;
                }
            }
        }

        if (frameLen < _bufferLen) {
            memmove(_buffer, _buffer + frameLen, _bufferLen - frameLen);
        }
        _bufferLen -= frameLen;
    }

    // A frame carrying the trip counters is a confirmed current odometer
    // reading: stamp the ride-log time anchor so it always matches this value.
    if (sawTrip) {
        _state.setLastDataNow();
    }

    if ((changed || sawTrip) && _callback) {
        _callback(_state);
    }
}

bool BleClient::requestStaticFields() {
    uint8_t extraFields[] = {
        FIELD_SN,
    };
    uint8_t extraCmd[20];
    size_t extraLen = buildRequest(extraCmd, extraFields, sizeof(extraFields));

    LOGF("[BLE] TX extended telemetry request (%d bytes)\n", extraLen);
    const bool ok = writeCmd(extraCmd, extraLen);
    if (!ok) {
        LOGLN("[BLE] Extended telemetry request failed");
        return false;
    }

    _lastStaticRequest = millis();
    return true;
}

bool BleClient::setLight(bool on) {
    if (_status != BleStatus::POLLING || !_client || !_client->isConnected()) {
        LOGLN("[BLE] Light write rejected: bike not connected");
        return false;
    }

    const uint8_t* cmd = on ? LIGHT_ON_CMD : LIGHT_OFF_CMD;
    const size_t len = on ? LIGHT_ON_CMD_LEN : LIGHT_OFF_CMD_LEN;

    LOGF("[BLE] TX light %s (%d bytes)\n", on ? "on" : "off", len);
    if (!writeCmd(cmd, len)) {
        LOGLN("[BLE] Light write failed");
        return false;
    }

    _lastPoll = 0;
    pollData();
    return true;
}

bool BleClient::writeCmd(const uint8_t* data, size_t len) {
    if (!_rxChar || !_client || !_client->isConnected()) return false;

    // Try without-response first (like the Tenways app), fallback to with-response
    if (_rxChar->writeValue(data, len, false)) return true;
    LOGLN("[BLE] Write no-resp failed, trying with-resp...");
    return _rxChar->writeValue(data, len, true);
}

void BleClient::runHandshake() {
    // Exact sequence from Python connection.py Session._session_loop
    delay(50);  // settle after subscribe

    LOGF("[BLE] TX init (%d bytes)\n", READ_INIT_CMD_LEN);
    if (!writeCmd(READ_INIT_CMD, READ_INIT_CMD_LEN)) {
        LOGLN("[BLE] Init write failed");
        if (_client) _client->disconnect();
        setStatus(BleStatus::DISCONNECTED, "init write failed");
        return;
    }

    delay(120);
    LOGF("[BLE] TX data cmd (%d bytes)\n", READ_DATA_CMD_LEN);
    if (!writeCmd(READ_DATA_CMD, READ_DATA_CMD_LEN)) {
        LOGLN("[BLE] Data cmd write failed");
        if (_client) _client->disconnect();
        setStatus(BleStatus::DISCONNECTED, "data cmd write failed");
        return;
    }

    delay(120);
    LOGF("[BLE] TX bootstrap 2 (%d bytes)\n", BOOTSTRAP_CMD_2_LEN);
    if (!writeCmd(BOOTSTRAP_CMD_2, BOOTSTRAP_CMD_2_LEN)) {
        LOGLN("[BLE] Bootstrap 2 write failed");
        if (_client) _client->disconnect();
        setStatus(BleStatus::DISCONNECTED, "bootstrap 2 write failed");
        return;
    }

    delay(120);
    LOGF("[BLE] TX bootstrap 3 (%d bytes)\n", BOOTSTRAP_CMD_3_LEN);
    if (!writeCmd(BOOTSTRAP_CMD_3, BOOTSTRAP_CMD_3_LEN)) {
        LOGLN("[BLE] Bootstrap 3 write failed");
        if (_client) _client->disconnect();
        setStatus(BleStatus::DISCONNECTED, "bootstrap 3 write failed");
        return;
    }

    // Serial/range sometimes arrive later than the initial bootstrap, so keep
    // this request reusable and retry from the poll loop until both are filled.
    delay(120);
    requestStaticFields();

    LOGLN("[BLE] Handshake done, polling");
    setStatus(BleStatus::POLLING, "handshake complete");
    _lastPoll = millis();
    _lastNotify = millis();
    _lastNotifyWarning = 0;
    _reconnectDelay = DISCONNECTED_SCAN_RETRY_MS;

    if (_state.setLastConnectedNow()) {
        LOGF("[BLE] Last connected at %s\n", _state.last_connected_at);
    } else {
        LOGLN("[BLE] Last connected timestamp unavailable (waiting for NTP)");
    }

    if (_callback) {
        _callback(_state);
    }
}

void BleClient::pollData() {
    if (!writeCmd(READ_DATA_CMD, READ_DATA_CMD_LEN)) {
        LOGLN("[BLE] Poll write failed");
        return;
    }

    if (millis() - _lastStaticRequest >= 15000) {
        requestStaticFields();
    }
}

void BleClient::notifyCallback(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify) {
    (void)chr;
    (void)isNotify;
    if (!_instance) return;
    BleClient* self = _instance;

    self->_lastNotify = millis();
    self->_lastNotifyWarning = 0;

    self->appendNotifyData(data, len);
    self->processFrameBuffer();
}

void BleClient::onConnect(NimBLEClient* client) {
    LOGF("[BLE] Connect event: %s\n", client->getPeerAddress().toString().c_str());
}

void BleClient::onConnectFail(NimBLEClient* client, int reason) {
    _lastConnectError = reason;
    LOGF("[BLE] Connect fail callback: reason=%d (%s)\n", reason, NimBLEUtils::returnCodeToString(reason));
}

void BleClient::onDisconnect(NimBLEClient* client, int reason) {
    const unsigned long sinceNotify = _lastNotify ? millis() - _lastNotify : 0;
    _lastDisconnectReason = reason;
    LOGF("[BLE] Disconnected (reason=%d, since_notify=%lu ms)\n", reason, sinceNotify);
    setStatus(BleStatus::DISCONNECTED, "disconnect callback");
    _lastReconnect = millis();
    _rxChar = nullptr;
}

const char* BleClient::getStatusStr() const {
    switch (_status) {
        case BleStatus::IDLE:         return "searching";
        case BleStatus::SCANNING:     return "searching";
        case BleStatus::CONNECTING:   return "connecting";
        case BleStatus::HANDSHAKE:    return "handshake";
        case BleStatus::POLLING:      return "connected";
        case BleStatus::DISCONNECTED: return "searching";
    }
    return "unknown";
}
