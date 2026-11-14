// Settings migration from the original bedroomdeck namespace to touchdeck.
// Never clear the source: it provides a fallback until a new firmware is
// verified, and users may still be running an older image in the other slot.
#include "settings_nvs_migration.h"
#include <Arduino.h>
#include <nvs.h>
#include <vector>
#include <cstring>

namespace {
constexpr const char* OLD_NS = "bedroomdeck";
constexpr const char* NEW_NS = "touchdeck";
constexpr const char* MARKER = "migrated_v1";
constexpr uint8_t MARKER_VALUE = 1;

bool copyValue(nvs_handle_t from, nvs_handle_t to, const char* key, nvs_type_t type) {
  esp_err_t err = ESP_FAIL;
#define COPY_SCALAR(T, NAME, CTYPE) case NVS_TYPE_##T: { \
    CTYPE value = 0; CTYPE check = 0; \
    if (nvs_get_##NAME(from, key, &value) != ESP_OK) return false; \
    err = nvs_set_##NAME(to, key, value); \
    if (err != ESP_OK || nvs_get_##NAME(to, key, &check) != ESP_OK || check != value) return false; \
    return true; \
  }
  switch (type) {
    COPY_SCALAR(I8, i8, int8_t)
    COPY_SCALAR(U8, u8, uint8_t)
    COPY_SCALAR(I16, i16, int16_t)
    COPY_SCALAR(U16, u16, uint16_t)
    COPY_SCALAR(I32, i32, int32_t)
    COPY_SCALAR(U32, u32, uint32_t)
    COPY_SCALAR(I64, i64, int64_t)
    COPY_SCALAR(U64, u64, uint64_t)
    case NVS_TYPE_STR:
    case NVS_TYPE_BLOB: {
      size_t len = 0;
      if ((type == NVS_TYPE_STR ? nvs_get_str(from, key, nullptr, &len)
                                : nvs_get_blob(from, key, nullptr, &len)) != ESP_OK ||
          len > 65536) return false;
      std::vector<uint8_t> bytes(len ? len : 1), check(len ? len : 1);
      if (type == NVS_TYPE_STR) {
        if (nvs_get_str(from, key, (char*)bytes.data(), &len) != ESP_OK) return false;
        err = nvs_set_str(to, key, (const char*)bytes.data());
      } else {
        if (nvs_get_blob(from, key, bytes.data(), &len) != ESP_OK) return false;
        err = nvs_set_blob(to, key, bytes.data(), len);
      }
      if (err != ESP_OK) return false;
      size_t verifyLen = len;
      err = type == NVS_TYPE_STR
              ? nvs_get_str(to, key, (char*)check.data(), &verifyLen)
              : nvs_get_blob(to, key, check.data(), &verifyLen);
      return err == ESP_OK && verifyLen == len &&
             memcmp(bytes.data(), check.data(), len) == 0;
    }
    default:
      return false;  // Unknown types must not be silently dropped.
  }
#undef COPY_SCALAR
}
}  // namespace

bool migrateSettingsNamespace() {
  nvs_handle_t newer;
  if (nvs_open(NEW_NS, NVS_READONLY, &newer) == ESP_OK) {
    uint8_t marker = 0;
    const bool done = nvs_get_u8(newer, MARKER, &marker) == ESP_OK &&
                      marker == MARKER_VALUE;
    nvs_close(newer);
    if (done) return true;
  }

  nvs_handle_t old;
  const esp_err_t oldErr = nvs_open(OLD_NS, NVS_READONLY, &old);
  if (oldErr == ESP_ERR_NVS_NOT_FOUND) return true; // Fresh installation.
  if (oldErr != ESP_OK) {
    Serial.println("Settings: legacy NVS unavailable, using new namespace");
    return true;
  }
  if (nvs_open(NEW_NS, NVS_READWRITE, &newer) != ESP_OK) {
    nvs_close(old);
    Serial.println("Settings: migration deferred; using legacy settings");
    return false;
  }

  nvs_iterator_t it = nullptr;
  esp_err_t err = nvs_entry_find("nvs", OLD_NS, NVS_TYPE_ANY, &it);
  bool ok = (err == ESP_OK);
  if (err == ESP_ERR_NVS_NOT_FOUND) ok = true;
  while (err == ESP_OK) {
    nvs_entry_info_t info{};
    nvs_entry_info(it, &info);
    if (!copyValue(old, newer, info.key, info.type)) {
      ok = false;
      break;
    }
    err = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_OK) ok = false;
  if (ok) ok = nvs_commit(newer) == ESP_OK;

  // Only mark after all values have been committed. A failed or interrupted
  // copy is retried from the original on the next boot; no credentials are lost.
  if (ok) {
    ok = nvs_set_u8(newer, MARKER, MARKER_VALUE) == ESP_OK &&
         nvs_commit(newer) == ESP_OK;
  }
  nvs_close(newer);
  nvs_close(old);
  Serial.println(ok ? "Settings: NVS migration to touchdeck completed"
                    : "Settings: NVS migration deferred; using legacy settings");
  return ok;
}
