// Idle photo frame: loop owns LVGL and buffers; the worker only decodes JPEGs.
#include "debug_log.h"
#include "deck_ui_int.h"
#include <esp_random.h>

namespace deckui {
#if defined(BOARD_IS_JC8012P4A1C)
extern "C" const lv_font_t deck_font_weather_clock_144;
#endif
namespace {

enum FrameState : uint8_t { FRAME_OFF, FRAME_LOADING, FRAME_ENTER, FRAME_IDLE, FRAME_CHANGE, FRAME_EXIT, FRAME_DARKEN, FRAME_BLACK, FRAME_LIGHTEN };
FrameState s_state = FRAME_OFF;
uint16_t *s_page = nullptr, *s_base = nullptr, *s_picture = nullptr, *s_next = nullptr;
FrameRect* s_rects = nullptr;
FrameTileRun* s_runs = nullptr;
uint16_t s_rectCount = 0;
int s_width = 0, s_height = 0;
lv_obj_t *s_root = nullptr, *s_clockBox = nullptr, *s_clockTime = nullptr, *s_clockDate = nullptr;
lv_obj_t* s_fade = nullptr;
uint32_t s_fadeStarted = 0, s_fadeLast = 0;
constexpr uint32_t PHOTO_FADE_MS = 320;
constexpr uint32_t PHOTO_FADE_STEP_MS = 40;
bool s_fadeShared = false;
lv_obj_t* s_cross = nullptr;
lv_image_dsc_t s_crossDsc{};
uint16_t* s_crossPixels = nullptr;
constexpr uint32_t CROSSFADE_MS = 900;

void clearCrossfade() {
  if (s_cross) {
    lv_obj_delete(s_cross);
    s_cross = nullptr;
  }
  lv_image_cache_drop(&s_crossDsc);
  free(s_crossPixels);
  s_crossPixels = nullptr;
  s_crossDsc = {};
}
lv_obj_t *s_weatherText = nullptr, *s_weatherIcon = nullptr;
lv_obj_t* s_weatherStill = nullptr;
lv_draw_buf_t* s_weatherStillBuf = nullptr;
lv_draw_buf_t* s_weatherAtlas = nullptr;
uint32_t s_weatherAnimAt = 0;
unsigned s_weatherAnimFrame = 0;
uint32_t s_weatherRetryAt = 0;
String s_weatherStillPath;
TileDef s_frameClockDef{};
bool s_hasWeather = false;
uint32_t s_weatherIconCp = 0;
int s_clockW = 220, s_clockH = 64;
lv_image_dsc_t s_pictureDsc = {};
lv_draw_buf_t s_clockBuf = {};
uint32_t* s_clockPixels = nullptr;
uint32_t s_changed = 0, s_clockCheck = 0, s_retry = 0, s_epoch = 0, s_galleryCheck = 0;
bool s_bench = false, s_tried = false, s_nextNow = false, s_night = false;
uint8_t s_source = 0;
String s_name, s_nextName, s_error;
WallVector<uint64_t> s_shown;
PanelStats s_lastStats = {};
const char* s_animation = "none";
const char* s_lastAnimation = "none";
bool s_lastCancelled = false;

portMUX_TYPE s_loadMux = portMUX_INITIALIZER_UNLOCKED;
enum LoadState : uint8_t { LOAD_IDLE, LOAD_RUNNING, LOAD_READY };
LoadState s_loadState = LOAD_IDLE;
bool s_loadCancelled = false;
struct LoadJob {
  char path[64];
  int w, h;
  uint32_t epoch;
  uint16_t* pixels;
  bool ok;
};
LoadJob s_load = {};

LoadState loadState() {
  portENTER_CRITICAL(&s_loadMux);
  const LoadState state = s_loadState;
  portEXIT_CRITICAL(&s_loadMux);
  return state;
}

void loadJob() {
  portENTER_CRITICAL(&s_loadMux);
  LoadJob job = s_load;
  portEXIT_CRITICAL(&s_loadMux);
  job.pixels = (uint16_t*)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
  if (!job.pixels) {
    debugSerial.printf("Deck frame: RGB565 allocation failed: %u bytes; free=%u largest=%u\n",
        (unsigned)FRAME_BYTES, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  }
  job.ok = job.pixels && wallLoadPicture(job.path, job.w, job.h, job.pixels);
  portENTER_CRITICAL(&s_loadMux);
  const bool cancelled = s_loadCancelled;
  if (cancelled) {
    s_load = {};
    s_loadState = LOAD_IDLE;
  } else {
    s_load = job;
    s_loadState = LOAD_READY;
  }
  portEXIT_CRITICAL(&s_loadMux);
  if (cancelled) free(job.pixels);
}

bool requestPicture() {
  if (loadState() != LOAD_IDLE || s_next) return false;
  const auto files = wallFiles(s_width < s_height);
  if (files.empty()) { s_error = "empty gallery"; return false; }
  size_t index = 0;
  bool currentFound = false;
  if (s_state == FRAME_LOADING && s_name.isEmpty()) {
    const String current = wallCurrent(s_width < s_height);
    for (size_t i = 0; i < files.size(); ++i) {
      if (files[i].name() == current) { index = i; currentFound = true; break; }
    }
  }
  if (currentFound) {
    // Start with the dashboard's current image, then use normal gallery order.
  } else if (dispSettings.wallOrder && files.size() > 1) {
    WallVector<size_t> choices;
    for (size_t i = 0; i < files.size(); i++)
      if (std::find(s_shown.begin(), s_shown.end(), files[i].id) == s_shown.end()) choices.push_back(i);
    if (choices.empty()) {
      s_shown.assign(1, wallId(s_name));
      for (size_t i = 0; i < files.size(); i++)
        if (files[i].name() != s_name) choices.push_back(i);
    }
    index = choices[esp_random() % choices.size()];
  } else {
    for (size_t i = 0; i < files.size(); i++)
      if (files[i].name() > s_name) { index = i; break; }
  }
  s_nextName = files[index].name();
  wallProtect(s_width < s_height, 3, wallSource() == 1 ? "d/" + s_nextName : s_nextName);
  LoadJob job = {};
  strlcpy(job.path, wallGalleryPath(s_width < s_height, s_nextName).c_str(), sizeof(job.path));
  job.w = s_width;
  job.h = s_height;
  job.epoch = s_epoch;
  portENTER_CRITICAL(&s_loadMux);
  s_load = job;
  s_loadState = LOAD_RUNNING;
  s_loadCancelled = false;
  portEXIT_CRITICAL(&s_loadMux);
  if (!wallRunWorker(loadJob, wallOnSd(), "deck_frame")) {
    portENTER_CRITICAL(&s_loadMux);
    s_loadState = LOAD_IDLE;
    portEXIT_CRITICAL(&s_loadMux);
    s_error = "worker allocation failed";
    return false;
  }
  return true;
}

void cancelPicture() {
  uint16_t* pixels = nullptr;
  portENTER_CRITICAL(&s_loadMux);
  s_loadCancelled = true;
  if (s_loadState == LOAD_READY) {
    pixels = s_load.pixels;
    s_load = {};
    s_loadState = LOAD_IDLE;
  }
  portEXIT_CRITICAL(&s_loadMux);
  free(pixels);
}

void pollPicture() {
  if (loadState() != LOAD_READY) return;
  portENTER_CRITICAL(&s_loadMux);
  const LoadJob job = s_load;
  s_load = {};
  s_loadState = LOAD_IDLE;
  portEXIT_CRITICAL(&s_loadMux);
  if (job.epoch == s_epoch && s_state != FRAME_OFF && job.ok) {
    s_next = job.pixels;
    s_error = "";
  } else {
    free(job.pixels);
    if (job.epoch == s_epoch && !job.ok) {
      s_error = "picture decode failed";
      debugSerial.printf("Deck frame: picture decode failed: %s\n", job.path);
      s_retry = millis();
    }
  }
}

Src source(const uint16_t* pixels) { return {pixels, (uint16_t)s_width, true, (uint16_t)s_height}; }

// Prefer the weather clock on the page being left, then any configured
// weather clock, then a plain clock. Copy the definition: config may rebuild.
void selectClock() {
  s_frameClockDef = TileDef{};
  s_hasWeather = false;
  for (int pass = 0; pass < 4; ++pass) {
    const bool weather = pass < 2;
    for (const auto& tile : s_tiles) {
      if (!tile.def || tile.off || (pass % 2 == 0 && tile.page != sh.active)) continue;
      if (tile.def->type != (weather ? TILE_WEATHER_CLOCK : TILE_CLOCK)) continue;
      s_frameClockDef = *tile.def;
      s_hasWeather = weather;
      return;
    }
  }
}

void animateFrameWeather() {
  if (!s_weatherAtlas || !s_weatherStillBuf || !s_weatherStill ||
      !presenceSettings.frameClock || s_state != FRAME_IDLE) return;
  const uint32_t now = millis();
  if (now-s_weatherAnimAt < 250) return;
  s_weatherAnimAt = now;
  s_weatherAnimFrame = (s_weatherAnimFrame+1)%8;
  // One native 64px frame; LVGL scales it to the small overlay size.
  lv_image_cache_drop(s_weatherStillBuf);
  for (unsigned y=0;y<64;++y)
    memcpy(s_weatherStillBuf->data+y*s_weatherStillBuf->header.stride,
           s_weatherAtlas->data+y*s_weatherAtlas->header.stride+s_weatherAnimFrame*64*4,64*4);
  lv_obj_invalidate(s_weatherStill);
}

void updateClock(bool force) {
  if (!s_clockBox) return;
  showObj(s_clockBox, presenceSettings.frameClock);
  if (!force && millis() - s_clockCheck < 1000) return;
  s_clockCheck = millis();
  const lv_opa_t background = presenceSettings.frameClockBackground ? LV_OPA_COVER : LV_OPA_TRANSP;
  if (lv_obj_get_style_bg_opa(s_clockBox, LV_PART_MAIN) != background)
    lv_obj_set_style_bg_opa(s_clockBox, background, 0);
  const time_t epoch = time(nullptr);
  char clock[16] = "--:--", date[32] = "";
  if (epoch >= 1609459200) {
    struct tm lt;
    localtime_r(&epoch, &lt);
    if (netSettings.use24h) snprintf(clock, sizeof(clock), "%02d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(clock, sizeof(clock), "%d:%02d %s", (lt.tm_hour + 11) % 12 + 1, lt.tm_min,
                  lt.tm_hour < 12 ? "AM" : "PM");
    strftime(date, sizeof(date), "%a %d %b", &lt);
  }
  setText(s_clockTime, clock);
  setText(s_clockDate, date);
  showObj(s_clockDate, !s_frameClockDef.clockDateNever);
  if (s_hasWeather && s_weatherText) {
    HaEntity e;
    const bool known = s_frameClockDef.entity.length() &&
        haGetEntity(s_frameClockDef.entity, e) && e.known &&
        e.state != "unavailable" && e.state != "unknown";
    const String state = known ? e.state : String("");
    String value = known ? weatherTempText(e, true, s_frameClockDef.weatherApparent) : String("--");
    if (known) value += "  ·  " + weatherCondition(state);
    setText(s_weatherText, value.c_str());
    const bool iconEnabled = presenceSettings.frameWeatherIcon;
    const int textX = iconEnabled ? (s_width >= 800 ? 68 : 42) : 10;
    if (lv_obj_get_x(s_weatherText) != textX) {
      lv_obj_set_x(s_weatherText, textX);
      lv_obj_set_width(s_weatherText, s_clockW - textX - 10);
    }
    if (!s_weatherIcon || !s_weatherStill) return;
    // Keep the text compact when the optional artwork is hidden.
    const bool large = s_width >= 800;
    const lv_font_t* detailFont = large ? &deck_font_20 : &deck_font_14;
    const int detailH = lv_font_get_line_height(detailFont);
    const int rowTop = lv_obj_get_y(s_clockDate) +
        (s_frameClockDef.clockDateNever ? 0 : detailH + (large ? 6 : 4));
    const int iconSize = large ? 44 : 24;
    const int textY = rowTop + (iconEnabled ? std::max<int>(0, (iconSize - detailH) / 2) : 0);
    if (lv_obj_get_y(s_weatherText) != textY) lv_obj_set_y(s_weatherText, textY);
    if (lv_obj_get_y(s_weatherIcon) != rowTop) lv_obj_set_y(s_weatherIcon, rowTop);
    if (lv_obj_get_y(s_weatherStill) != rowTop) lv_obj_set_y(s_weatherStill, rowTop);
    if (!iconEnabled) {
      showObj(s_weatherIcon, false);
      showObj(s_weatherStill, false);
      if (s_weatherStillBuf) {
        lv_image_set_src(s_weatherStill, nullptr);
        lv_image_cache_drop(s_weatherStillBuf);
        lv_draw_buf_destroy(s_weatherStillBuf);
        s_weatherStillBuf = nullptr;
      }
      if (s_weatherAtlas) { lv_draw_buf_destroy(s_weatherAtlas); s_weatherAtlas = nullptr; }
      s_weatherStillPath = "";
      return;
    }
    TileDef visual = s_frameClockDef;
    visual.icon = String("mdi:") + weatherIcon(state);
    s_weatherIconCp = showIcon(s_weatherIcon, visual, nullptr, false,
                               s_width >= 800 ? 44 : 24, s_weatherIconCp);
    lv_obj_set_style_text_color(s_weatherIcon, lv_color_white(), 0);
    String path;
    if (known && s_frameClockDef.weatherIcons == 3 && wallOnSd()) {
      String slug = outlineWeatherSlug(state);
      slug.replace("-", "_");
      path = String("/touchdeck/icons/bedroom/wc_outline_anim_") + slug + "_off.png";
    }
    if (path != s_weatherStillPath || (path.length() && !s_weatherStillBuf && millis()-s_weatherRetryAt>=30000)) {
      s_weatherRetryAt=millis();
      lv_image_set_src(s_weatherStill, nullptr);
      if (s_weatherStillBuf) {
        lv_image_cache_drop(s_weatherStillBuf);
        lv_draw_buf_destroy(s_weatherStillBuf);
        s_weatherStillBuf = nullptr;
      }
      if (s_weatherAtlas) { lv_draw_buf_destroy(s_weatherAtlas); s_weatherAtlas=nullptr; }
      s_weatherStillPath = path;
      if (path.length() && wallFs().exists(path)) {
        bool opened = false;
        s_weatherAtlas = loadTilePng(path, true, opened);
        if (s_weatherAtlas && s_weatherAtlas->header.w==512 && s_weatherAtlas->header.h==64) {
          s_weatherStillBuf=lv_draw_buf_create(64,64,LV_COLOR_FORMAT_ARGB8888,0);
          if (s_weatherStillBuf) {
            for (unsigned y=0;y<64;++y)
              memcpy(s_weatherStillBuf->data+y*s_weatherStillBuf->header.stride,
                     s_weatherAtlas->data+y*s_weatherAtlas->header.stride,64*4);
            s_weatherAnimFrame=0;
            s_weatherAnimAt=millis();
          }
        }
        if (!s_weatherStillBuf && s_weatherAtlas) { lv_draw_buf_destroy(s_weatherAtlas); s_weatherAtlas=nullptr; }
        if (s_weatherStillBuf) {
          lv_image_set_src(s_weatherStill,s_weatherStillBuf);
          const int size = s_width >= 800 ? 44 : 24;
          lv_image_set_scale(s_weatherStill,size*LV_SCALE_NONE/64);
        }
      }
    }
    showObj(s_weatherStill,s_weatherStillBuf != nullptr);
    showObj(s_weatherIcon,s_weatherStillBuf == nullptr);
  }
}

void installPicture() {
  if (!s_picture) {
    // The dashboard wallpaper remains owned by the wallpaper renderer.
    lv_obj_set_style_bg_color(s_root, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_image_src(s_root, wallSharp(), 0);
    lv_obj_invalidate(s_root);
    return;
  }
  lv_image_cache_drop(&s_pictureDsc);
  s_pictureDsc = {};
  s_pictureDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  s_pictureDsc.header.cf = LV_COLOR_FORMAT_RGB565;
  s_pictureDsc.header.w = s_width;
  s_pictureDsc.header.h = s_height;
  s_pictureDsc.header.stride = s_width * 2;
  s_pictureDsc.data_size = FRAME_BYTES;
  s_pictureDsc.data = (const uint8_t*)s_picture;
  lv_obj_set_style_bg_image_src(s_root, &s_pictureDsc, 0);
  lv_obj_invalidate(s_root);
}

void makeScreen() {
  s_root = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, s_width, s_height);
  lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  installPicture();
  // Between the wallpaper and the clock; never dim the physical backlight.
  s_fade = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_fade);
  lv_obj_set_size(s_fade, s_width, s_height);
  lv_obj_set_style_bg_color(s_fade, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_fade, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(s_fade, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  s_clockBox = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_clockBox);
  lv_obj_remove_flag(s_clockBox, LV_OBJ_FLAG_SCROLLABLE);
  const bool large = s_width >= 800;
  const bool bigClock = presenceSettings.frameClockLarge;
  const lv_font_t* smallFont = large ? &lv_font_montserrat_40 : &deck_font_28;
  const lv_font_t* timeFont = bigClock ? (large ? clockFont72() : clockFont56()) : smallFont;
#if defined(BOARD_IS_JC8012P4A1C)
  // Match the native 144px dashboard weather clock (103px visible digits).
  static lv_font_t photoClock = deck_font_weather_clock_144;
  photoClock.fallback = &lv_font_montserrat_40; // AM/PM and unsynced placeholder
  if (bigClock && large) timeFont = &photoClock;
#endif
  s_clockW = std::min(s_width - 24, bigClock && large ? 560 : bigClock ? 320 : large ? 440 : 280);
  if (bigClock) {
    const char* sample = netSettings.use24h ? "88:88" : "88:88 PM";
    const int available = s_clockW - 20;
    if (textW(sample, timeFont) > available)
      timeFont = large ? clockFont72() : &lv_font_montserrat_40;
    if (textW(sample, timeFont) > available)
      timeFont = smallFont;
  }
  const int extra = bigClock ? std::max<int>(0, lv_font_get_line_height(timeFont) - lv_font_get_line_height(smallFont)) : 0;
  const int detailHeight = lv_font_get_line_height(large ? &deck_font_20 : &deck_font_14);
  const int minHeight = (large ? 64 : 46) + extra + detailHeight +
      (s_hasWeather ? (large ? 6 : 4) + std::max<int>(detailHeight, large ? 44 : 24) : 0) + 8;
  s_clockH = std::max<int>((large ? (s_hasWeather ? 152 : 112) : (s_hasWeather ? 100 : 76)) + extra, minHeight);
  lv_obj_set_pos(s_clockBox, 12, s_height - s_clockH - 14);
  lv_obj_set_size(s_clockBox, s_clockW, s_clockH);
  lv_obj_set_style_bg_color(s_clockBox, lv_color_black(), 0);
  lv_obj_set_style_bg_grad_color(s_clockBox, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_clockBox, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_main_opa(s_clockBox, 160, 0);
  lv_obj_set_style_bg_grad_opa(s_clockBox, 0, 0);
  lv_obj_set_style_bg_grad_dir(s_clockBox, LV_GRAD_DIR_HOR, 0);
  s_clockTime = lv_label_create(s_clockBox);
  lv_obj_set_pos(s_clockTime, 10, 6);
  lv_obj_set_style_text_color(s_clockTime, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_clockTime, timeFont, 0);
  // Tighten the gallery clock tracking without changing its font or size.
  lv_obj_set_style_text_letter_space(s_clockTime, bigClock && large ? -4 : -1, 0);
  s_clockDate = lv_label_create(s_clockBox);
  const int dateY = (large ? 64 : 46) + extra;
  lv_obj_set_pos(s_clockDate, 10, dateY);
  lv_obj_set_style_text_color(s_clockDate, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_clockDate, large ? &deck_font_20 : &deck_font_14, 0);
  if (s_hasWeather) {
    s_weatherIcon = lv_label_create(s_clockBox);
    const int detailH = lv_font_get_line_height(large ? &deck_font_20 : &deck_font_14);
    const int rowTop = dateY + (s_frameClockDef.clockDateNever ? 0 : detailH + (large ? 6 : 4));
    const int iconSize = large ? 44 : 24;
    lv_obj_set_pos(s_weatherIcon, 10, rowTop);
    s_weatherStill = lv_image_create(s_clockBox);
    lv_obj_remove_flag(s_weatherStill, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_image_set_pivot(s_weatherStill,0,0);
    lv_obj_set_pos(s_weatherStill, 10, rowTop);
    showObj(s_weatherStill,false);
    s_weatherText = lv_label_create(s_clockBox);
    const int weatherX = presenceSettings.frameWeatherIcon ? (large ? 68 : 42) : 10;
    lv_obj_set_pos(s_weatherText, weatherX, rowTop +
        (presenceSettings.frameWeatherIcon ? std::max<int>(0, (iconSize - detailH) / 2) : 0));
    lv_obj_set_width(s_weatherText, s_clockW - weatherX - 10);
    lv_label_set_long_mode(s_weatherText, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_color(s_weatherText, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_weatherText, large ? &deck_font_20 : &deck_font_14, 0);
  }
  updateClock(true);
}

bool capturePage() {
  s_page = (uint16_t*)heap_caps_aligned_alloc(64, FRAME_BYTES, MALLOC_CAP_SPIRAM);
  s_base = (uint16_t*)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
  Page& page = s_pages[sh.active];
  const int capacity = lv_obj_get_child_count(page.grid) + 1;
  s_rects = (FrameRect*)heap_caps_malloc(capacity * sizeof(FrameRect), MALLOC_CAP_SPIRAM);
  s_runs = (FrameTileRun*)heap_caps_malloc(capacity * sizeof(FrameTileRun), MALLOC_CAP_SPIRAM);
  if (!s_page || !s_base || !s_rects || !s_runs) return false;
  lv_obj_update_layout(page.root);
  lv_draw_buf_t buf;
  if (lv_draw_buf_init(&buf, s_width, s_height, LV_COLOR_FORMAT_RGB565, s_width * 2,
                       s_page, FRAME_BYTES) != LV_RESULT_OK ||
      lv_snapshot_take_to_draw_buf(page.root, LV_COLOR_FORMAT_RGB565, &buf) != LV_RESULT_OK) return false;
  const lv_image_dsc_t* sharp = themeUsesWallpaper(s_theme) ? wallSharp() : nullptr;
  const uint16_t color = lv_color_to_u16(lv_color_hex(C_BG));
  for (int y = 0; y < s_height; y++) {
    uint16_t* row = s_base + y * s_width;
    if (sharp) memcpy(row, sharp->data + y * sharp->header.stride, s_width * 2);
    else std::fill(row, row + s_width, color);
  }
  s_rectCount = 0;
  if (s_header) s_rects[s_rectCount++] = {0, 0, (int16_t)s_width, HEADER_H};
  for (uint32_t i = 0; i < lv_obj_get_child_count(page.grid); i++) {
    lv_obj_t* tile = lv_obj_get_child(page.grid, i);
    if (lv_obj_has_flag(tile, LV_OBJ_FLAG_HIDDEN)) continue;
    lv_area_t a;
    lv_obj_get_coords(tile, &a);
    const int x = std::max<int>(0, a.x1), y = std::max<int>(0, a.y1);
    const int x2 = std::min<int>(s_width - 1, a.x2), y2 = std::min<int>(s_height - 1, a.y2);
    if (x2 >= x && y2 >= y) s_rects[s_rectCount++] = {(int16_t)x, (int16_t)y, (int16_t)(x2 - x + 1), (int16_t)(y2 - y + 1)};
  }
  std::sort(s_rects, s_rects + s_rectCount, [](const FrameRect& a, const FrameRect& b) {
    return a.y == b.y ? a.x < b.x : a.y < b.y;
  });
  return true;
}

void startAnimation(FrameState state) {
  panelQuiesce();
  updateClock(true);
  free(s_clockPixels);
  s_clockPixels = presenceSettings.frameClock ?
      (uint32_t*)heap_caps_aligned_alloc(64, s_clockW * s_clockH * 4, MALLOC_CAP_SPIRAM) : nullptr;
  if (s_clockPixels &&
      (lv_draw_buf_init(&s_clockBuf, s_clockW, s_clockH, LV_COLOR_FORMAT_ARGB8888, s_clockW * 4,
                        s_clockPixels, s_clockW * s_clockH * 4) != LV_RESULT_OK ||
       lv_snapshot_take_to_draw_buf(s_clockBox, LV_COLOR_FORMAT_ARGB8888, &s_clockBuf) != LV_RESULT_OK)) {
    free(s_clockPixels);
    s_clockPixels = nullptr;
  }
  FrameScene scene = {};
  scene.base = source(state == FRAME_CHANGE ? s_picture : s_base);
  scene.picture = source(state == FRAME_CHANGE ? s_next : s_picture);
  scene.page = source(s_page);
  scene.waking = state == FRAME_EXIT;
  scene.clockFade = state != FRAME_CHANGE;
  scene.rects = s_rects;
  scene.runs = s_runs;
  scene.count = state == FRAME_CHANGE ? 0 : s_rectCount;
  if (s_clockPixels) {
    scene.clock = s_clockPixels;
    scene.clockStride = s_clockBuf.header.stride / 4;
    scene.clockRect = {12, (int16_t)(s_height - s_clockH - 14), (int16_t)s_clockW, (int16_t)s_clockH};
  }
  const uint32_t duration = state == FRAME_CHANGE ? 1000 : presenceSettings.frameAnim ?
                            frameTilesMs(s_rectCount) + FRAME_FADE_MS : 0;
  compBeginFrame(scene, duration);
  s_state = state;
  s_animation = state == FRAME_ENTER ? "enter" : state == FRAME_EXIT ? "exit" : "next";
}

void recordAnimation(const PanelStats& stats, bool cancelled) {
  s_lastStats = stats;
  s_lastAnimation = s_animation;
  s_lastCancelled = cancelled;
  debugSerial.printf("Deck frame: %s%s frames=%u avg=%uus max=%uus gap=%uus\n", s_lastAnimation,
                cancelled ? " cancelled" : "", (unsigned)stats.frames, (unsigned)stats.frameUsAvg,
                (unsigned)stats.frameUsMax, (unsigned)stats.gapMaxUs);
}

void shown() {
  s_name = s_nextName;
  wallProtect(s_width < s_height, 2, wallSource() == 1 ? "d/" + s_name : s_name);
  wallProtect(s_width < s_height, 3, "");
  if (std::find(s_shown.begin(), s_shown.end(), wallId(s_name)) == s_shown.end())
    s_shown.push_back(wallId(s_name));
  s_changed = millis();
  s_nextNow = false;
}

void beginPhotoFade(bool shared) {
  s_fadeShared = shared;
  // Keep an old-image layer above the wallpaper and below the clock. Prefer
  // native resolution, but on P4 a half-resolution copy costs only 500 KiB.
  const lv_image_dsc_t* old = shared || !s_picture ? wallSharp() : &s_pictureDsc;
  if (old && old->data && old->header.cf == LV_COLOR_FORMAT_RGB565 &&
      old->header.w == s_width && old->header.h == s_height) {
    const size_t reserve = 256 * 1024;
    for (int factor : {1, 2, 4}) {
      if (s_width % factor || s_height % factor) continue;
      const int w = s_width / factor, h = s_height / factor;
      const size_t bytes = (size_t)w * h * 2;
      if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < bytes + reserve ||
          heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < bytes) continue;
      s_crossPixels = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
      if (!s_crossPixels) continue;
      for (int y = 0; y < h; ++y) {
        const auto* row = (const uint16_t*)(old->data + y * factor * old->header.stride);
        for (int x = 0; x < w; ++x) s_crossPixels[y * w + x] = row[x * factor];
      }
      s_crossDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      s_crossDsc.header.cf = LV_COLOR_FORMAT_RGB565;
      s_crossDsc.header.w = w;
      s_crossDsc.header.h = h;
      s_crossDsc.header.stride = w * 2;
      s_crossDsc.data_size = bytes;
      s_crossDsc.data = (const uint8_t*)s_crossPixels;
      s_cross = lv_image_create(s_root);
      lv_image_set_src(s_cross, &s_crossDsc);
      lv_image_set_pivot(s_cross, 0, 0);
      lv_image_set_scale(s_cross, factor * 256);
      lv_obj_set_pos(s_cross, 0, 0);
      lv_obj_remove_flag(s_cross, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
      lv_obj_move_foreground(s_clockBox);
      // Present the old layer before the shared decoder overwrites its pixels.
      lv_timer_handler();
      lv_refr_now(s_disp);
      s_fadeStarted = millis();
      s_state = FRAME_BLACK;
      debugSerial.printf("Deck frame: crossfade old layer %dx%d (%u bytes)\n", w, h, (unsigned)bytes);
      return;
    }
  }
  debugSerial.println("Deck frame: crossfade memory unavailable; using black fade");
  s_fadeStarted = millis();
  s_fadeLast = s_fadeStarted;
  s_state = FRAME_DARKEN;
}

// The deck may have changed while asleep: catch it up, then take the page image again.
void wake() {
  frameCatchUp();
  if (!frameActive()) return;
  lv_obj_move_foreground(s_root);  // a rebuild adds pages above it
#if defined(BOARD_IS_JC8012P4A1C)
  frameAbort();
  return;
#endif
  free(s_page); free(s_base); free(s_rects); free(s_runs);
  s_page = s_base = nullptr;
  s_rects = nullptr;
  s_runs = nullptr;
  if (!s_count || !capturePage()) {
    frameAbort();
    return;
  }
  startAnimation(FRAME_EXIT);
}

bool enter() {
  if (s_state != FRAME_OFF || !s_disp || !s_count || getScreenState() != SCREEN_DECK ||
      s_qp.open || s_modalCard || s_ed.root || sh.owned || compMode() != COMP_NONE ||
      touchIsDown() || manualDisplayOff() || nightScreenOff()) return false;
  stopGestures();
  s_width = logicalW();
  s_height = logicalH();
  s_source = wallSource();
  selectClock();
  s_galleryCheck = millis();
  s_error = "";
  s_name = "";
  s_shown.clear();
  ++s_epoch;
  s_state = FRAME_LOADING;
  s_retry = 0;
  s_tried = true;
  portENTER_CRITICAL(&s_mux);
  sh.swipeEnabled = false;
  portEXIT_CRITICAL(&s_mux);
  setIdleSleep(true, 255);
#if defined(BOARD_IS_JC8012P4A1C)
  // Reuse the live wallpaper immediately; do not require another full-screen
  // allocation just to enter photo mode. Gallery decoding can follow later.
  s_name = wallCurrent(s_width < s_height);
  s_nextName = s_name;
  shown();
  makeScreen();
  s_state = FRAME_IDLE;
  debugSerial.printf("Deck frame: entered using current wallpaper\n");
  return true;
#endif
  if (loadState() == LOAD_IDLE && !requestPicture()) {
    debugSerial.printf("Deck frame: entry failed: %s\n", s_error.c_str());
    frameAbort();
    setIdleSleep(false, 0);
    return false;
  }
  return true;
}

}  // namespace

bool frameActive() { return s_state != FRAME_OFF; }

void frameAbort() {
  if (!frameActive()) return;
  panelQuiesce();
  PanelStats stats;
  if (compFrameResult(stats)) recordAnimation(stats, true);
  ++s_epoch;
  wallProtect(s_width < s_height, 2, "");
  wallProtect(s_width < s_height, 3, "");
  cancelPicture();  // A running decoder frees its own output when it finishes.
  clearCrossfade();
  if (s_root) {
    lv_obj_delete(s_root);
    wallRestartTimer();  // else an overdue Glass change follows the wake
  }
  s_root = s_clockBox = s_clockTime = s_clockDate = nullptr;
  s_weatherText = s_weatherIcon = s_weatherStill = nullptr;
  if (s_weatherStillBuf) {
    lv_image_cache_drop(s_weatherStillBuf);
    lv_draw_buf_destroy(s_weatherStillBuf);
    s_weatherStillBuf = nullptr;
  }
  if (s_weatherAtlas) { lv_draw_buf_destroy(s_weatherAtlas); s_weatherAtlas=nullptr; }
  s_weatherStillPath = "";
  s_fade = nullptr;
  s_weatherIconCp = 0;
  lv_image_cache_drop(&s_pictureDsc);
  free(s_clockPixels);
  s_clockPixels = nullptr;
  free(s_page); free(s_base); free(s_picture); free(s_next); free(s_rects); free(s_runs);
  s_page = s_base = s_picture = s_next = nullptr;
  s_rects = nullptr;
  s_runs = nullptr;
  s_rectCount = 0;
  s_state = FRAME_OFF;
  s_bench = s_nextNow = false;
  for (int i = 0; i < s_count; i++) s_pages[i].stale = true;
  lv_obj_invalidate(lv_screen_active());
  ++s_gen;
}

bool frameSleep(bool sleep) {
  const bool night = nightScreenOff();
  if (!sleep || night != s_night) s_tried = false;
  s_night = night;
  if (frameActive()) {
    if (!sleep && !s_bench) setIdleSleep(false, 0);
    return true;
  }
  if (!presenceSettings.frame || !sleep) return false;
  if (night || manualDisplayOff()) return false;
  if (!s_tried) enter();
  if (!frameActive()) setIdleSleep(false, 0);
  return true;
}

void frameTick() {
  pollPicture();
  if (!frameActive()) return;
  if (getScreenState() != SCREEN_DECK || manualDisplayOff() || nightScreenOff() ||
      s_source != wallSource() || s_width != logicalW() || s_height != logicalH() ||
      s_qp.open || s_modalCard || s_ed.root) {
    frameAbort();
    setIdleSleep(false, 0);
    return;
  }
  if (s_state == FRAME_LOADING) {
    if (!idleSleepActive()) { frameAbort(); return; }
    if (!s_next) {
      if (loadState() == LOAD_IDLE && (s_retry || !requestPicture())) {
        frameAbort();
        setIdleSleep(false, 0);
      }
      return;
    }
 #if !defined(BOARD_IS_JC8012P4A1C)
    if (!capturePage()) {
      s_error = "snapshot allocation failed";
      frameAbort();
      setIdleSleep(false, 0);
      return;
    }
#endif
    s_picture = s_next;
    s_next = nullptr;
    shown();
    makeScreen();
#if defined(BOARD_IS_JC8012P4A1C)
    s_state = FRAME_IDLE;
#else
    startAnimation(FRAME_ENTER);
#endif
  }
  if (s_state == FRAME_ENTER || s_state == FRAME_EXIT || s_state == FRAME_CHANGE) {
    PanelStats stats;
    if (!compFrameResult(stats)) return;
    recordAnimation(stats, false);
    panelQuiesce();
    if (s_state == FRAME_EXIT) { frameAbort(); return; }
    if (s_state == FRAME_CHANGE) {
      lv_image_cache_drop(&s_pictureDsc);
      free(s_picture);
      s_picture = s_next;
      s_next = nullptr;
      installPicture();
      shown();
    }
    s_state = FRAME_IDLE;
  }
  if (s_state == FRAME_DARKEN || s_state == FRAME_BLACK || s_state == FRAME_LIGHTEN) {
    if (!idleSleepActive()) {
      // A wake touch must not wait for the transition or commit a queued image.
      lv_obj_set_style_bg_opa(s_fade, LV_OPA_TRANSP, 0);
      clearCrossfade();
      wake();
      return;
    }
    updateClock(false);
    const uint32_t now = millis();
    if (s_state == FRAME_BLACK) {
      // The previous loop rendered the opaque overlay. Drain scanout before
      // overwriting the shared P4 pixels so the old image cannot flash through.
      if (now - s_fadeStarted < 80) return;
      panelQuiesce();
      if (touchIsDown()) return;
      if (s_fadeShared) {
        if (wallPoll(s_width, s_height, true, true, false)) {
          const uint32_t generation = wallGeneration();
          wallCommit();
          if (wallGeneration() != generation) {
            installPicture();
            s_nextName = wallCurrent(s_width < s_height);
            shown();
            s_retry = 0;
            for (int i = 0; i < s_count; ++i) {
              auto& page = s_pages[i];
              page.stale = true;
              if (themeUsesModernLayout(s_theme)) {
                lv_obj_set_style_bg_image_src(page.root, wallSharp(), 0);
                lv_obj_invalidate(page.root);
              } else {
                page.wallBake = true;
              }
            }
            qpMarkStale();
            ++s_gen;
            debugSerial.printf("Deck frame: rotated using shared wallpaper: %s; next in %u min\n",
                s_name.c_str(), (unsigned)std::max<uint32_t>(1, presenceSettings.frameEvery));
          }
        }
      } else if (s_next) {
        auto* old = s_picture;
        s_picture = s_next;
        s_next = nullptr;
        installPicture();
        free(old);
        shown();
      }
      s_fadeStarted = s_fadeLast = millis();
      s_state = FRAME_LIGHTEN;
      return;
    }
    if (now - s_fadeLast < PHOTO_FADE_STEP_MS) return;
    s_fadeLast = now;
    const uint32_t duration = s_cross ? CROSSFADE_MS : PHOTO_FADE_MS;
    const uint32_t elapsed = std::min<uint32_t>(now - s_fadeStarted, duration);
    const uint8_t amount = (uint8_t)(elapsed * 255 / duration);
    if (s_cross) lv_obj_set_style_image_opa(s_cross, 255 - amount, 0);
    else lv_obj_set_style_bg_opa(s_fade, s_state == FRAME_DARKEN ? amount : 255 - amount, 0);
    if (elapsed == duration) {
      if (s_state == FRAME_DARKEN) {
        // Force a black frame before committing pixels on a later tick.
        lv_timer_handler();
        lv_refr_now(s_disp);
        s_fadeStarted = millis();
        s_state = FRAME_BLACK;
      } else {
        clearCrossfade();
        s_state = FRAME_IDLE;
        s_changed = millis();  // count the full display interval after fade-in
      }
    }
    return;
  }
  if (s_state != FRAME_IDLE) return;
  if (!idleSleepActive()) { wake(); return; }
  updateClock(false);
  animateFrameWeather();
  const uint32_t now = millis();
#if defined(BOARD_IS_JC8012P4A1C)
  if (themeUsesWallpaper(s_theme) && wallSharp()) {
    // Use the existing P4 compressed-JPEG staging and pixel-buffer reuse.
    // The separate photo decoder would need another contiguous 2 MiB block.
    const bool portrait = s_width < s_height;
    const bool safe = !touchIsDown() && compMode() == COMP_NONE && !sh.owned;
    if (wallPoll(s_width, s_height, true, safe, false)) {
      beginPhotoFade(true);
      return;
    }
    // Use a fresh timestamp for the rotation interval.
    const uint32_t rotationNow = millis();
    const uint32_t interval = std::max<uint32_t>(1, presenceSettings.frameEvery) * 60000UL;
    if (safe && (s_nextNow || rotationNow - s_changed >= interval) &&
        (!s_retry || rotationNow - s_retry >= 30000)) {
      s_retry = rotationNow;
      if (wallFiles(portrait).size() > 1 && wallNext(portrait)) {
        s_nextNow = false;
      }
    }
    return;
  }
#endif
  if (now - s_galleryCheck >= 5000) {
    s_galleryCheck = now;
    if (wallFiles(s_width < s_height).empty()) {
      s_error = "empty gallery";
      // Keep showing the current picture (or clock) if no rotation is possible.
      s_retry = now;
      return;
    }
  }
  if (!s_next && loadState() == LOAD_IDLE && (!s_retry || now - s_retry >= 30000)) {
    if (!requestPicture()) {
      s_retry = now;
      debugSerial.printf("Deck frame: gallery rotation unavailable: %s\n", s_error.c_str());
    }
  }
  if (s_next && (s_nextNow || now - s_changed >= presenceSettings.frameEvery * 60000UL)) {
    if (!wallFs().exists(wallGalleryPath(s_width < s_height, s_nextName))) {
      free(s_next);
      s_next = nullptr;
      return;
    }
    beginPhotoFade(false);
  }
}

}  // namespace deckui

bool deckFrameCommand(const String& op) {
  using namespace deckui;
  if (op == "enter") {
    if (!enter()) return false;
    s_bench = true;
    return true;
  }
  if (op == "exit") {
    if (!frameActive()) return false;
    s_bench = false;
    presenceWake();
    return true;
  }
  if (op == "next" && (s_state == FRAME_IDLE || s_state == FRAME_ENTER)) {
    s_nextNow = true;
    return true;
  }
  return false;
}

void deckFrameJson(JsonObject out) {
  using namespace deckui;
  const char* names[] = {"off", "loading", "entering", "idle", "changing", "exiting", "fading-out", "black", "fading-in"};
  out["state"] = names[s_state];
  out["picture"] = s_name;
  out["nextReady"] = s_next != nullptr;
  out["loading"] = loadState() == LOAD_RUNNING;
  out["error"] = s_error;
  out["animation"] = s_lastAnimation;
  out["cancelled"] = s_lastCancelled;
  out["frames"] = s_lastStats.frames;
  out["averageUs"] = s_lastStats.frameUsAvg;
  out["maxUs"] = s_lastStats.frameUsMax;
  out["longestGapUs"] = s_lastStats.gapMaxUs;
  out["buildAverageUs"] = s_lastStats.buildUsAvg;
  out["flushAverageUs"] = s_lastStats.flushUsAvg;
}
