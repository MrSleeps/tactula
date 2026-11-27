// Tactula JC8012P4A1C Wi-Fi onboarding: P4 hosts UI; ESP32-C6 is the radio.
// Matches S3 setup SSID/password and credentials stored under touchdeck/ssid,pass.
// Never flashes or resets the C6 firmware independently.
#include "p4_wifi_setup.h"
#include "tactula_setup_view.h"
#include "web_assets.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_wifi.h>
#include <cstring>
// Version-only ESP-Hosted RPC. Do not perform co-processor OTA/flash here.
#if __has_include("esp_hosted.h")
#include "esp_hosted.h"
#define TACTULA_P4_HAS_HOSTED_VERSION_QUERY 1
#else
#define TACTULA_P4_HAS_HOSTED_VERSION_QUERY 0
#endif
#if __has_include("esp_hosted_host_fw_ver.h")
#include "esp_hosted_host_fw_ver.h"
#endif

namespace {
enum class State : uint8_t { STARTING, AP, CONNECTING, CONNECTED, RADIO_ERROR };
State state = State::STARTING;
WebServer server(80);
DNSServer dns;
bool serverStarted = false;
bool handlersRegistered = false;
bool dnsStarted = false;
bool apActive = false;
String apSsid, savedSsid, pendingSsid, pendingPass, currentIp;
uint32_t attemptAt = 0, lastStatusAt = 0;
String lastFailure;
volatile uint8_t lastDisconnectReason = 0;
volatile uint32_t staConnectedEvents = 0;
volatile uint32_t staGotIpEvents = 0;
uint8_t loggedDisconnectReason = 0;
uint32_t loggedConnectedEvents = 0;
uint32_t loggedGotIpEvents = 0;
bool wifiEventsRegistered = false;
uint32_t portalCloseAt = 0;
// Diagnostic opt-in: remove the setup AP before trying STA to isolate
// ESP-Hosted AP+STA coexistence. Never switch inside an HTTP callback.
uint32_t staOnlySwitchAt = 0;
bool staOnlyPending = false;
// On a cold P4 boot the hosted C6 may not yet accept a station connection.
// Do not run WiFi.begin synchronously in setup() immediately after GPIO54
// releases the radio. Stage STA mode and the first connect from loop().
bool bootConnectPending = false;
uint32_t bootConnectAt = 0;
uint8_t bootConnectAttempts = 0;
bool bootConnect = false;
bool hostedVersionChecked = false;
void checkHostedVersions() {
    if (hostedVersionChecked) return;
    hostedVersionChecked = true;
#if defined(ESP_HOSTED_VERSION_MAJOR_1) && defined(ESP_HOSTED_VERSION_MINOR_1) && defined(ESP_HOSTED_VERSION_PATCH_1)
    Serial.printf("[P4 HOSTED] Host library version: %u.%u.%u\n",
                  unsigned(ESP_HOSTED_VERSION_MAJOR_1),
                  unsigned(ESP_HOSTED_VERSION_MINOR_1),
                  unsigned(ESP_HOSTED_VERSION_PATCH_1));
#else
    Serial.println("[P4 HOSTED] Host library version not exposed by this build");
#endif
#if TACTULA_P4_HAS_HOSTED_VERSION_QUERY
    esp_hosted_coprocessor_fwver_t version = {};
    const int result = esp_hosted_get_coprocessor_fwversion(&version);
    if (result == ESP_OK) {
        Serial.printf("[P4 HOSTED] C6 coprocessor firmware: %lu.%lu.%lu\n",
                      (unsigned long)version.major1,
                      (unsigned long)version.minor1,
                      (unsigned long)version.patch1);
    } else {
        Serial.printf("[P4 HOSTED] C6 version query failed: %s (%d)\n",
                      esp_err_to_name(static_cast<esp_err_t>(result)), result);
    }
#else
    Serial.println("[P4 HOSTED] No compatible version-query header in this build");
#endif
}

// Capture the actual Arduino Wi-Fi event sequence without doing Serial I/O
// from the Wi-Fi event task. Log it later from the main loop, preserving
// timestamps and ordering. Passwords are never captured or printed.
struct WifiTraceEvent {
    uint32_t atMs;
    int32_t eventId;
    uint8_t reason;
};
QueueHandle_t wifiTraceQueue = nullptr;
uint32_t wifiTraceDropped = 0;
void captureWifiTrace(WiFiEvent_t eventId, WiFiEventInfo_t info) {
    if (!wifiTraceQueue) return;
    WifiTraceEvent item{millis(), (int32_t)eventId, 0};
    if (eventId == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        item.reason = info.wifi_sta_disconnected.reason;
    }
    if (xQueueSend(wifiTraceQueue, &item, 0) != pdTRUE) {
        ++wifiTraceDropped;
    }
}
const char* wifiEventName(int32_t eventId) {
    switch (eventId) {
        case ARDUINO_EVENT_WIFI_READY: return "WIFI_READY";
        case ARDUINO_EVENT_WIFI_STA_START: return "STA_START";
        case ARDUINO_EVENT_WIFI_STA_STOP: return "STA_STOP";
        case ARDUINO_EVENT_WIFI_STA_CONNECTED: return "STA_CONNECTED";
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: return "STA_DISCONNECTED";
        case ARDUINO_EVENT_WIFI_STA_GOT_IP: return "STA_GOT_IP";
        case ARDUINO_EVENT_WIFI_STA_LOST_IP: return "STA_LOST_IP";
        case ARDUINO_EVENT_WIFI_AP_START: return "AP_START";
        case ARDUINO_EVENT_WIFI_AP_STOP: return "AP_STOP";
        default: return "OTHER";
    }
}
const char* reasonName(uint8_t reason) {
    switch (reason) {
        case 2: return "authentication expired";
        case 8: return "association leave (local disconnect or AP request)";
        case 15: return "four-way handshake timeout";
        case 200: return "beacon timeout";
        case 201: return "network not found";
        case 202: return "authentication failed";
        case 203: return "association failed";
        case 204: return "handshake timeout";
        case 205: return "connection failed";
        case 206: return "access point changed";
        default: return "see reason code";
    }
}
void registerWiFiEvents() {
    if (wifiEventsRegistered) return;
    wifiEventsRegistered = true;
    wifiTraceQueue = xQueueCreate(24, sizeof(WifiTraceEvent));
    if (!wifiTraceQueue) {
        Serial.println("[P4 WIFI TRACE] Unable to allocate event queue");
    }
    WiFi.onEvent(captureWifiTrace);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
        lastDisconnectReason = info.wifi_sta_disconnected.reason;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
        ++staConnectedEvents;
    }, ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
        ++staGotIpEvents;
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
}
void sendWiFiStatus() {
    server.sendHeader("Cache-Control", "no-store");
    const bool connected = state == State::CONNECTED || WiFi.status() == WL_CONNECTED;
    String json = "{\"state\":\"";
    json += connected ? "connected" : state == State::CONNECTING ? "connecting" :
            state == State::RADIO_ERROR ? "radio_error" : "setup";
    json += "\",\"connected\":";
    json += connected ? "true" : "false";
    json += ",\"reason\":" + String((unsigned)lastDisconnectReason);
    json += ",\"ip\":\"";
    json += connected ? WiFi.localIP().toString() : "";
    json += "\",\"message\":\"";
    // Fixed strings only: no credentials or user-supplied strings in JSON.
    json += connected ? "Connected to home WiFi." :
            state == State::CONNECTING ? "Connecting to home WiFi..." :
            state == State::RADIO_ERROR ? "The ESP32-C6 radio is unavailable." :
            lastFailure.length() ? "Connection failed. Check your WiFi details and try again." :
            "Ready to configure WiFi.";
    json += "\"}";
    server.send(200, "application/json", json);
}
lv_obj_t* setupScreen = nullptr;
lv_obj_t* connectedInfo = nullptr;
bool needsUiRefresh = true;

// Serve precisely the same Tactula visual assets as the S3 web portal.
// The bootstrap page is a dedicated WiFi-only entrypoint to the shared
// CSS/brand/components; hardware-specific routes stay here.
void serveCompressed(const uint8_t* bytes, size_t length, const char* mime) {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, mime, reinterpret_cast<PGM_P>(bytes), length);
}
void sendPortal() {
    serveCompressed(WEB_WIFI_SETUP_HTML, sizeof(WEB_WIFI_SETUP_HTML),
                    "text/html; charset=utf-8");
}
void sendPortalCss() {
    serveCompressed(WEB_PORTAL_CSS, sizeof(WEB_PORTAL_CSS), "text/css");
}
void sendBrandMark() {
    serveCompressed(WEB_TACTULA_MARK_SVG, sizeof(WEB_TACTULA_MARK_SVG),
                    "image/svg+xml");
}
void sendJson(int code, bool ok, const char* message) {
    // No user-supplied strings in JSON errors or success responses.
    String output = String("{\"success\":") + (ok ? "true" : "false")
                    + ",\"message\":\"" + message + "\"}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "application/json", output);
}
void redirectToPortal() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "Tactula Wi-Fi setup");
}
void startConnection(const String& ssid, const String& password) {
    pendingSsid = ssid;
    pendingPass = password;
    lastFailure = "";
    lastDisconnectReason = 0;
    loggedDisconnectReason = 0;
    // startPortal() has already put the C6 in AP+STA mode. Re-applying
    // WiFi.mode(AP_STA) during a live captive-portal POST can tear down
    // the hosted STA association and generate WIFI_REASON_ASSOC_LEAVE (8).
    // Only select STA mode on boot when there is no provisioning AP.
    const wifi_mode_t currentMode = WiFi.getMode();
    // The cold-boot path already selected STA mode during radio bring-up.
    // Do not reconfigure the hosted C6's mode immediately before begin().
    // Repeated mode/config changes could be responsible for the reason-8
    // ASSOC_LEAVE event (still to be verified on hardware).
    if (!apActive && !(currentMode & WIFI_MODE_STA)) {
        const bool modeOk = WiFi.mode(WIFI_STA);
        Serial.printf("[P4 WIFI] STA mode was missing; selected=%d\n", int(modeOk));
    } else {
        Serial.printf("[P4 WIFI] Reusing existing radio mode=%d, AP active=%d\n",
                      int(currentMode), int(apActive));
    }
    Serial.printf("[P4 WIFI] Trying home Wi-Fi: %s\n", ssid.c_str());
    Serial.printf("[P4 WIFI] Before WiFi.begin: t=%lu ms, mode=%d, status=%d\n",
                  (unsigned long)millis(), int(WiFi.getMode()), int(WiFi.status()));
    wifi_config_t stationCfg = {};
    const esp_err_t cfgResult = esp_wifi_get_config(WIFI_IF_STA, &stationCfg);
    Serial.printf("[P4 WIFI TRACE] station config: err=%s, ssid-present=%d, bssid-pinned=%d, channel=%u\n",
                  esp_err_to_name(cfgResult),
                  cfgResult == ESP_OK && stationCfg.sta.ssid[0] != 0,
                  cfgResult == ESP_OK && stationCfg.sta.bssid_set != 0,
                  unsigned(cfgResult == ESP_OK ? stationCfg.sta.channel : 0));
    WiFi.begin(ssid.c_str(), password.c_str());
    attemptAt = millis();
    if (bootConnect) ++bootConnectAttempts;
    Serial.printf("[P4 WIFI] WiFi.begin returned status=%d, AP active=%d, boot-attempt=%u\n",
                  int(WiFi.status()), int(apActive),
                  unsigned(bootConnect ? bootConnectAttempts : 0));
    state = State::CONNECTING;
    needsUiRefresh = true;
}
// Cold-boot isolation test: skip Arduino STA.connect's combined
// configure/connect path. The hosted interface is already started by
// WiFi.mode(WIFI_STA) in p4WifiBegin. Keep portal connections unchanged.
void startSavedConnection(const String& ssid, const String& password) {
    pendingSsid = ssid;
    pendingPass = password;
    lastFailure = "";
    lastDisconnectReason = 0;
    loggedDisconnectReason = 0;
    wifi_config_t config = {};
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63) {
        Serial.println("[P4 WIFI COLD] Invalid saved credentials; refusing association");
        state = State::RADIO_ERROR;
        needsUiRefresh = true;
        return;
    }
    memcpy(config.sta.ssid, ssid.c_str(), ssid.length());
    memcpy(config.sta.password, password.c_str(), password.length());
    config.sta.scan_method = WIFI_FAST_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    const esp_err_t configured = esp_wifi_set_config(WIFI_IF_STA, &config);
    Serial.printf("[P4 WIFI COLD] esp_wifi_set_config: %s (%d), t=%lu\n",
                  esp_err_to_name(configured), int(configured), (unsigned long)millis());
    if (configured != ESP_OK) {
        lastFailure = "Station configuration failed";
        state = State::RADIO_ERROR;
        needsUiRefresh = true;
        return;
    }
    wifi_config_t check = {};
    const esp_err_t verified = esp_wifi_get_config(WIFI_IF_STA, &check);
    Serial.printf("[P4 WIFI COLD] readback: %s, SSID matches=%d, channel=%u\n",
                  esp_err_to_name(verified),
                  int(verified == ESP_OK &&
                      memcmp(check.sta.ssid, config.sta.ssid, sizeof(config.sta.ssid)) == 0),
                  unsigned(verified == ESP_OK ? check.sta.channel : 0));
    const esp_err_t connectResult = esp_wifi_connect();
    attemptAt = millis();
    ++bootConnectAttempts;
    Serial.printf("[P4 WIFI COLD] esp_wifi_connect: %s (%d), boot-attempt=%u, t=%lu\n",
                  esp_err_to_name(connectResult), int(connectResult),
                  unsigned(bootConnectAttempts), (unsigned long)attemptAt);
    state = State::CONNECTING;
    needsUiRefresh = true;
}

void saveIfConnected() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("[P4 WIFI] ERROR: cannot open Tactula NVS settings namespace");
        return;
    }
    bool ok = prefs.putString("ssid", pendingSsid) == pendingSsid.length();
    ok = (prefs.putString("pass", pendingPass) == pendingPass.length()) && ok;
    prefs.end();
    if (!ok) Serial.println("[P4 WIFI] ERROR: credentials failed to save to NVS");
    else Serial.println("[P4 WIFI] Connected credentials saved using Tactula settings keys");
}
void acceptCredentials() {
    const String ssid = server.arg("ssid");
    const String pass = server.arg("pass");
    if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 64 ||
        (!pass.isEmpty() && pass.length() < 8)) {
        sendJson(400, false, "Network name or password is invalid.");
        return;
    }
    const bool diagnosticStaOnly = server.hasArg("sta_only") &&
                                  server.arg("sta_only") == "1";
    if (diagnosticStaOnly) {
        // HTTP ACK must reach the phone before its AP disappears.
        sendJson(200, true, "Diagnostic STA-only connection scheduled. Setup hotspot will briefly disappear.");
        pendingSsid = ssid;
        pendingPass = pass;
        lastFailure = "";
        lastDisconnectReason = 0;
        state = State::CONNECTING;
        needsUiRefresh = true;
        staOnlyPending = true;
        staOnlySwitchAt = millis() + 1800;
        Serial.println("[P4 WIFI] DIAGNOSTIC STA-only requested; setup AP will be paused");
    } else {
        sendJson(200, true, "Connecting to WiFi. Watch the Tactula screen for the result.");
        startConnection(ssid, pass);
    }
}
void startPortal() {
    if (!apSsid.length()) {
        char suffix[16];
        snprintf(suffix, sizeof(suffix), "%04x", unsigned(ESP.getEfuseMac() & 0xffff));
        apSsid = String(WIFI_AP_PREFIX) + suffix;
    }
    Serial.println("[P4 WIFI] Starting ESP32-C6 hosted SoftAP");
    if (!WiFi.mode(WIFI_AP_STA) || !WiFi.softAP(apSsid.c_str(), WIFI_AP_PASSWORD)) {
        Serial.println("[P4 WIFI] ERROR: cannot start hosted SoftAP. Check ESP32-C6 ESP-Hosted firmware / SDIO.");
        state = State::RADIO_ERROR;
        needsUiRefresh = true;
        return;
    }
    apActive = true;
    if (!handlersRegistered) {
        server.on("/", HTTP_GET, sendPortal);
        server.on("/portal.css", HTTP_GET, sendPortalCss);
        server.on("/tactula-mark.svg", HTTP_GET, sendBrandMark);
        server.on("/save/wifi", HTTP_POST, acceptCredentials);
        server.on("/api/wifi/status", HTTP_GET, sendWiFiStatus);
        server.on("/generate_204", HTTP_GET, redirectToPortal);
        server.on("/hotspot-detect.html", HTTP_GET, redirectToPortal);
        server.on("/connecttest.txt", HTTP_GET, redirectToPortal);
        server.onNotFound(redirectToPortal);
        handlersRegistered = true;
    }
    if (!serverStarted) {
        server.begin();
        serverStarted = true;
    }
    if (!dnsStarted) {
        dns.start(53, "*", WiFi.softAPIP());
        dnsStarted = true;
    }
    state = State::AP;
    needsUiRefresh = true;
    Serial.printf("[P4 WIFI] Tactula AP: %s, portal http://%s/\n",
                  apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}
void stopPortal() {
    if (serverStarted) { server.stop(); serverStarted = false; }
    if (dnsStarted) { dns.stop(); dnsStarted = false; }
    if (apActive) { WiFi.softAPdisconnect(true); apActive = false; }
    WiFi.mode(WIFI_STA);
}
void refreshUi() {
    if (!setupScreen) return;
    if (state == State::CONNECTED) {
        lv_obj_add_flag(setupScreen, LV_OBJ_FLAG_HIDDEN);
        if (connectedInfo) {
            const String message = "Wi-Fi connected  |  " + currentIp + "  |  " + String(WiFi.RSSI()) + " dBm";
            lv_label_set_text(connectedInfo, message.c_str());
        }
        return;
    }

    lv_obj_remove_flag(setupScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(setupScreen);
    if (state == State::CONNECTING) {
        // Exactly the S3 Tactula circular animation, from the shared
        // component. It is displayed throughout the AP->STA diagnostic
        // transition as well as on later boots with stored credentials.
        tactula_setup::buildConnecting(setupScreen, pendingSsid, true);
    } else if (apActive && state == State::AP) {
        tactula_setup::buildApSetup(
            setupScreen, true, apSsid, WIFI_AP_PASSWORD,
            WiFi.softAPIP().toString(), true);
        if (lastFailure.length()) {
            lv_obj_t* note = tactula_setup::text(
                setupScreen,
                "Connection failed. Check WiFi settings and try again.",
                &deck_font_20, 0xFF8A7A);
            lv_obj_set_style_pad_top(note, 22, 0);
        }
    } else {
        tactula_setup::text(setupScreen, "WiFi unavailable",
                            &lv_font_montserrat_40, tactula_setup::TITLE);
        tactula_setup::gap(setupScreen, 16);
        tactula_setup::text(setupScreen,
            "ESP32-C6 radio could not start. Check ESP-Hosted / SDIO logs.",
            &deck_font_20, tactula_setup::DIM);
    }
}

}  // namespace (private portal state and helpers)

void p4WifiBegin() {
    registerWiFiEvents();
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true)) {
        savedSsid = prefs.getString("ssid", "");
        const String password = prefs.getString("pass", "");
        prefs.end();
        if (!savedSsid.isEmpty() && savedSsid.length() <= 32) {
            // The working portal diagnostic connects only after the C6 has
            // been running for some time. Prime STA and wait asynchronously
            // before starting the saved network connection.
            pendingSsid = savedSsid;
            pendingPass = password;
            bootConnect = true;
            bootConnectAttempts = 0;
            const bool staOk = WiFi.mode(WIFI_STA);
            bootConnectPending = true;
            bootConnectAt = millis() + 2500;
            state = State::CONNECTING;
            needsUiRefresh = true;
            Serial.printf("[P4 WIFI] Cold-boot STA primed=%d; first connection in 2500 ms\n",
                          int(staOk));
        } else startPortal();
    } else {
        Serial.println("[P4 WIFI] Could not read settings NVS, starting setup AP");
        startPortal();
    }
}
bool p4WifiConnected() { return WiFi.status() == WL_CONNECTED; }

void p4WifiCreateUi(lv_obj_t* parent) {
    connectedInfo = lv_label_create(parent);
    lv_obj_set_width(connectedInfo, 1100);
    lv_obj_set_style_text_color(connectedInfo, lv_color_hex(tactula_setup::ACCENT), 0);
    lv_obj_set_style_text_align(connectedInfo, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(connectedInfo, "");
    lv_obj_align(connectedInfo, LV_ALIGN_BOTTOM_MID, 0, -18);

    // Same opaque, centred, column-flex system layer as deck_sys.cpp.
    setupScreen = lv_obj_create(parent);
    lv_obj_remove_style_all(setupScreen);
    lv_obj_set_size(setupScreen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(setupScreen, lv_color_hex(tactula_setup::BG), 0);
    lv_obj_set_style_bg_opa(setupScreen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(setupScreen, lv_color_hex(tactula_setup::TEXT), 0);
    lv_obj_set_flex_flow(setupScreen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(setupScreen, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(setupScreen, 12, 0);
    lv_obj_remove_flag(setupScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(setupScreen, LV_OBJ_FLAG_CLICKABLE);
    needsUiRefresh = true;
    refreshUi();
}
void p4WifiTick() {
    WifiTraceEvent trace;
    if (wifiTraceQueue) {
        while (xQueueReceive(wifiTraceQueue, &trace, 0) == pdTRUE) {
            Serial.printf("[P4 WIFI TRACE] t=%lu event=%s (%ld) reason=%u boot-attempt=%u\n",
                          (unsigned long)trace.atMs, wifiEventName(trace.eventId),
                          (long)trace.eventId, unsigned(trace.reason), unsigned(bootConnectAttempts));
        }
    }
    if (wifiTraceDropped) {
        Serial.printf("[P4 WIFI TRACE] dropped=%lu\n", (unsigned long)wifiTraceDropped);
        wifiTraceDropped = 0;
    }
    if (loggedConnectedEvents != staConnectedEvents) {
        loggedConnectedEvents = staConnectedEvents;
        Serial.printf("[P4 WIFI] STA association event #%lu, status=%d\n",
                      (unsigned long)loggedConnectedEvents, int(WiFi.status()));
    }
    if (loggedGotIpEvents != staGotIpEvents) {
        loggedGotIpEvents = staGotIpEvents;
        Serial.printf("[P4 WIFI] STA got IP event #%lu: %s\n",
                      (unsigned long)loggedGotIpEvents, WiFi.localIP().toString().c_str());
    }
    const uint8_t discReason = lastDisconnectReason;
    if (discReason && discReason != loggedDisconnectReason) {
        loggedDisconnectReason = discReason;
        Serial.printf("[P4 WIFI] STA disconnected: t=%lu ms, reason=%u (%s), status=%d, AP=%d, boot-attempt=%u\n",
            (unsigned long)millis(), (unsigned)discReason, reasonName(discReason),
            int(WiFi.status()), int(apActive), unsigned(bootConnectAttempts));
    }
    if (serverStarted) server.handleClient();
    if (dnsStarted) dns.processNextRequest();
    const uint32_t now = millis();
    if (bootConnectPending) {
        if ((int32_t)(now - bootConnectAt) >= 0) {
            checkHostedVersions();
            bootConnectPending = false;
            Serial.printf("[P4 WIFI] Cold-boot attempt %u after radio warm-up\n",
                          unsigned(bootConnectAttempts + 1));
            startSavedConnection(pendingSsid, pendingPass);
        }
        // Continue LVGL frame processing while the radio warms up.
        if (needsUiRefresh && now - lastStatusAt >= 200) {
            lastStatusAt = now;
            needsUiRefresh = false;
            refreshUi();
        }
        return;
    }
    if (staOnlyPending) {
        if ((int32_t)(now - staOnlySwitchAt) >= 0) {
            staOnlyPending = false;
            const String ssid = pendingSsid;
            const String password = pendingPass;
            Serial.println("[P4 WIFI] DIAGNOSTIC: disabling SoftAP and trying STA-only");
            stopPortal();
            Serial.printf("[P4 WIFI] DIAGNOSTIC: STA-only radio mode=%d\n", int(WiFi.getMode()));
            startConnection(ssid, password);
        }
        return;
    }
    if (state == State::CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[P4 WIFI] CONNECTED: %s RSSI=%d\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
            currentIp = WiFi.localIP().toString();
            saveIfConnected();
            // Keep setup AP available briefly so the phone can read SUCCESS
            // before the hosted C6 switches to STA-only.
            portalCloseAt = apActive ? millis() + 5000 : 0;
            savedSsid = pendingSsid;
            pendingSsid = "";
            pendingPass = "";
            lastFailure = "";
            bootConnect = false;
            bootConnectAttempts = 0;
            state = State::CONNECTED;
            needsUiRefresh = true;
        } else if (now - attemptAt > 20000) {
            Serial.printf("[P4 WIFI] Connection timed out: status=%d, reason=%u (%s), AP=%d\n",
                          int(WiFi.status()), unsigned(lastDisconnectReason), reasonName(lastDisconnectReason), int(apActive));
            if (bootConnect && bootConnectAttempts < 2) {
                // A fresh attempt after the ESP-Hosted radio has settled is
                // more informative than immediately abandoning saved WiFi.
                Serial.println("[P4 WIFI] Cold-boot STA retry scheduled in 2500 ms");
                bootConnectPending = true;
                bootConnectAt = now + 2500;
                return;
            }
            bootConnect = false;
            pendingSsid = "";
            pendingPass = "";
            lastFailure = "Connection failed";
            if (apActive) {
                // Stay in AP+STA and retain the captive portal so the user
                // can retry. WiFi.disconnect(false) here itself emits reason
                // 8 (ASSOC_LEAVE), masking the actual failure reason.
                state = State::AP;
                needsUiRefresh = true;
            } else {
                Serial.println("[P4 WIFI] DIAGNOSTIC: STA-only timed out; restoring Tactula setup AP");
                startPortal();
            }
        }
    } else if (state == State::CONNECTED && WiFi.status() != WL_CONNECTED) {
        portalCloseAt = 0;
        Serial.println("[P4 WIFI] Disconnected; retrying saved network");
        bootConnect = false;
        // Reload credentials before starting STA to avoid a blank-password attempt.
        Preferences prefs;
        String reconnectPass;
        if (prefs.begin(NVS_NAMESPACE, true)) {
            reconnectPass = prefs.getString("pass", "");
            prefs.end();
        }
        startConnection(savedSsid, reconnectPass);
    } else if (state == State::CONNECTED && portalCloseAt && (int32_t)(now - portalCloseAt) >= 0) {
        portalCloseAt = 0;
        stopPortal();
    }
    if (needsUiRefresh && now - lastStatusAt >= 200) {
        lastStatusAt = now;
        needsUiRefresh = false;
        refreshUi();
    }
}
