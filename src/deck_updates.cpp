// Firmware update discovery (no unattended flashing). The HTTPS manifest is
// authenticated with a trusted root; board/variant, size, version and digest
// are checked before an update can be advertised. Installing firmware needs a
// separate signed-update / authenticated-control implementation.
#include "deck_updates.h"
#include "config.h"
#include "web_server.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <esp_partition.h>
#include <time.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace firmwareUpdate {
namespace {
// Includes ISRG Root X1 and DigiCert Global Root G2; verify TLS hostnames.
constexpr const char* ROOT_CA =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\\n"
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\\n"
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\\n"
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\\n"
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\\n"
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\\n"
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\\n"
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\\n"
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\\n"
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\\n"
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\\n"
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\\n"
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\\n"
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\\n"
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\\n"
    "MrY=\\n"
    "-----END CERTIFICATE-----\\n";
constexpr const char* MANIFEST_URL =
    "https://api.github.com/repos/MrSleeps/tactula/releases/latest";
constexpr uint32_t INTERVAL_MS = 6UL * 60 * 60 * 1000;
constexpr uint32_t RETRY_MS = 30UL * 60 * 1000;
constexpr uint32_t START_DELAY_MS = 45000;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
State status;
bool requested = false;
uint32_t nextAttempt = START_DELAY_MS;
bool running = false;
bool downloadRequested = false;
bool downloadRunning = false;
bool installRequested = false;
char approvedDigest[65] = {};
char approvedPath[160] = {};
uint32_t approvedSize = 0;
const esp_partition_t* stagedPartition = nullptr;

void setError(const char* msg) {
  portENTER_CRITICAL(&mux);
  snprintf(status.error, sizeof(status.error), "%s", msg);
  status.available = false;
  status.version[0] = 0;
  status.checking = false;
  status.lastCheckMs = millis();
  running = false;
  nextAttempt = millis() + RETRY_MS;
  portEXIT_CRITICAL(&mux);
}

bool parseVersion(const char* input, int (&numbers)[3]) {
  if (!input || *input == 'v') { if (!input) return false; ++input; }
  int offset = 0, used = 0;
  if (sscanf(input, "%d.%d.%d%n", &numbers[0], &numbers[1], &numbers[2], &used) != 3 ||
      used == 0 || input[used] != 0) return false;
  for (int n : numbers) if (n < 0 || n > 100000) return false;
  return true;
}
bool newer(const char* version) {
  int remote[3], current[3];
  if (!parseVersion(version, remote) || !parseVersion(FW_VERSION, current)) return false;
  for (int i = 0; i < 3; ++i) {
    if (remote[i] != current[i]) return remote[i] > current[i];
  }
  return false;
}
bool validHash(const char* s) {
  if (!s || strlen(s) != 64) return false;
  for (int i = 0; i < 64; ++i)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  return true;
}

void checkTask(void*) {
  // Require clock sync for X.509 certificate validity.
  if (time(nullptr) < 1700000000) {
    setError("Waiting for time synchronisation");
    vTaskDelete(nullptr);
    return;
  }
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  client.setTimeout(12000);
  HTTPClient http;
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setUserAgent("Tactula-Update-Check/1.0");
  bool ok = http.begin(client, MANIFEST_URL);
  if (!ok || http.GET() != HTTP_CODE_OK) {
    http.end();
    setError("Unable to query GitHub Releases over verified HTTPS");
    vTaskDelete(nullptr);
    return;
  }
  // API JSON contains release notes and metadata unrelated to the device.
  // Reject oversized responses and use ArduinoJson's document filter to
  // retain only the signed-in-TLS release metadata and asset fields.
  const int len = http.getSize();
  if (len > 65536) {
    http.end();
    setError("GitHub release response exceeds size limit");
    vTaskDelete(nullptr);
    return;
  }
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["draft"] = true;
  filter["prerelease"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["size"] = true;
  filter["assets"][0]["digest"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  const DeserializationError parseError = deserializeJson(
      doc, http.getStream(),
      DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(8));
  http.end();
  if (parseError || (doc["draft"] | true) || (doc["prerelease"] | true)) {
    setError("Invalid, draft or prerelease GitHub release");
    vTaskDelete(nullptr);
    return;
  }
  const char* version = doc["tag_name"] | "";
  if (!version[0] || strlen(version) >= sizeof(status.version) || !newer(version)) {
    portENTER_CRITICAL(&mux);
    status.available = false;
    status.version[0] = 0;
    status.error[0] = 0;
    status.lastCheckMs = millis();
    status.checking = false;
    running = false;
    nextAttempt = millis() + INTERVAL_MS;
    portEXIT_CRITICAL(&mux);
    vTaskDelete(nullptr);
    return;
  }
  // We require the OTA binary published by the exact GitHub Release, not
  // arbitrary paths from a separately hosted update feed.
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  constexpr const char* edition = "with_bambu";
#else
  constexpr const char* edition = "without_bambu";
#endif
  char expectedName[115];
  snprintf(expectedName, sizeof(expectedName),
           "tactula-%s-jc3248w535-%s-ota.bin", version, edition);
  char expectedUrl[245];
  snprintf(expectedUrl, sizeof(expectedUrl),
           "https://github.com/MrSleeps/tactula/releases/download/%s/%s",
           version, expectedName);
  bool matched = false;
  char assetDigest[65] = {};
  uint32_t assetSize = 0;
  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  for (JsonObjectConst asset : doc["assets"].as<JsonArrayConst>()) {
    if (strcmp(asset["name"] | "", expectedName) != 0) continue;
    const char* digest = asset["digest"] | "";
    const char* url = asset["browser_download_url"] | "";
    const uint32_t size = asset["size"] | 0;
    if (strcmp(url, expectedUrl) != 0 || strncmp(digest, "sha256:", 7) != 0 ||
        !validHash(digest + 7) || !partition ||
        size < 1024 || size > partition->size) break;
    snprintf(assetDigest, sizeof(assetDigest), "%s", digest + 7);
    assetSize = size;
    matched = true;
    break;
  }
  if (!matched) {
    setError("Release lacks a matching verified board/edition OTA asset");
    vTaskDelete(nullptr);
    return;
  }
  portENTER_CRITICAL(&mux);
  status.available = true;
  // A release change invalidates any previously staged image.
  if (strcmp(status.version, version) != 0) {
    status.staged = false;
    stagedPartition = nullptr;
  }
  snprintf(status.version, sizeof(status.version), "%s", version);
  snprintf(approvedDigest, sizeof(approvedDigest), "%s", assetDigest);
  snprintf(approvedPath, sizeof(approvedPath),
    "/tactula/releases/%s/%s", version, expectedName);
  approvedSize = assetSize;
  status.totalBytes = assetSize;
  if (status.autoDownload && !status.staged) downloadRequested = true;
  status.error[0] = 0;
  status.checking = false;
  status.lastCheckMs = millis();
  running = false;
  nextAttempt = millis() + INTERVAL_MS;
  portEXIT_CRITICAL(&mux);
  vTaskDelete(nullptr);
}
} // namespace

void begin() {
  Preferences prefs;
  prefs.begin("tactula_upd", true);
  status.checksEnabled = prefs.getBool("checks", true);
  status.showBadge = prefs.getBool("badge", true);
  status.autoDownload = prefs.getBool("autodl", false);
  prefs.end();
#if defined(TACTULA_DEV_UPDATE_SIMULATION) && TACTULA_DEV_UPDATE_SIMULATION
  // Development-only visual fixture. Honour persisted badge/check toggles.
  // No URL, digest, OTA download, or boot partition activation is possible.
  status.available = true;
  snprintf(status.version, sizeof(status.version), "v9.9.9");
#endif
  nextAttempt = millis() + START_DELAY_MS;
}
State snapshot() {
  portENTER_CRITICAL(&mux);
  State result = status;
  portEXIT_CRITICAL(&mux);
  return result;
}
void setPreferences(bool checksEnabled, bool showBadge, bool autoDownload) {
  Preferences prefs;
  prefs.begin("tactula_upd", false);
  prefs.putBool("checks", checksEnabled);
  prefs.putBool("badge", showBadge);
  prefs.putBool("autodl", autoDownload);
  prefs.end();
  portENTER_CRITICAL(&mux);
  status.checksEnabled = checksEnabled;
  status.showBadge = showBadge;
  status.autoDownload = autoDownload;
  if (autoDownload && status.available && !status.staged) downloadRequested = true;
  if (!checksEnabled) {
    status.available = false;
    status.version[0] = 0;
  }
  requested = checksEnabled;
  portEXIT_CRITICAL(&mux);
}
void requestCheck() {
  portENTER_CRITICAL(&mux);
  requested = true;
  portEXIT_CRITICAL(&mux);
}

void requestDownload() {
#if defined(TACTULA_DEV_UPDATE_SIMULATION) && TACTULA_DEV_UPDATE_SIMULATION
  return;
#endif
  portENTER_CRITICAL(&mux);
  if (status.available && !status.staged && !status.downloading) downloadRequested = true;
  portEXIT_CRITICAL(&mux);
}

// Only called from a local LVGL touch event. The web API intentionally exposes
// no install endpoint; the verified image is not activated from the network.
bool installStagedFromTouch() {
#if defined(TACTULA_DEV_UPDATE_SIMULATION) && TACTULA_DEV_UPDATE_SIMULATION
  return false;
#endif
  portENTER_CRITICAL(&mux);
  const bool ok = status.staged && !status.downloading && stagedPartition != nullptr;
  if (ok) installRequested = true;
  portEXIT_CRITICAL(&mux);
  return ok;
}

void downloadTask(void*) {
  char digest[65], path[160];
  uint32_t expected = 0;
  portENTER_CRITICAL(&mux);
  snprintf(digest, sizeof(digest), "%s", approvedDigest);
  snprintf(path, sizeof(path), "%s", approvedPath);
  expected = approvedSize;
  portEXIT_CRITICAL(&mux);
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  const auto fail = [](const char* reason) {
    portENTER_CRITICAL(&mux);
    status.downloading = false;
    status.staged = false;
    stagedPartition = nullptr;
    snprintf(status.error, sizeof(status.error), "%s", reason);
    downloadRunning = false;
    portEXIT_CRITICAL(&mux);
    vTaskDelete(nullptr);
  };
  if (!target || expected < 1024 || expected > target->size || !validHash(digest)) {
    fail("Image cannot fit inactive OTA partition");
    return;
  }
  if (time(nullptr) < 1700000000) {
    fail("Clock not synchronised; HTTPS unavailable");
    return;
  }
  // Only the known, hard-coded host serves bytes. No externally supplied URL.
  String url = String("https://firmware.mrsleeps.uk") + path;
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  client.setTimeout(15000);
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url) || http.GET() != HTTP_CODE_OK ||
      http.getSize() != static_cast<int>(expected)) {
    http.end();
    fail("Mirror returned an invalid image or length");
    return;
  }
  esp_ota_handle_t otaHandle = 0;
  if (esp_ota_begin(target, expected, &otaHandle) != ESP_OK) {
    http.end();
    fail("Cannot initialise inactive OTA partition");
    return;
  }
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  mbedtls_sha256_starts(&hash, 0);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[4096];
  uint32_t total = 0;
  uint32_t stalled = millis();
  bool ok = stream != nullptr;
  while (ok && total < expected) {
    if (WiFi.status() != WL_CONNECTED) { ok = false; break; }
    const size_t available = stream->available();
    if (!available) {
      if (millis() - stalled > 15000) { ok = false; break; }
      vTaskDelay(pdMS_TO_TICKS(25));
      continue;
    }
    const size_t size = std::min<size_t>(sizeof(buffer), std::min<size_t>(expected - total, available));
    const int count = stream->readBytes(buffer, size);
    if (count <= 0 || esp_ota_write(otaHandle, buffer, count) != ESP_OK ||
        mbedtls_sha256_update(&hash, buffer, count) != 0) {
      ok = false;
      break;
    }
    total += static_cast<uint32_t>(count);
    stalled = millis();
    portENTER_CRITICAL(&mux);
    status.downloadedBytes = total;
    portEXIT_CRITICAL(&mux);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  http.end();
  uint8_t bytes[32] = {};
  if (mbedtls_sha256_finish(&hash, bytes) != 0) ok = false;
  mbedtls_sha256_free(&hash);
  char observed[65];
  for (int i = 0; i < 32; ++i) snprintf(observed + 2*i, 3, "%02x", bytes[i]);
  if (!ok || total != expected || strcmp(observed, digest) != 0) {
    esp_ota_abort(otaHandle);
    fail("Download incomplete or SHA-256 mismatch");
    return;
  }
  // esp_ota_end also validates the ESP application image. It does NOT
  // change the next boot partition; only local confirmation may do that.
  if (esp_ota_end(otaHandle) != ESP_OK) {
    fail("ESP firmware image validation failed");
    return;
  }
  portENTER_CRITICAL(&mux);
  stagedPartition = target;
  status.staged = true;
  status.downloading = false;
  downloadRunning = false;
  status.error[0] = 0;
  portEXIT_CRITICAL(&mux);
  vTaskDelete(nullptr);
}

void tick() {
#if defined(TACTULA_DEV_UPDATE_SIMULATION) && TACTULA_DEV_UPDATE_SIMULATION
  return;
#endif
  if (WiFi.status() != WL_CONNECTED) return;
  if (webOtaBusy()) return;
  bool start = false;
  bool startDownload = false;
  bool activate = false;
  const esp_partition_t* bootTarget = nullptr;
  portENTER_CRITICAL(&mux);
  if (!running && !downloadRunning && (requested || (status.checksEnabled && (int32_t)(millis() - nextAttempt) >= 0))) {
    requested = false;
    running = true;
    status.checking = true;
    start = true;
    nextAttempt = millis() + RETRY_MS;
  }
  if (downloadRequested && !downloadRunning && !running &&
      status.available && !status.staged) {
    downloadRequested = false;
    downloadRunning = true;
    status.downloading = true;
    status.downloadedBytes = 0;
    status.error[0] = 0;
    startDownload = true;
  }
  if (installRequested && status.staged && !status.downloading &&
      !downloadRunning && stagedPartition) {
    installRequested = false;
    activate = true;
    bootTarget = stagedPartition;
  }
  portEXIT_CRITICAL(&mux);
  if (activate) {
    // A failed boot partition switch never starts a reboot.
    if (esp_ota_set_boot_partition(bootTarget) == ESP_OK) ESP.restart();
    else setError("Cannot activate staged firmware");
  }
  if (startDownload && xTaskCreate(downloadTask, "update_fetch", 12288, nullptr, 1, nullptr) != pdPASS) {
    portENTER_CRITICAL(&mux);
    downloadRunning = false;
    status.downloading = false;
    snprintf(status.error, sizeof(status.error), "No memory to download firmware");
    portEXIT_CRITICAL(&mux);
  }
  if (start) {
    if (xTaskCreate(checkTask, "update_check", 8192, nullptr, 1, nullptr) != pdPASS) {
      setError("No memory for update check");
    }
  }
}
} // namespace firmwareUpdate
