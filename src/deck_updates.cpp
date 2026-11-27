#include "optional_preferences.h"
// Firmware update discovery (no unattended flashing). The HTTPS manifest is
// authenticated with a trusted root; board/variant, size, version and digest
// are checked before an update can be advertised. Installing firmware needs a
// separate signed-update / authenticated-control implementation.
#include "debug_log.h"
#include "deck_updates.h"
#include "firmware_mirror_ca.h"
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
#include <esp_heap_caps.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace firmwareUpdate {
namespace {
constexpr const char* ROOT_CA = FIRMWARE_MIRROR_CA;
// The mirror has independent, verified release feeds for S3 and P4.
// Never attempt to install a binary for another processor.
#if defined(BOARD_IS_JC8012P4A1C)
constexpr const char* STABLE_MANIFEST_URL =
    "https://firmware.mrsleeps.uk/tactula/releases/jc8012p4a1c/current.json";
constexpr const char* BETA_MANIFEST_URL =
    "https://firmware.mrsleeps.uk/tactula/releases/jc8012p4a1c/beta.json";
constexpr const char* MIRROR_BOARD = "p4";
#else
constexpr const char* STABLE_MANIFEST_URL =
    "https://firmware.mrsleeps.uk/tactula/releases/current.json";
constexpr const char* BETA_MANIFEST_URL =
    "https://firmware.mrsleeps.uk/tactula/releases/beta.json";
constexpr const char* MIRROR_BOARD = "s3";
#endif
constexpr uint32_t INTERVAL_MS = 6UL * 60 * 60 * 1000;
constexpr uint32_t RETRY_MS = 30UL * 60 * 1000;
constexpr uint32_t START_DELAY_MS = 45000;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
State status;
bool requested = false;
uint32_t nextAttempt = START_DELAY_MS;
bool running = false;
bool downloadRequested = false;
bool manualDownloadRequested = false;
bool downloadRunning = false;
bool installRequested = false;
bool touchConfirmationRequested = false;
char approvedDigest[65] = {};
char approvedPath[224] = {};
uint32_t approvedSize = 0;
const esp_partition_t* stagedPartition = nullptr;

// A transient request failure must not invalidate a release whose filename,
// size and SHA-256 digest were already verified in an earlier successful check.
// Never preserve the prior release for malformed/untrusted release metadata.
void setError(const char* msg, bool preserveVerified = false) {
  portENTER_CRITICAL(&mux);
  snprintf(status.error, sizeof(status.error), "%s", msg);
  if (!preserveVerified) {
    status.available = false;
    status.version[0] = 0;
  }
  status.checking = false;
  status.lastCheckMs = millis();
  nextAttempt = millis() + RETRY_MS;
  portEXIT_CRITICAL(&mux);
}

struct ParsedVersion {
  int core[3] = {};
  const char* suffix = nullptr; // nullptr means stable release
};
bool parseVersion(const char* input, ParsedVersion& out) {
  if (!input) return false;
  if (*input == 'v') ++input;
  int used = 0;
  if (sscanf(input, "%d.%d.%d%n", &out.core[0], &out.core[1],
             &out.core[2], &used) != 3 || used == 0) return false;
  for (int n : out.core) if (n < 0 || n > 100000) return false;
  if (input[used] == 0) return true;
  if (input[used] != '-' || !input[used + 1]) return false;
  out.suffix = input + used + 1;
  bool afterDot = false;
  for (const char* p = out.suffix; *p; ++p) {
    const char c = *p;
    if (c == '.') {
      if (p == out.suffix || afterDot || !p[1]) return false;
      afterDot = true;
    } else if ((c >= '0' && c <= '9') ||
               (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-') {
      afterDot = false;
    } else return false;
  }
  return true;
}
int compareSuffix(const char* left, const char* right) {
  // SemVer prerelease identifiers compare numerically when both numeric;
  // otherwise lexicographically. A shorter matching prefix is older.
  while (true) {
    const char* le = left;
    const char* re = right;
    while (*le && *le != '.') ++le;
    while (*re && *re != '.') ++re;
    size_t ln = le - left, rn = re - right;
    bool lnum = ln > 0, rnum = rn > 0;
    for (size_t i = 0; i < ln; ++i) if (left[i] < '0' || left[i] > '9') lnum = false;
    for (size_t i = 0; i < rn; ++i) if (right[i] < '0' || right[i] > '9') rnum = false;
    int result = 0;
    if (lnum && rnum) {
      // Numeric identifiers are bounded by the firmware version buffer.
      while (ln > 1 && *left == '0') { ++left; --ln; }
      while (rn > 1 && *right == '0') { ++right; --rn; }
      if (ln != rn) result = ln < rn ? -1 : 1;
      else result = strncmp(left, right, ln);
    } else if (lnum != rnum) result = lnum ? -1 : 1;
    else {
      result = strncmp(left, right, std::min(ln, rn));
      if (!result && ln != rn) result = ln < rn ? -1 : 1;
    }
    if (result) return result;
    if (!*le || !*re) return (*le == *re) ? 0 : (*le ? 1 : -1);
    left = le + 1;
    right = re + 1;
  }
}
bool newer(const char* version) {
  ParsedVersion remote, current;
  if (!parseVersion(version, remote) || !parseVersion(FW_VERSION, current)) return false;
  for (int i = 0; i < 3; ++i)
    if (remote.core[i] != current.core[i]) return remote.core[i] > current.core[i];
  if (!remote.suffix && current.suffix) return true; // stable beats its beta
  if (remote.suffix && !current.suffix) return false;
  if (!remote.suffix && !current.suffix) return false;
  return compareSuffix(remote.suffix, current.suffix) > 0;
}
bool validHash(const char* s) {
  if (!s || strlen(s) != 64) return false;
  for (int i = 0; i < 64; ++i)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  return true;
}

struct CheckTiming {
  uint32_t dns = 0, headers = 0, body = 0;
};

void checkForUpdate(CheckTiming& timing) {
  // Snapshot the opted-in channel at the start of the HTTPS request.
  portENTER_CRITICAL(&mux);
  const bool useBeta = status.allowBeta;
  portEXIT_CRITICAL(&mux);
  // Require clock sync for X.509 certificate validity.
  if (time(nullptr) < 1700000000) {
    setError("Waiting for time synchronisation", true);
    return;
  }
  IPAddress mirrorAddress;
  const uint32_t dnsStarted = millis();
  const bool resolved = WiFi.hostByName("firmware.mrsleeps.uk", mirrorAddress) == 1;
  timing.dns = millis() - dnsStarted;
  if (!resolved) {
    setError("Firmware mirror DNS lookup failed", true);
    return;
  }
  debugSerial.printf("[Tactula OTA] Mirror IP=%s, internal heap=%u, largest block=%u\n",
                mirrorAddress.toString().c_str(),
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));

  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  client.setTimeout(12000);
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setTimeout(12000);
  http.useHTTP10(true);  // Static manifest: require nonchunked Content-Length.
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setUserAgent("Tactula-Update-Check/2.0");
  const uint32_t requestStarted = millis();
  const bool begun = http.begin(client, useBeta ? BETA_MANIFEST_URL : STABLE_MANIFEST_URL);
  const int httpStatus = begun ? http.GET() : -1;
  timing.headers = millis() - requestStarted;
  if (!begun || httpStatus != HTTP_CODE_OK) {
    char diagnostic[112] = {};
    if (httpStatus > 0) {
      snprintf(diagnostic, sizeof(diagnostic), "Mirror manifest HTTP %d", httpStatus);
    } else {
      char tlsReason[96] = {};
      const int tlsCode = client.lastError(tlsReason, sizeof(tlsReason));
      debugSerial.printf("[Tactula OTA] Mirror HTTPS failure: HTTP=%d TLS=%d (%s)\n",
                    httpStatus, tlsCode, tlsReason);
      snprintf(diagnostic, sizeof(diagnostic),
               "Mirror HTTPS failed TLS %d (HTTP %d)", tlsCode, httpStatus);
    }
    http.end();
    setError(diagnostic, true);
    return;
  }

  // The mirror publishes this tiny feed atomically after verifying the
  // release binaries against GitHub's original SHA-256 digests and sizes.
  // Keep the entire JSON bounded; never load GitHub's large releases API.
  constexpr int MAX_MANIFEST_BYTES = 4096;
  const int declaredLength = http.getSize();
  if (declaredLength < 1 || declaredLength > MAX_MANIFEST_BYTES) {
    http.end();
    setError("Mirror manifest size missing or too large", true);
    return;
  }
  String responseBody;
  if (!responseBody.reserve(declaredLength + 1)) {
    http.end();
    setError("Not enough memory for update manifest", true);
    return;
  }
  const uint32_t bodyStarted = millis();
  responseBody = http.getString();
  timing.body = millis() - bodyStarted;
  const size_t receivedLength = responseBody.length();
  http.end();
  debugSerial.printf("[Tactula OTA] Manifest expected=%d received=%u\n",
                declaredLength, static_cast<unsigned>(receivedLength));
  if (receivedLength != static_cast<size_t>(declaredLength)) {
    setError("Mirror manifest download incomplete", true);
    return;
  }
  JsonDocument doc;
  const DeserializationError parseError =
      deserializeJson(doc, responseBody, DeserializationOption::NestingLimit(8));
  responseBody = "";
  if (parseError) {
    char diagnostic[112] = {};
    snprintf(diagnostic, sizeof(diagnostic),
             "Mirror manifest JSON invalid: %s", parseError.c_str());
    setError(diagnostic, true);
    return;
  }
  if ((doc["schema"] | 0) != 1 ||
      strcmp(doc["status"] | "", "published") != 0) {
    setError("Mirror release not published or manifest invalid");
    return;
  }
  const char* version = doc["version"] | "";
  if (!version[0] || strlen(version) >= sizeof(status.version)) {
    setError("Mirror manifest has invalid version");
    return;
  }
  // A preference change while TLS was active invalidates this response.
  portENTER_CRITICAL(&mux);
  const bool channelChanged = status.allowBeta != useBeta;
  if (channelChanged) {
    status.checking = false;
    requested = status.checksEnabled;
  }
  portEXIT_CRITICAL(&mux);
  if (channelChanged) return;

  if (!newer(version)) {
    portENTER_CRITICAL(&mux);
    status.available = false;
    status.version[0] = 0;
    status.error[0] = 0;
    status.lastCheckMs = millis();
    status.checking = false;
    nextAttempt = millis() + INTERVAL_MS;
    portEXIT_CRITICAL(&mux);
    return;
  }
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  constexpr const char* edition = "bambu";
  constexpr const char* firmwareVariant = "with_bambu";
#else
  constexpr const char* edition = "standard";
  constexpr const char* firmwareVariant = "without_bambu";
#endif
  char expectedName[140];
  char expectedPath[200];
#if defined(BOARD_IS_JC8012P4A1C)
  // P4 files have hardware-specific names and a dedicated mirror directory.
  snprintf(expectedName, sizeof(expectedName),
           "tactula-%s-p4-jc8012p4a1c-%s-ota.bin", version, edition);
  snprintf(expectedPath, sizeof(expectedPath),
           "releases/jc8012p4a1c/%s/%s", version, expectedName);
#else
  // The S3 mirror intentionally keeps legacy filenames as hardlink aliases,
  // so existing S3 installations continue to update without migration.
  snprintf(expectedName, sizeof(expectedName),
           "tactula-%s-jc3248w535-%s-ota.bin", version, firmwareVariant);
  snprintf(expectedPath, sizeof(expectedPath),
           "releases/%s/%s", version, expectedName);
#endif
  char assetDigest[65] = {};
  uint32_t assetSize = 0;
  bool matched = false;
  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  for (JsonObjectConst variant : doc["variants"].as<JsonArrayConst>()) {
    if (strcmp(variant["board"] | "", MIRROR_BOARD) != 0 ||
        strcmp(variant["edition"] | "", edition) != 0) continue;
    JsonObjectConst asset = variant["ota"].as<JsonObjectConst>();
    const char* digest = asset["sha256"] | "";
    const char* path = asset["path"] | "";
    const uint32_t size = asset["size"] | 0;
    if (strcmp(path, expectedPath) != 0 || !validHash(digest) ||
        !partition || size < 1024 || size > partition->size) break;
    snprintf(assetDigest, sizeof(assetDigest), "%s", digest);
    assetSize = size;
    matched = true;
    break;
  }
  if (!matched) {
    setError("Mirror lacks verified board/edition OTA asset");
    return;
  }
  portENTER_CRITICAL(&mux);
  if (status.allowBeta != useBeta) {
    status.checking = false;
    requested = status.checksEnabled;
    portEXIT_CRITICAL(&mux);
    return;
  }
  status.available = true;
  // A release change invalidates any previously staged image.
  if (strcmp(status.version, version) != 0) {
    status.staged = false;
    touchConfirmationRequested = false;
    status.promptOnStage = false;
    stagedPartition = nullptr;
  }
  snprintf(status.version, sizeof(status.version), "%s", version);
  snprintf(approvedDigest, sizeof(approvedDigest), "%s", assetDigest);
  snprintf(approvedPath, sizeof(approvedPath),
    "/tactula/%s", expectedPath);
  approvedSize = assetSize;
  status.totalBytes = assetSize;
  if (status.autoDownload && !status.staged) downloadRequested = true;
  status.error[0] = 0;
  status.checking = false;
  status.lastCheckMs = millis();
  nextAttempt = millis() + INTERVAL_MS;
  portEXIT_CRITICAL(&mux);
}

struct CheckHeap {
  size_t internalFree, internalLargest, psramFree, psramLargest;
};

CheckHeap checkHeap() {
  constexpr auto internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr auto psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  return {heap_caps_get_free_size(internal), heap_caps_get_largest_free_block(internal),
          heap_caps_get_free_size(psram), heap_caps_get_largest_free_block(psram)};
}

// Compare both samples with the check task's stack still allocated.
// checkForUpdate() destroys its HTTPS and JSON objects before returning.
void checkTask(void*) {
  const CheckHeap before = checkHeap();
  CheckTiming timing;
  const uint32_t started = millis();
  checkForUpdate(timing);
  const uint32_t elapsed = millis() - started;
  const CheckHeap after = checkHeap();
  debugSerial.printf("[Tactula OTA] Check timing: DNS %lu ms, HTTPS headers %lu ms, body %lu ms, total %lu ms\n",
                     static_cast<unsigned long>(timing.dns), static_cast<unsigned long>(timing.headers),
                     static_cast<unsigned long>(timing.body), static_cast<unsigned long>(elapsed));
  debugSerial.printf("[Tactula OTA] Check heap free/largest (bytes): "
                "internal %u/%u -> %u/%u, PSRAM %u/%u -> %u/%u\n",
                static_cast<unsigned>(before.internalFree), static_cast<unsigned>(before.internalLargest),
                static_cast<unsigned>(after.internalFree), static_cast<unsigned>(after.internalLargest),
                static_cast<unsigned>(before.psramFree), static_cast<unsigned>(before.psramLargest),
                static_cast<unsigned>(after.psramFree), static_cast<unsigned>(after.psramLargest));
  portENTER_CRITICAL(&mux);
  running = false;
  portEXIT_CRITICAL(&mux);
  vTaskDelete(nullptr);
}
} // namespace

void begin() {
  Preferences prefs;
  if (openOptionalPreferences(prefs, "tactula_upd")) {
    status.checksEnabled = prefs.getBool("checks", true);
    status.showBadge = prefs.getBool("badge", true);
    status.autoDownload = prefs.getBool("autodl", false);
    status.allowBeta = prefs.getBool("beta", false);
    prefs.end();
  }
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
  result.checkPending = requested;
  portEXIT_CRITICAL(&mux);
  return result;
}
void setPreferences(bool checksEnabled, bool showBadge, bool autoDownload, bool allowBeta) {
  Preferences prefs;
  prefs.begin("tactula_upd", false);
  prefs.putBool("checks", checksEnabled);
  prefs.putBool("badge", showBadge);
  prefs.putBool("autodl", autoDownload);
  prefs.putBool("beta", allowBeta);
  prefs.end();
  portENTER_CRITICAL(&mux);
  const bool channelChanged = status.allowBeta != allowBeta;
  status.checksEnabled = checksEnabled;
  status.showBadge = showBadge;
  status.autoDownload = autoDownload;
  status.allowBeta = allowBeta;
  if (channelChanged) {
    // Do not offer or install firmware staged from the previous channel.
    status.available = false;
    status.staged = false;
    status.promptOnStage = false;
    status.version[0] = 0;
    status.error[0] = 0;
    stagedPartition = nullptr;
    touchConfirmationRequested = false;
    installRequested = false;
    downloadRequested = false;
    manualDownloadRequested = false;
    approvedDigest[0] = 0;
    approvedPath[0] = 0;
    approvedSize = 0;
  } else if (autoDownload && status.available && !status.staged) {
    downloadRequested = true;
  }
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
  if (status.available && !status.staged && !status.downloading) {
    manualDownloadRequested = true;
    downloadRequested = true;
  }
  portEXIT_CRITICAL(&mux);
}

// Only called from a local LVGL touch event. The web API intentionally exposes
// no install endpoint; the verified image is not activated from the network.
bool requestTouchConfirmation() {
  portENTER_CRITICAL(&mux);
  const bool ready = status.staged && !status.downloading &&
                     stagedPartition != nullptr;
  if (ready) touchConfirmationRequested = true;
  portEXIT_CRITICAL(&mux);
  return ready;
}

bool takeTouchConfirmationRequest() {
  portENTER_CRITICAL(&mux);
  const bool ready = touchConfirmationRequested &&
                     status.staged && !status.downloading &&
                     stagedPartition != nullptr;
  touchConfirmationRequested = false;
  portEXIT_CRITICAL(&mux);
  return ready;
}

// Only the physical LVGL confirmation may invoke the installation request.
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

void downloadUpdate() {
  char digest[65], path[224];
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
    touchConfirmationRequested = false;
    status.promptOnStage = false;
    stagedPartition = nullptr;
    snprintf(status.error, sizeof(status.error), "%s", reason);
    portEXIT_CRITICAL(&mux);
  };
  if (!target || expected < 1024 || expected > target->size || !validHash(digest)) {
    fail("Image cannot fit inactive OTA partition");
    return;
  }
  if (time(nullptr) < 1700000000) {
    fail("Clock not synchronised; HTTPS unavailable");
    return;
  }
  constexpr uint32_t transferTimeoutMs = 15000;
  constexpr uint32_t internalRam = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const uint32_t started = millis();
  const unsigned freeBefore = heap_caps_get_free_size(internalRam);
  const unsigned largestBefore = heap_caps_get_largest_free_block(internalRam);

  // Only the known, hard-coded host serves bytes. No externally supplied URL.
  String url = String("https://firmware.mrsleeps.uk") + path;
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  client.setTimeout(transferTimeoutMs);
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setConnectTimeout(transferTimeoutMs);
  http.setTimeout(transferTimeoutMs);
  // Match manifest retrieval: request a non-chunked response with a fixed
  // Content-Length. ESP32 OTA must reject missing or mismatched file sizes.
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setUserAgent("Tactula-OTA-Download/2.0");
  const bool begun = http.begin(client, url);
  const int httpStatus = begun ? http.GET() : -1;
  const int contentLength = httpStatus == HTTP_CODE_OK ? http.getSize() : -1;
  if (httpStatus != HTTP_CODE_OK ||
      contentLength != static_cast<int>(expected)) {
    char diagnostic[112] = {};
    if (!begun) {
      snprintf(diagnostic, sizeof(diagnostic), "Cannot initialise firmware HTTPS request");
    } else if (httpStatus < 0) {
      char tlsReason[96] = {};
      const int tlsCode = client.lastError(tlsReason, sizeof(tlsReason));
      snprintf(diagnostic, sizeof(diagnostic),
               "Firmware HTTPS failed (HTTP %d, TLS %d, largest block %u B)",
               httpStatus, tlsCode, largestBefore);
      debugSerial.printf("[Tactula OTA] HTTPS failed after %lu ms, TLS %d (%s), "
                    "internal free/largest before %u/%u B, now %u/%u B, "
                    "Wi-Fi status %d, RSSI %ld dBm\n",
                    static_cast<unsigned long>(millis() - started), tlsCode, tlsReason,
                    freeBefore, largestBefore,
                    static_cast<unsigned>(heap_caps_get_free_size(internalRam)),
                    static_cast<unsigned>(heap_caps_get_largest_free_block(internalRam)),
                    static_cast<int>(WiFi.status()), static_cast<long>(WiFi.RSSI()));
    } else {
      snprintf(diagnostic, sizeof(diagnostic),
               "Mirror image HTTP %d, length %d (expected %lu)",
               httpStatus, contentLength, static_cast<unsigned long>(expected));
    }
    debugSerial.printf("[Tactula OTA] %s\n", diagnostic);
    http.end();
    fail(diagnostic);
    return;
  }
  const uint32_t headersReceived = millis();
  esp_ota_handle_t otaHandle = 0;
  const esp_err_t beginResult = esp_ota_begin(target, expected, &otaHandle);
  if (beginResult != ESP_OK) {
    char diagnostic[112];
    snprintf(diagnostic, sizeof(diagnostic), "Cannot initialise inactive OTA partition: %s",
             esp_err_to_name(beginResult));
    http.end();
    fail(diagnostic);
    return;
  }
  const uint32_t transferStarted = millis();
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  mbedtls_sha256_starts(&hash, 0);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[4096];
  uint32_t total = 0;
  uint32_t lastRead = transferStarted;
  const char* failure = stream ? nullptr : "Firmware response stream unavailable";
  while (!failure && total < expected) {
    if (WiFi.status() != WL_CONNECTED) {
      failure = "Wi-Fi disconnected during firmware download";
      break;
    }
    const int available = stream->available();
    if (available <= 0) {
      if (!http.connected()) {
        failure = "Firmware connection closed early";
        break;
      }
      if (millis() - lastRead >= transferTimeoutMs) {
        failure = "Firmware download stalled";
        break;
      }
      vTaskDelay(1);
      continue;
    }
    const size_t size = std::min<size_t>(sizeof(buffer), std::min<size_t>(expected - total, available));
    const int count = stream->readBytes(buffer, size);
    if (count <= 0) {
      failure = "Firmware response read failed";
      break;
    }
    const esp_err_t writeResult = esp_ota_write(otaHandle, buffer, count);
    if (writeResult != ESP_OK) {
      debugSerial.printf("[Tactula OTA] Flash write failed: %s\n", esp_err_to_name(writeResult));
      failure = "Firmware flash write failed";
      break;
    }
    if (mbedtls_sha256_update(&hash, buffer, count) != 0) {
      failure = "Firmware SHA-256 calculation failed";
      break;
    }
    total += static_cast<uint32_t>(count);
    lastRead = millis();
    portENTER_CRITICAL(&mux);
    status.downloadedBytes = total;
    portEXIT_CRITICAL(&mux);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  http.end();
  uint8_t bytes[32] = {};
  if (!failure && mbedtls_sha256_finish(&hash, bytes) != 0) {
    failure = "Firmware SHA-256 calculation failed";
  }
  mbedtls_sha256_free(&hash);
  char observed[65];
  for (int i = 0; i < 32; ++i) snprintf(observed + 2*i, 3, "%02x", bytes[i]);
  debugSerial.printf("[Tactula OTA] %lu/%lu B, HTTPS %lu ms, flash erase %lu ms, transfer %lu ms\n",
                static_cast<unsigned long>(total), static_cast<unsigned long>(expected),
                static_cast<unsigned long>(headersReceived - started),
                static_cast<unsigned long>(transferStarted - headersReceived),
                static_cast<unsigned long>(millis() - transferStarted));
  if (!failure && total != expected) failure = "Firmware download incomplete";
  if (!failure && strcmp(observed, digest) != 0) failure = "Firmware SHA-256 mismatch";
  if (failure) {
    esp_ota_abort(otaHandle);
    fail(failure);
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
  status.error[0] = 0;
  portEXIT_CRITICAL(&mux);
}

void downloadTask(void*) {
  downloadUpdate();
  portENTER_CRITICAL(&mux);
  downloadRunning = false;
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
  // Prefer a pending verified download over another network check. It also
  // avoids recreating a TLS connection while the download is waiting to start.
  if (!running && !downloadRunning && !downloadRequested &&
      (requested || (status.checksEnabled && (int32_t)(millis() - nextAttempt) >= 0))) {
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
    status.promptOnStage = manualDownloadRequested;
    manualDownloadRequested = false;
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
    // A failed boot partition switch never starts a reboot. Include the
    // ESP-IDF error name/code instead of hiding the reason from the UI.
    const esp_err_t result = esp_ota_set_boot_partition(bootTarget);
    if (result == ESP_OK) {
      ESP.restart();
    } else {
      char reason[sizeof(status.error)];
      snprintf(reason, sizeof(reason), "Cannot activate staged firmware: %s (0x%04X)",
               esp_err_to_name(result), static_cast<unsigned>(result));
      debugSerial.printf("OTA: %s; target=%s offset=0x%lx size=%lu\n",
                    reason, bootTarget ? bootTarget->label : "none",
                    bootTarget ? static_cast<unsigned long>(bootTarget->address) : 0UL,
                    bootTarget ? static_cast<unsigned long>(bootTarget->size) : 0UL);
      setError(reason);
    }
  }
  if (startDownload && xTaskCreate(downloadTask, "update_fetch", 12288, nullptr, 1, nullptr) != pdPASS) {
    portENTER_CRITICAL(&mux);
    downloadRunning = false;
    status.downloading = false;
    snprintf(status.error, sizeof(status.error), "No memory to download firmware");
    portEXIT_CRITICAL(&mux);
  }
  if (start) {
    // HTTPS/TLS handshakes are CPU-heavy. An unpinned FreeRTOS task can run
    // on the same core as Arduino's loop(), stalling LVGL/touch processing.
    // Reserve core 1 for the physical UI; run update discovery on core 0
    // alongside Wi-Fi's higher-priority tasks instead. No behaviour change
    // to verification, update timing or user-confirmed installation.
    if (xTaskCreatePinnedToCore(checkTask, "update_check", 8192, nullptr,
                                tskIDLE_PRIORITY + 1, nullptr, 0) != pdPASS) {
      setError("No memory for update check");
      portENTER_CRITICAL(&mux);
      running = false;
      portEXIT_CRITICAL(&mux);
    }
  }
}
} // namespace firmwareUpdate
