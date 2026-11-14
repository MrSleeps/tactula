#pragma once
#include <Arduino.h>

namespace firmwareUpdate {
// Safe to call from web server / UI tasks. Never starts an OTA installation.
struct State {
  bool checksEnabled = true;
  bool showBadge = true;
  bool available = false;
  bool checking = false;
  bool autoDownload = false;
  bool downloading = false;
  bool staged = false;
  bool promptOnStage = false; // true only for user-initiated OTA downloads
  uint32_t downloadedBytes = 0;
  uint32_t totalBytes = 0;
  char version[24] = {};
  char error[112] = {};
  uint32_t lastCheckMs = 0;
};
void begin();
void tick();
void requestCheck();
State snapshot();
void setPreferences(bool checksEnabled, bool showBadge, bool autoDownload);
// Only fetches trusted HTTPS bytes from the local mirror and verifies against
// digest/size discovered from GitHub. Never selects a new boot partition.
void requestDownload();
// A physical confirmation on the LVGL device is required to install a staged
// image. No remote API may call this method.
bool installStagedFromTouch();
}
