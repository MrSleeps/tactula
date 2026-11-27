#pragma once

#include <Preferences.h>
#include <nvs.h>

// An absent read-only namespace is normal for features using defaults.
// Let Preferences report errors other than a missing namespace.
inline bool openOptionalPreferences(Preferences& prefs, const char* name) {
  nvs_handle_t handle;
  const esp_err_t result = nvs_open(name, NVS_READONLY, &handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return false;
  if (result == ESP_OK) nvs_close(handle);
  return prefs.begin(name, true);
}
