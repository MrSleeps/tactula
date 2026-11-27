// Glass wallpaper galleries and PSRAM image loading.
#ifndef DECK_WALL_H
#define DECK_WALL_H

#include <Arduino.h>
#include <lvgl.h>
#include "deck_sd.h"
#include "deck_blue_noise.h"

namespace deck {

constexpr uint32_t WALL_IDLE_MS = 30000;
constexpr size_t WALL_RESERVE = 256 * 1024;
constexpr size_t WALL_MAX_JPEG = 200 * 1024;
struct WallFile {
  uint64_t id;
  size_t size;
  uint32_t date;
  bool qualified = false;
  String name() const;
};
using WallFiles = WallVector<WallFile>;
uint64_t wallId(const String& name);
bool wallValidRef(const String& name);
void wallRebuildIndex();
void wallIndexChanged(bool portrait, bool daily, const String& name);
bool wallRemoveFile(bool portrait, bool daily, const String& name, bool protect = false);
// Slots: wallpaper, queued wallpaper, frame, queued frame, three source pins, pending pin.
void wallProtect(bool portrait, int slot, const String& name);
bool wallValidName(const String& name);
// Galleries: own uploads or daily downloads.
const char* wallDir(bool portrait, bool daily);
bool wallDaily();
// Gallery mode (own, downloaded, or both). Spotlight and Tactula feed share the downloaded gallery.
// Loop task only, after dispSettings.wallSource changed.
uint8_t wallSource();
void wallSetSource(uint8_t source);
String wallGalleryPath(bool portrait, const String& name = "");
WallFiles wallFiles(bool portrait);
WallFiles wallListDir(const char* path);
String wallCurrent(bool portrait);
// Name of the image actually installed in the live RGB565 buffer, not a queued selection.
String wallDisplayedName();
String wallPinned(bool portrait);
bool wallPin(bool portrait, const String& name);
void wallClearPin(bool portrait);
bool wallValidateJpeg(const char* path, bool portrait);
// Worker from wallRunWorker only. Undimmed RGB565, no full-size scratch.
bool wallDecodeCamera(const uint8_t* data, size_t bytes, int w, int h, uint16_t* pixels);
// Album covers only: smooth small JPEGs, with bounded scratch and nearest fallback.
bool wallDecodeArtwork(const uint8_t* data, size_t bytes, int w, int h, uint16_t* pixels);
bool wallLoadPicture(const char* path, int w, int h, uint16_t* pixels);
// Runs a picture job off the loop task. SD jobs share one long-lived worker with
// a PSRAM stack; LittleFS jobs get a short-lived task with an internal stack.
bool wallRunWorker(void (*job)(), bool onSd, const char* name);
// Loop task only. Requests replace older jobs without touching their buffers.
void wallRequest(bool portrait, const String& name);
bool wallNext(bool portrait);
void wallGalleryChanged(bool portrait);
void wallInvalidateJobs();
// Poll/start work, then commit only after the UI has stopped gestures.
bool wallPoll(int w, int h, bool idle, bool canSwitch, bool automatic = true);
void wallCommit();
// The automatic change interval starts again (the photo frame covered the wallpaper).
void wallRestartTimer();

// Where the wallpaper for a w x h screen lives on LittleFS.
const char* wallPath(int w, int h);
// Load the w x h wallpaper (a built-in gradient when there is no file),
// darken it by dimPct and blur a copy by blurR px (0 = an unblurred copy).
// False when memory is short or the selected JPEG cannot be decoded.
bool wallLoad(int w, int h, int blurR, int dimPct);
void wallFree();
// Changes whenever the images are freed or reloaded, including failed loads.
uint32_t wallGeneration();
// The dimmed RGB565 picture, or nullptr when no wallpaper is loaded.
const lv_image_dsc_t* wallSharp();
// A sharp wallpaper is installed together with its blurred bands.
inline bool wallBlur() { return wallSharp() != nullptr; }
// While a wallpaper is loaded, the RGB888 band containing y and the row at y.
constexpr int WALL_BLUR_BAND_ROWS = 16;
const lv_image_dsc_t& wallBlurBand(int y);
const uint8_t* wallBlurRow(int y);
// Near-gaussian blur of one 8-bit plane in place (tmp: scratch of the same size).
void wallBlurPlane(uint8_t* p, uint8_t* tmp, int w, int h, int r);
// Blue-noise dithering avoids the repeating Bayer row pattern.
// Oppose green to red/blue to reduce visible luminance variation.
inline uint16_t wallDither565(int r, int g, int b, int x, int y) {
  const int t = (BLUE_NOISE[y & 63][x & 63] * 2 + 1) * 255;
  int r5 = (r * 31 * 128 + t) / 32640,
      g6 = (g * 63 * 128 + 32640 - t) / 32640,
      b5 = (b * 31 * 128 + t) / 32640;
  if (r5 > 31) r5 = 31;
  if (g6 > 63) g6 = 63;
  if (b5 > 31) b5 = 31;
  return (uint16_t)(r5 << 11 | g6 << 5 | b5);
}

}  // namespace deck

#endif  // DECK_WALL_H
