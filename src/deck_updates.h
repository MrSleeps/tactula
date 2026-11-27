#pragma once
#include <Arduino.h>

namespace firmwareUpdate {
// Safe to call from web server / UI tasks. Never starts an OTA installation.
struct State {
  bool checksEnabled = true;
  bool showBadge = true;
  bool allowBeta = false;
  bool allowTouchUpdates = true;
  bool allowHaUpdates = false;
  bool remoteInstalling = false; // snapshot only
  bool webConfirmationAuthorized = false; // snapshot only
  bool available = false;
  bool checking = false;
  bool checkPending = false;
  bool autoDownload = false;
  bool downloading = false;
  bool staged = false;
  bool promptOnStage = false; // true only for user-initiated OTA downloads
  uint32_t downloadedBytes = 0;
  uint32_t totalBytes = 0;
  char version[40] = {};
  char error[112] = {};
  uint32_t lastCheckMs = 0;
};
void begin();
void tick();
void requestCheck();
State snapshot();
void setPreferences(bool checksEnabled, bool showBadge, bool autoDownload, bool allowBeta, bool allowTouchUpdates, bool allowHaUpdates);
// Only fetches trusted HTTPS bytes from the local mirror and verifies against
// digest/size discovered from GitHub. Never selects a new boot partition.
void requestDownload(bool fromTouch = false);
// Web UI can only request a physical-screen confirmation, never activate OTA.
bool requestTouchConfirmation();
// Consumed by the LVGL main loop; only returns true for a staged image.
bool takeTouchConfirmationRequest();
void cancelTouchConfirmation();
// Physical confirmation path. No remote API may call this method.
bool installStagedFromTouch();
// Explicit version-bound request from the opted-in MQTT controller only.
bool requestInstallFromHa(const char* version);
}
