// Deck UI: tile look and building a tile.
#include "debug_log.h"
#include "deck_ui_int.h"
#include "deck_sd.h"
#define LODEPNG_NO_COMPILE_CPP
#include <src/libs/lodepng/lodepng.h>
#include <math.h>

namespace deckui {

// --- Tile look ------------------------------------------------------------------------
String entityDomain(const String& entity) {
  const int dot = entity.indexOf('.');
  return dot > 0 ? entity.substring(0, dot) : String();
}

uint32_t domainColor(const String& domain) {
  if (domain == "light") return 0xB8ADFF;
  if (domain == "switch" || domain == "fan" || domain == "input_boolean") return 0x927DFF;
  if (domain == "scene" || domain == "script") return 0x7664E4;
  if (domain == "lock" || domain == "cover") return 0x85C4FF;
  return 0x48A5FF;
}

// Material Design Icons names (what HA uses) mapped onto the LVGL symbol font
// until the MDI subset font lands with the Home Assistant work.
const char* iconSymbol(const TileDef& t) {
  struct Map { const char* name; const char* sym; };
  static const Map kMap[] = {
    {"lightbulb", LV_SYMBOL_EYE_OPEN}, {"ceiling-light", LV_SYMBOL_EYE_OPEN},
    {"television-ambient-light", LV_SYMBOL_IMAGE}, {"fan", LV_SYMBOL_REFRESH},
    {"weather-night", LV_SYMBOL_IMAGE}, {"eye", LV_SYMBOL_EYE_OPEN},
    {"speaker", LV_SYMBOL_AUDIO},
    {"lock", LV_SYMBOL_HOME}, {"power", LV_SYMBOL_POWER}, {"power-sleep", LV_SYMBOL_POWER},
    {"chevron-right", LV_SYMBOL_RIGHT}, {"chevron-left", LV_SYMBOL_LEFT},
    {"arrow-right-circle-outline", LV_SYMBOL_RIGHT}, {"television", LV_SYMBOL_IMAGE},
    {"microphone-off", LV_SYMBOL_MUTE}, {"image", LV_SYMBOL_IMAGE},
    {"skip-previous", LV_SYMBOL_PREV}, {"skip-next", LV_SYMBOL_NEXT},
    {"play", LV_SYMBOL_PLAY}, {"pause", LV_SYMBOL_PAUSE},
    {"console", LV_SYMBOL_KEYBOARD}, {"volume-high", LV_SYMBOL_VOLUME_MAX},
    {"volume-off", LV_SYMBOL_MUTE}, {"wifi", LV_SYMBOL_WIFI}, {"cog", LV_SYMBOL_SETTINGS},
    {"bell", LV_SYMBOL_BELL}, {"home", LV_SYMBOL_HOME},
  };
  String name = t.icon;
  if (name.startsWith("mdi:")) name = name.substring(4);
  for (const Map& m : kMap)
    if (name == m.name) return m.sym;
  const String d = entityDomain(t.entity);
  if (d == "light") return LV_SYMBOL_EYE_OPEN;
  if (d == "fan") return LV_SYMBOL_REFRESH;
  if (d == "lock") return LV_SYMBOL_HOME;
  if (d == "media_player") return LV_SYMBOL_PLAY;
  if (d == "sensor" || d == "binary_sensor" || d == "weather") return LV_SYMBOL_LIST;
  return LV_SYMBOL_POWER;
}

// Default MDI icon for an entity without one of its own.
const char* defaultIcon(const String& domain, const String& dc, const String& unit, bool on) {
  if (domain == "light") return on ? "lightbulb" : "lightbulb-outline";
  if (domain == "switch" && dc == "outlet") return on ? "power-plug" : "power-plug-off";
  if (domain == "switch" || domain == "input_boolean")
    return on ? "toggle-switch-variant" : "toggle-switch-variant-off";
  if (domain == "fan") return on ? "fan" : "fan-off";
  if (domain == "lock") return on ? "lock" : "lock-open-variant";
  if (domain == "cover") return on ? "window-shutter-open" : "window-shutter";
  if (domain == "scene") return "palette";
  if (domain == "script") return "script-text";
  if (domain == "automation") return "robot";
  if (domain == "button" || domain == "input_button") return "gesture-tap-button";
  if (domain == "media_player") return "speaker";
  if (domain == "climate") return "thermostat";
  if (domain == "weather") return "weather-partly-cloudy";
  if (domain == "person") return "account";
  if (domain == "binary_sensor") {
    if (dc == "door" || dc == "garage_door") return on ? "door-open" : "door-closed";
    if (dc == "window") return on ? "window-open" : "window-closed";
    if (dc == "motion" || dc == "occupancy" || dc == "presence") return "motion-sensor";
    return on ? "checkbox-marked-circle" : "checkbox-blank-circle-outline";
  }
  if (domain == "sensor") {
    if (dc == "temperature" || unit.startsWith("\xC2\xB0")) return "thermometer";
    if (dc == "humidity") return "water-percent";
    if (dc == "power") return "flash";
    if (dc == "energy") return "lightning-bolt";
    if (dc == "battery") return "battery";
    if (dc == "illuminance") return "brightness-5";
    if (dc == "pressure" || dc == "atmospheric_pressure") return "gauge";
    if (dc == "carbon_dioxide") return "molecule-co2";
    if (dc == "voltage") return "sine-wave";
    if (dc == "current") return "current-ac";
    return "eye";
  }
  return "gesture-tap-button";
}

// Puts a tile's icon on a label: the tile's own MDI icon, else the entity's
// icon in HA, else a default; an LVGL symbol while no icon font is loaded.
// Returns the codepoint shown (0 = symbol); shown = what the label has now.
uint32_t showIcon(lv_obj_t* label, const TileDef& d, const HaEntity* e, bool on, int px,
                  uint32_t shown) {
  uint32_t cp = 0;
  if (on && d.onIcon.length()) cp = iconCodepoint(d.onIcon);
  if (!cp && d.icon.length()) cp = iconCodepoint(d.icon);
  if (!cp && e && e->icon.length()) cp = iconCodepoint(e->icon);
  if (!cp)
    cp = iconCodepoint(d.type == TILE_NAV ? "arrow-right-circle-outline" :
                       defaultIcon(entityDomain(d.entity), e ? e->deviceClass : String(),
                                   e ? e->unit : String(), on));
  const lv_font_t* f = cp ? iconFont(px, cp) : nullptr;
  if (!f) cp = 0;
  if (cp && cp == shown) return cp;
  if (cp) {
    char u[5];
    iconUtf8(cp, u);
    lv_obj_set_style_text_font(label, f, 0);
    lv_label_set_text(label, u);
  } else {
    lv_label_set_text(label, iconSymbol(d));
  }
  return cp;
}

String tileLabel(const TileDef& t) {
  if (t.label.length()) return t.label;
  if (t.type == TILE_GROUP) return "Group";
  if (t.type == TILE_TEXT) return "Text";
  if (t.type == TILE_CLOCK) return "Clock";
  if (t.type == TILE_WEATHER_CLOCK) return "Weather Clock";
  if (t.type == TILE_NAV) return "Navigation";
  const int dot = t.entity.indexOf('.');
  if (dot > 0) return t.entity.substring(dot + 1);
  return t.id.length() ? t.id : String("Tile");
}

String tileSub(const TileRt& r) {
  const TileDef& t = *r.def;
  if (t.sub.length()) return t.sub;
  if (t.type == TILE_TOGGLE) return r.on ? "On" : "Off";
  const String d = entityDomain(t.entity);
  if (d == "scene") return "Scene";
  if (d == "script") return "Script";
  if (d == "button" || d == "input_button") return "Press";
  return "";
}

// A tile with a matching id (Netflix, Plex, etc.) can use two PNGs stored
// in /touchdeck/icons/bedroom/. MDI remains the fallback while the card or
// either file is missing. The built-in SD 'B:' driver also serves brand fonts.
static bool bedroomPngName(const String& filename) {
  if (filename.length() < 8 || filename.length() > 64 || !filename.endsWith(".png")) return false;
  const int suffix = filename.lastIndexOf('_');
  if (suffix < 1 || (filename.substring(suffix) != "_on.png" &&
                      filename.substring(suffix) != "_off.png")) return false;
  for (int i = 0; i < suffix; ++i) {
    const char c = filename[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

void fitPngIcon(TileRt& r) {
  if (!r.pngIcon || !r.pngWidth || !r.pngHeight || !r.obj) return;
  // PNGs are direct children of the tile, not of the small MDI icon chip.
  // Reserve space for the label at the bottom and keep the artwork centred.
  const int w = lv_obj_get_width(r.obj);
  const int h = lv_obj_get_height(r.obj);
  if (w <= 0 || h <= 0) return;
  const int pad = std::min(w, h) >= 80 ? 9 : std::min(w, h) >= 56 ? 7 : 5;
  const bool artworkOnly = r.def->type == TILE_NAV || r.def->noName;
  const bool large = roomyWidget(w - 2 * pad, h - 2 * pad);
  const int labelH = artworkOnly ? 0 : large ? lv_font_get_line_height(&deck_font_28)
                                            : w - 2 * pad < 90 ? 15 : 18;
  const int reserved = artworkOnly ? 0 : labelH + 8;
  const int availableW = std::max(1, w - 2 * pad - 2);
  const int availableH = std::max(1, h - 2 * pad - reserved - 2);
  // Hidden labels give logos the entire inner tile. Allow modest upscaling
  // of small source PNGs (up to 2x), without stretching their aspect ratio.
  const uint32_t maxScale = large ? (artworkOnly ? 768 : 512) : artworkOnly ? 512 : 256;
  const uint32_t scale = std::min<uint32_t>(maxScale, std::min<uint32_t>(
      (uint32_t)availableW * 256 / r.pngWidth,
      (uint32_t)availableH * 256 / r.pngHeight));
  lv_image_set_scale(r.pngIcon, std::max<uint32_t>(1, scale));
  lv_obj_align(r.pngIcon, LV_ALIGN_CENTER, 0, -reserved / 2);
}

static void dropTilePng(lv_obj_t* image) {
  auto* pixels = static_cast<lv_draw_buf_t*>(lv_obj_get_user_data(image));
  if (!pixels) return;
  lv_obj_set_user_data(image, nullptr);
  lv_image_cache_drop(pixels);
  lv_draw_buf_destroy(pixels);
}

static void hideTilePng(TileRt& r) {
  const bool wasPng = r.pngIcon && !lv_obj_has_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN);
  if (r.pngIcon) {
    lv_obj_add_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(r.pngIcon, nullptr);
    dropTilePng(r.pngIcon);
  }
  r.pngSource = "";
  r.pngWidth = r.pngHeight = 0;
  if (r.icon) lv_obj_remove_flag(r.icon, LV_OBJ_FLAG_HIDDEN);
  if (wasPng && r.fitW > 0) { r.fitW = 0; fitTile(r); }
}

lv_draw_buf_t* loadTilePng(const String& path, bool explicitImage, bool& opened) {
  File file = deck::wallFs().open(path, "r");
  opened = static_cast<bool>(file);
  if (!opened) {
    if (explicitImage) debugSerial.printf("Deck: tile PNG open failed: %s\n", path.c_str());
    return nullptr;
  }
  // Match the upload limits, including files copied directly to the card.
  const size_t bytes = file.size();
  uint8_t header[24];
  if (bytes < sizeof(header) || bytes > 512 * 1024 ||
      file.read(header, sizeof(header)) != sizeof(header)) {
    debugSerial.printf("Deck: tile PNG size/header read failed: %s (%u bytes)\n",
                  path.c_str(), (unsigned)bytes);
    return nullptr;
  }
  const uint32_t width = ((uint32_t)header[16] << 24) | ((uint32_t)header[17] << 16) |
                         ((uint32_t)header[18] << 8) | header[19];
  const uint32_t height = ((uint32_t)header[20] << 24) | ((uint32_t)header[21] << 16) |
                          ((uint32_t)header[22] << 8) | header[23];
  bool hdSkycons = false;
#if defined(BOARD_IS_JC8012P4A1C)
  hdSkycons = path.startsWith("/touchdeck/icons/bedroom/wc_sky_hd_") && path.endsWith("_off.png") && width == 1280 && height == 160;
#endif
  if (!width || !height || (!hdSkycons && (width > 512 || height > 512))) {
    debugSerial.printf("Deck: tile PNG dimensions invalid: %s\n", path.c_str());
    return nullptr;
  }

  // Read once through Arduino FS. LVGL's file decoder reopens the file to
  // discover its length and again to read it, through the brand font drive.
  auto* data = static_cast<uint8_t*>(lv_malloc(bytes));
  if (!data) {
    debugSerial.printf("Deck: tile PNG input allocation failed: %s\n", path.c_str());
    return nullptr;
  }
  memcpy(data, header, sizeof(header));
  const size_t remaining = bytes - sizeof(header);
  const size_t read = file.read(data + sizeof(header), remaining);
  file.close();
  if (read != remaining) {
    lv_free(data);
    debugSerial.printf("Deck: tile PNG short read: %s (%u/%u bytes)\n",
                  path.c_str(), (unsigned)read, (unsigned)remaining);
    return nullptr;
  }

  // LVGL 9.5's bundled LodePNG returns an LVGL draw buffer. Keep that
  // allocation instead of cloning the decoded image before closing a decoder.
  unsigned char* decoded = nullptr;
  unsigned w = 0, h = 0;
  const unsigned error = lodepng_decode32(&decoded, &w, &h, data, bytes);
  lv_free(data);
  auto* pixels = reinterpret_cast<lv_draw_buf_t*>(decoded);
  if (error) {
    if (pixels) lv_draw_buf_destroy(pixels);
    debugSerial.printf("Deck: tile PNG decode failed: %s (%ux%u, %u bytes, error %u: %s)\n",
                  path.c_str(), (unsigned)width, (unsigned)height, (unsigned)bytes,
                  error, lodepng_error_text(error));
#if defined(BOARD_IS_JC8012P4A1C)
    if (error == 83) {
      const auto caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
      debugSerial.printf("Deck: PNG decode PSRAM free=%u largest=%u\n",
                         (unsigned)heap_caps_get_free_size(caps),
                         (unsigned)heap_caps_get_largest_free_block(caps));
    }
#endif
    return nullptr;
  }

  // LodePNG produces RGBA. LVGL's ARGB8888 has blue first in memory on ESP32.
  for (unsigned y = 0; y < h; ++y) {
    uint8_t* const row = pixels->data + y * pixels->header.stride;
    for (unsigned x = 0; x < w; ++x) std::swap(row[4 * x], row[4 * x + 2]);
  }
  return pixels;
}

static String tilePngName(const TileDef& def, bool on) {
  const String& selected = on ? def.onImage : def.offImage;
  if (!selected.isEmpty()) return selected;  // includes the explicit "icon" choice
  if (def.type == TILE_NAV) return {};
  return def.id + (on ? "_on.png" : "_off.png");
}

constexpr uint8_t TILE_PNG_MAX_ATTEMPTS = 4;
constexpr uint32_t TILE_PNG_RETRY_MS = 1500;

void recordTilePngFailure(TileRt& r) {
  ++r.pngFailures;
  r.pngFailedAt = millis();
}

bool tilePngRetryDue(const TileRt& r, uint32_t now) {
  return r.pngFailures && r.pngFailures < TILE_PNG_MAX_ATTEMPTS &&
         now - r.pngFailedAt >= TILE_PNG_RETRY_MS;
}

static void setTilePng(TileRt& r, const String& name, bool force) {
  if (!deck::wallOnSd() || !bedroomPngName(name)) {
    hideTilePng(r);
    return;
  }
  if (!force && r.pngSource == name) return;

  const String path = String("/touchdeck/icons/bedroom/") + name;
  const String& selected = r.pngState == 1 ? r.def->onImage : r.def->offImage;
  const bool explicitImage = !selected.isEmpty();
  bool opened = false;
  lv_draw_buf_t* pixels = loadTilePng(path, explicitImage, opened);
  if (!pixels) {
    // Missing tile-ID files are normal. Retry deliberate selections and
    // files that could be opened but failed to read, decode or allocate.
    if (explicitImage || opened || r.pngFailures) recordTilePngFailure(r);
    // A failed replacement must not evict an image already on screen.
    // An absent file (including a deliberate deletion) still falls back.
    if (!opened || r.pngSource != name) hideTilePng(r);
    return;
  }

  if (!r.pngIcon) {
    r.pngIcon = lv_image_create(r.obj);
    if (!r.pngIcon) {
      lv_draw_buf_destroy(pixels);
      recordTilePngFailure(r);
      debugSerial.printf("Deck: tile PNG widget allocation failed: %s\n", name.c_str());
      return;
    }
    lv_obj_remove_flag(r.pngIcon, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_event_cb(r.pngIcon, [](lv_event_t* e) {
      dropTilePng(lv_event_get_target_obj(e));
    }, LV_EVENT_DELETE, nullptr);
  }
  const bool wasPng = !r.pngSource.isEmpty();
  lv_image_set_src(r.pngIcon, pixels);
  if (lv_image_get_src(r.pngIcon) != pixels) {
    lv_draw_buf_destroy(pixels);
    hideTilePng(r);
    recordTilePngFailure(r);
    debugSerial.printf("Deck: tile PNG rejected by LVGL: %s\n", name.c_str());
    return;
  }
  dropTilePng(r.pngIcon);
  lv_obj_set_user_data(r.pngIcon, pixels);
  r.pngFailures = 0;
  r.pngSource = name;
  r.pngWidth = pixels->header.w;
  r.pngHeight = pixels->header.h;
  if (!r.navFace) fitPngIcon(r);
  lv_obj_remove_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(r.icon, LV_OBJ_FLAG_HIDDEN);
  if ((!wasPng || r.navFace) && r.fitW > 0) { r.fitW = 0; fitTile(r); }
}

void updatePngIcon(TileRt& r, bool on) {
  if (!r.icon || !r.chip || !r.def) return;
  const int8_t state = on ? 1 : 0;
  if (r.pngState == state && r.pngFailures) return;
  r.pngState = state;
  r.pngFailures = 0;
  setTilePng(r, tilePngName(*r.def, on), false);
}

void refreshTilePngs(const std::vector<String>& names) {
  for (TileRt& r : s_tiles) {
#if defined(BOARD_IS_JC8012P4A1C)
    if (r.def->type == TILE_WEATHER_CLOCK && r.def->weatherIcons == 3 && r.pngSource.length()) {
      const String name = r.pngSource.substring(r.pngSource.lastIndexOf('/') + 1);
      if (std::find(names.begin(), names.end(), name) != names.end())
        applyWeatherClock(r, true);
      continue;
    }
#endif
    if (r.pngState < 0) continue;
    const String name = tilePngName(*r.def, r.pngState == 1);
    if (std::find(names.begin(), names.end(), name) == names.end()) continue;
    r.pngFailures = 0;
    setTilePng(r, name, true);
    markPageStale(r.page);
  }
}

void retryTilePngs() {
  const uint32_t now = millis();
  for (TileRt& r : s_tiles) {
    if (!tilePngRetryDue(r, now)) continue;
#if defined(BOARD_IS_JC8012P4A1C)
    if (r.def->type == TILE_WEATHER_CLOCK) {
      if (r.off || r.page != sh.active || r.fitW <= 0) continue;
      r.fitW = 0;
      fitTile(r);
      return;
    }
#endif
    setTilePng(r, tilePngName(*r.def, r.pngState == 1), true);
    markPageStale(r.page);
    return;  // Limit SD and decoding work to one retry per loop pass.
  }
}

void markPageStale(int page) {
  if (page >= 0 && page < s_count) s_pages[page].stale = true;
}

uint32_t tileAccent(const TileDef& def) {
  if (def.hasColor) return def.color;
  if (themeUsesModernLayout(s_theme)) {
    const String domain = entityDomain(def.entity);
    if (domain == "light") return 0xFFBD65;
    if (domain == "media_player") return C_OK;
    if (domain == "switch") return modernColour(0x59D8F4);
    return C_OK;
  }
  return domainColor(entityDomain(def.entity));
}

// Per-tile look from deck.json, else the theme and display settings.
uint32_t tileBg(const TileDef& d) { return d.hasBgColor ? d.bgColor : C_TILE; }
int tileTint(const TileDef& d) {
  return d.opacity >= 0 ? d.opacity : dispSettings.glassTint;
}
int tileOnOpa(const TileDef& d) {
  return d.onOpacity >= 0 ? d.onOpacity : dispSettings.glassOn;
}

// Home Assistant normally includes rgb_color, including for some white modes.
// Convert Kelvin to approximate RGB when a white-only light omits rgb_color.
uint32_t lightColourRgb(const HaEntity& e) {
  if (e.rgb >= 0) return (uint32_t)e.rgb;
  if (e.colorTempKelvin <= 0) return 0xFFFFFFFFu;
  const float k = constrain(e.colorTempKelvin / 100.0f, 10.0f, 400.0f);
  const float red = k <= 66 ? 255 : 329.6987f * powf(k - 60, -0.1332048f);
  const float green = k <= 66 ? 99.4708f * logf(k) - 161.1196f
                              : 288.1222f * powf(k - 60, -0.07551485f);
  const float blue = k >= 66 ? 255 : k <= 19 ? 0 : 138.5177f * logf(k - 10) - 305.0448f;
  auto byte = [](float v) -> uint32_t { return (uint32_t)constrain((int)lroundf(v), 0, 255); };
  return (byte(red) << 16) | (byte(green) << 8) | byte(blue);
}

// Blend an RGB colour into the tile's static background: low-brightness lights
// stay visible, and "Both" doesn't hide an icon of the same hue.
uint32_t lightTint(uint32_t rgb, uint32_t base, int alpha) {
  const int inv = 255 - alpha;
  const uint32_t r = ((((rgb >> 16) & 255) * alpha + ((base >> 16) & 255) * inv) / 255);
  const uint32_t g = ((((rgb >> 8) & 255) * alpha + ((base >> 8) & 255) * inv) / 255);
  const uint32_t b = (((rgb & 255) * alpha + (base & 255) * inv) / 255);
  return (r << 16) | (g << 8) | b;
}

uint32_t tileOnFill(const TileDef& d) {
  if (d.hasOnColor) return d.onColor;
  const uint32_t accent = tileAccent(d);
  if (s_theme == THEME_TACTULA && !d.hasColor) return C_OK;
  if (themeUsesModernLayout(s_theme)) return lightTint(accent, tileBg(d), 46);
  return accent;
}

// Sync the selected live colour parts with Home Assistant. Off and unavailable
// lights fall back to the configured static appearance.
void tileIconColor(TileRt& r, const HaEntity* e) {
  const TileDef& d = *r.def;
  if (!r.chip || (d.type != TILE_ACTION && d.type != TILE_TOGGLE &&
                  d.type != TILE_SLIDER && d.type != TILE_SENSOR)) return;
  const bool on = lv_obj_has_state(r.obj, LV_STATE_CHECKED);
  const bool light = entityDomain(d.entity) == "light";
  const bool cinematicLight = light && themeUsesModernLayout(s_theme);
  const bool followIcon = light && (cinematicLight || (d.lightColorMode & 1));
  const bool followBg = light && (d.lightColorMode & 2) && !d.groupPlain;
  HaEntity own;
  if ((followIcon || followBg) && !e && haGetEntity(d.entity, own)) e = &own;
  const bool lit = e && e->known && e->state == "on";
  const uint32_t rgb = lit ? lightColourRgb(*e) : 0xFFFFFFFFu;
  const bool live = rgb != 0xFFFFFFFFu;
  const lv_opa_t iconOpa = !cinematicLight && followIcon && lit && e->brightness >= 0 ?
      (lv_opa_t)(102 + e->brightness * 153 / 255) : LV_OPA_COVER;

  if (followBg) {
    const uint32_t fixed = tileOnFill(d);
    uint32_t fill = fixed;
    if (live) {
      const int strength = 110 + (e->brightness < 0 ? 145 : e->brightness * 145 / 255);
      const int maxBlend = themeUsesModernLayout(s_theme) ? 65 : followIcon ? 150 : 255;
      fill = lightTint(rgb, tileBg(d), min(strength, maxBlend));
    }
    lv_obj_set_style_bg_color(r.obj, lv_color_hex(fill), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(r.obj, lv_color_mix(lv_color_hex(fill), lv_color_black(), 200),
                              LV_STATE_PRESSED | LV_STATE_CHECKED);
    if (themeUsesWallpaper(s_theme) && wallBlur() && tileOnOpa(d) < 50)
      lv_obj_set_style_border_color(r.obj, lv_color_hex(fill), LV_STATE_CHECKED);
    if (r.fill) lv_obj_set_style_bg_color(r.fill, lv_color_hex(live ? fill : tileAccent(d)), 0);
    if (live && !d.hasOnTextColor) {
      const int lum = (((fill >> 16) & 255) * 299 + ((fill >> 8) & 255) * 587 +
                       (fill & 255) * 114) / 1000;
      const bool glass = themeUsesWallpaper(s_theme) && wallBlur();
      const bool darkText = lum * (glass ? tileOnOpa(d) : 100) / 100 >= 115;
      lv_obj_set_style_text_color(r.obj, lv_color_hex(darkText ? C_TEXT_ON : C_TEXT), LV_STATE_CHECKED);
    } else if (!d.hasOnTextColor) {
      lv_obj_set_style_text_color(r.obj, lv_color_hex(onTextColor(d, themeUsesWallpaper(s_theme) && wallBlur())),
                                  LV_STATE_CHECKED);
    }
  }

  if (cinematicLight) {
    const uint32_t tint = lit ? (live ? rgb : 0xFFBF60u) : 0x8290A3u;
    lv_obj_set_style_text_color(r.chip, lv_color_hex(tint), 0);
  } else if (followIcon && live) {
    lv_obj_set_style_text_color(r.chip, lv_color_hex(rgb), 0);
  } else if (on && d.hasOnIconColor) {
    lv_obj_set_style_text_color(r.chip, lv_color_hex(d.onIconColor), 0);
  } else if (on && d.iconPlain) {
    lv_obj_remove_local_style_prop(r.chip, LV_STYLE_TEXT_COLOR, 0);
  } else if (d.hasIconColor) {
    lv_obj_set_style_text_color(r.chip, lv_color_hex(d.iconColor), 0);
  } else {
    styleIconChip(r.chip, tileAccent(d));
  }
  if (lv_obj_get_style_text_opa(r.icon, LV_PART_MAIN) != iconOpa)
    lv_obj_set_style_text_opa(r.icon, iconOpa, 0);
}

// Glass tiles sit over the pre-blurred wallpaper. Ordinary tiles bake their
// off tint into the wallpaper. Toggle tiles draw BOTH tints at runtime so the
// checked colour replaces (rather than overlays) the off colour.
void glassStyle(lv_obj_t* t, const TileDef& def) {
  const int onPct = tileOnOpa(def);
  const lv_opa_t on = (lv_opa_t)(onPct * 255 / 100);
  // Boxed children draw their tint over the single group surface.
  const bool liveTint = def.type == TILE_TOGGLE || def.parent.length();
  const lv_opa_t off = (lv_opa_t)(tileTint(def) * 255 / 100);
  lv_obj_set_style_bg_opa(t, liveTint ? off : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(t, liveTint ? off : LV_OPA_30, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(t, on, LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(t, on, LV_STATE_PRESSED | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(t, 0, 0);
  // A faint fill alone hardly tells on from off: outline it in the accent.
  if (onPct < 50 && s_theme != THEME_TACTULA) {
    lv_obj_set_style_border_width(t, 2, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(t, lv_color_hex(tileOnFill(def)), LV_STATE_CHECKED);
  }
}

// Text on an "on" tile: its own colour, else dark while the fill is bright
// enough, else the normal light text (Glass with a faint fill over a dark picture).
uint32_t onTextColor(const TileDef& def, bool glass) {
  if (def.hasOnTextColor) return def.onTextColor;
  if (def.groupPlain) return tileText(def);
  if (!glass) return C_TEXT_ON;
  const uint32_t fill = tileOnFill(def);
  const uint32_t lum = ((fill >> 16 & 0xFF) * 299 + (fill >> 8 & 0xFF) * 587 + (fill & 0xFF) * 114) / 1000;
  return lum * tileOnOpa(def) / 100 >= 115 ? C_TEXT_ON : C_TEXT;
}

static bool glassNavLink(const TileDef& def) {
  return def.tap.kind == ACT_PAGE && (def.type == TILE_NAV || def.type == TILE_ACTION) &&
         !def.noName && def.navLinkStyle != NavLinkStyle::Classic &&
         (def.navLinkStyle == NavLinkStyle::Glass ||
          (themeUsesModernLayout(s_theme) && def.label.length()));
}

bool tileBare(const TileDef& def) {
  return glassNavLink(def) || def.groupPlain || (def.type == TILE_GROUP && def.groupSurface == 2) ||
      (!def.clockBox && (def.type == TILE_TEXT || def.type == TILE_CLOCK || def.type == TILE_WEATHER_CLOCK || def.type == TILE_TOGGLE ||
       (def.type == TILE_SENSOR && (def.style == METRIC_HEADLINE || def.style == METRIC_WEATHER))));
}

bool tileBaked(const TileDef& def) {
  return !def.parent.length() && !tileBare(def) &&
      !(def.type == TILE_GROUP && def.groupSurface == 1);
}

// Each height shares one 64-pixel strip. The 8×8 dither repeats seamlessly
// across the tile, and LVGL clips the image to the rounded card.
struct CinematicGradient {
  lv_draw_buf_t* image;
  unsigned users;
  int palette;
  uint32_t accent;
};

static std::vector<CinematicGradient> s_cinematicGradients;

static lv_draw_buf_t* acquireCinematicGradient(int height, int palette = 0, uint32_t accent = 0) {
  if (height <= 1) return nullptr;
  for (auto& strip : s_cinematicGradients) {
    if (strip.image->header.h == height && strip.palette == palette && strip.accent == accent) {
      ++strip.users;
      return strip.image;
    }
  }
  constexpr int width = 64;
  lv_draw_buf_t* image = lv_draw_buf_create(width, height, LV_COLOR_FORMAT_RGB565, 0);
  if (!image) return nullptr;
  const auto top = palette == 3 ? lv_color_mix(lv_color_white(), lv_color_hex(accent), 35) : lv_color_hex(palette == 2 ? modernColour(0x25435F) : palette == 1 ? modernColour(0x203C60) : C_TILE);
  const auto bottom = palette == 3 ? lv_color_mix(lv_color_black(), lv_color_hex(accent), 45) : lv_color_hex(palette == 2 ? modernColour(0x15283E) : palette == 1 ? modernColour(0x0A1527) : C_BG);
  for (int y = 0; y < height; ++y) {
    const auto channel = [=](int start, int end) {
      return (start * (height - 1 - y) + end * y) / (height - 1);
    };
    const int r = channel(top.red, bottom.red);
    const int g = channel(top.green, bottom.green);
    const int b = channel(top.blue, bottom.blue);
    auto* row = static_cast<uint16_t*>(lv_draw_buf_goto_xy(image, 0, y));
    for (int x = 0; x < width; ++x) row[x] = wallDither565(r, g, b, x, y);
  }
  s_cinematicGradients.push_back({image, 1, palette, accent});
  return image;
}

static void releaseCinematicGradient(const lv_draw_buf_t* image) {
  for (auto it = s_cinematicGradients.begin(); it != s_cinematicGradients.end(); ++it) {
    if (it->image != image) continue;
    if (--it->users == 0) {
      lv_image_cache_drop(image);
      lv_draw_buf_destroy(it->image);
      s_cinematicGradients.erase(it);
    }
    return;
  }
}

static void updateCinematicGradient(lv_event_t* event) {
  auto* tile = lv_event_get_target_obj(event);
  const auto* old = static_cast<const lv_draw_buf_t*>(lv_obj_get_style_bg_image_src(tile, LV_PART_MAIN));
  if (lv_event_get_code(event) == LV_EVENT_DELETE) {
    if (old) releaseCinematicGradient(old);
    return;
  }
  const int height = lv_obj_get_height(tile);
  if (old && old->header.h == height) return;
  const auto flags = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  const auto* image = acquireCinematicGradient(height, flags >> 8);
  const auto opacity = static_cast<lv_opa_t>(flags & 255);
  lv_obj_set_style_bg_image_src(tile, image, 0);
  // A translucent gradient needs just one blend over the wallpaper.
  lv_obj_set_style_bg_opa(tile, image && opacity < LV_OPA_COVER ? LV_OPA_TRANSP : opacity, 0);
  if (old) releaseCinematicGradient(old);
}

// Shared opaque surface for Cinematic detail sheets and inset panels.
void cinematicSurface(lv_obj_t* obj, int palette) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(palette == 2 ? modernColour(0x25435F) : palette == 1 ? modernColour(0x203C60) : C_TILE), 0);
  if (themeUsesGlassMode(s_theme)) {
    // Live translucent surface: no per-page frost buffer or full-screen snapshot.
    const int tint = std::max<int>(dispSettings.glassTint, 75);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)(tint * 255 / 100), 0);
    return;
  }
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_image_tiled(obj, true, 0);
  lv_obj_set_style_bg_image_opa(obj, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(obj, updateCinematicGradient, LV_EVENT_SIZE_CHANGED,
                      reinterpret_cast<void*>((static_cast<uintptr_t>(palette) << 8) | LV_OPA_COVER));
  lv_obj_add_event_cb(obj, updateCinematicGradient, LV_EVENT_DELETE, nullptr);
  lv_obj_update_layout(obj);
  if (!lv_obj_get_style_bg_image_src(obj, LV_PART_MAIN)) {
    if (auto* image = acquireCinematicGradient(lv_obj_get_height(obj), palette))
      lv_obj_set_style_bg_image_src(obj, image, 0);
  }
}

// Accent-specific strip, shared across equally sized play buttons.
static void updateMediaButtonGradient(lv_event_t* event) {
  auto* button = lv_event_get_target_obj(event);
  const auto* old = static_cast<const lv_draw_buf_t*>(lv_obj_get_style_bg_image_src(button, LV_PART_MAIN));
  if (lv_event_get_code(event) == LV_EVENT_DELETE) {
    if (old) releaseCinematicGradient(old);
    return;
  }
  const int height = lv_obj_get_height(button);
  if (old && old->header.h == height) return;
  const auto accent = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  auto* image = acquireCinematicGradient(height, 3, accent);
  lv_obj_set_style_bg_image_src(button, image, 0);
  if (old) releaseCinematicGradient(old);
}
void cinematicMediaButtonGradient(lv_obj_t* button, uint32_t accent) {
  lv_obj_set_style_bg_image_tiled(button, true, 0);
  lv_obj_set_style_bg_image_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_image_opa(button, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_add_event_cb(button, updateMediaButtonGradient, LV_EVENT_SIZE_CHANGED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(accent)));
  lv_obj_add_event_cb(button, updateMediaButtonGradient, LV_EVENT_DELETE, nullptr);
}

// Same inset treatment for Studio light, climate and media controls.
void cinematicInsetStyle(lv_obj_t* obj) {
  lv_obj_set_style_radius(obj, 24, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(modernColour(0x45678D)), 0);
  cinematicSurface(obj, 2);
}

void makeTile(lv_obj_t* grid, const TileDef& def, int page, int col, int row, int w, int h) {
  const String domain = entityDomain(def.entity);
  const uint32_t accent = tileAccent(def);
  const bool boxedChild = s_theme == THEME_TACTULA && def.parent.length() && !tileBare(def);
  const bool glass = themeUsesWallpaper(s_theme) && wallBlur() && (tileBaked(def) || boxedChild);

  lv_obj_t* t = lv_obj_create(grid);
  lv_obj_set_grid_cell(t, LV_GRID_ALIGN_STRETCH, col, w, LV_GRID_ALIGN_STRETCH, row, h);
  lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
  int radius = 16;
  if (s_theme == THEME_TACTULA) radius = dispSettings.tactulaRadius;
  if (themeUsesModernLayout(s_theme)) radius = 22;
  lv_obj_set_style_radius(t, radius, 0);
  // Light themes use a hairline to separate the tile from the page.
  lv_obj_set_style_border_width(t, s_theme == THEME_DAYLIGHT ? 1 : 0, 0);
  lv_obj_set_style_border_color(t, lv_color_hex(C_TILE_LINE), 0);
  lv_obj_set_style_pad_all(t, 9, 0);
  const uint32_t onFill = tileOnFill(def);
  lv_obj_set_style_bg_color(t, lv_color_hex(tileBg(def)), 0);
  lv_obj_set_style_bg_color(t, lv_color_hex(onFill), LV_STATE_CHECKED);
  lv_obj_set_style_text_color(t, lv_color_hex(tileText(def)), 0);
  lv_obj_set_style_text_color(t, lv_color_hex(onTextColor(def, glass)), LV_STATE_CHECKED);
  // Press feedback: the card darkens and shrinks a little, eased in 90 ms.
  lv_obj_set_style_bg_color(t, def.hasBgColor ? lv_color_mix(lv_color_hex(def.bgColor), lv_color_black(), 200)
                                              : lv_color_hex(C_TILE_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(t, lv_color_mix(lv_color_hex(onFill), lv_color_black(), 200),
                            LV_STATE_PRESSED | LV_STATE_CHECKED);
  lv_obj_set_style_transform_width(t, -3, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(t, -3, LV_STATE_PRESSED);
  lv_obj_set_style_transition(t, &s_pressTrans, 0);
  lv_obj_set_style_transition(t, &s_pressTrans, LV_STATE_PRESSED);
  lv_obj_add_event_cb(t, fxPressed, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(t, fxPressed, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(t, fxPressed, LV_EVENT_PRESS_LOST, nullptr);
  if (glass) glassStyle(t, def);
  if ((s_theme == THEME_TACTULA) && !tileBare(def)) {
    // An inset outline preserves the content area and group geometry.
    lv_obj_set_style_outline_width(t, 1, 0);
    lv_obj_set_style_outline_pad(t, -1, 0);
    lv_obj_set_style_outline_color(t, lv_color_hex(C_TILE_LINE), 0);
    lv_obj_set_style_outline_width(t, 2, LV_STATE_CHECKED);
    lv_obj_set_style_outline_pad(t, -2, LV_STATE_CHECKED);
    lv_obj_set_style_outline_color(t, lv_color_hex(C_OK), LV_STATE_CHECKED);
  }
  if (themeUsesModernLayout(s_theme) && !tileBare(def)) {
    const bool glassMode = themeUsesGlassMode(s_theme);
    const bool solidGroup = def.type == TILE_GROUP && def.groupSurface == 1;
    const int offDefault = glassMode && !solidGroup ? dispSettings.glassTint : 100;
    const int onDefault = glassMode && !solidGroup ? dispSettings.glassOn : 100;
    const lv_opa_t off = (def.opacity >= 0 ? def.opacity : offDefault) * LV_OPA_COVER / 100;
    const lv_opa_t on = (def.onOpacity >= 0 ? def.onOpacity : onDefault) * LV_OPA_COVER / 100;
    lv_obj_set_style_bg_opa(t, off, 0);
    lv_obj_set_style_bg_opa(t, off, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(t, on, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(t, on, LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_border_color(t, lv_color_hex(accent), LV_STATE_CHECKED);
    if (glassMode) {
      lv_obj_set_style_border_color(t, lv_color_white(), 0);
      lv_obj_set_style_border_opa(t, LV_OPA_30, 0);
      lv_obj_set_style_border_opa(t, LV_OPA_COVER, LV_STATE_CHECKED);
    }
    if (off && !def.hasBgColor && !glassMode) {
      lv_obj_set_style_bg_image_tiled(t, true, 0);
      lv_obj_set_style_bg_image_opa(t, off, 0);
      lv_obj_set_style_bg_image_opa(t, LV_OPA_TRANSP, LV_STATE_PRESSED);
      lv_obj_set_style_bg_image_opa(t, LV_OPA_TRANSP, LV_STATE_CHECKED);
      lv_obj_add_event_cb(t, updateCinematicGradient, LV_EVENT_SIZE_CHANGED, reinterpret_cast<void*>(static_cast<uintptr_t>(off)));
      lv_obj_add_event_cb(t, updateCinematicGradient, LV_EVENT_DELETE, nullptr);
    }
  }
  // Clock without a card: don't draw a tile fill, outline or shadow.
  // Glass-theme backgrounds are also omitted from bakeGlass().
  if (tileBare(def)) {
    lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, LV_STATE_PRESSED | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_border_width(t, 0, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(t, 0, 0);
  }

  if (def.type == TILE_TEXT) {
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_event_cb(t, fxPressed);
    TileRt r = {};
    r.def = &def; r.page = page; r.obj = t;
    r.name = lv_label_create(t);
    String value = def.text;
    if (def.entity.length()) { value.replace("{{state}}", "--"); value.replace("{{unit}}", ""); }
    lv_label_set_text(r.name, value.c_str());
    lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_WRAP);
    const size_t idx = s_tiles.size();
    s_tiles.push_back(r);
    lv_obj_add_event_cb(t, fitEvent, LV_EVENT_SIZE_CHANGED, (void*)(uintptr_t)idx);
    applyHaToTile(s_tiles.back(), true);
    return;
  }
  if (def.type == TILE_GROUP) {
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_event_cb(t, fxPressed);
    lv_obj_set_style_transform_width(t, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(t, 0, LV_STATE_PRESSED);
    TileRt r = {};
    r.def = &def;
    r.page = page;
    r.obj = t;
    r.name = lv_label_create(t);
    lv_label_set_text(r.name, def.label.c_str());
    lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_DOTS);
    const auto* nameFont = std::min(logicalW(), logicalH()) >= 800 ? &deck_font_20 : &deck_font_14;
    lv_obj_set_style_text_font(r.name, nameFont, 0);
    lv_obj_set_width(r.name, LV_PCT(100));
    r.groupBody = lv_obj_create(t);
    lv_obj_remove_style_all(r.groupBody);
    lv_obj_remove_flag(r.groupBody, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(r.groupBody, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_layout(r.groupBody, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_row(r.groupBody, def.groupGap, 0);
    lv_obj_set_style_pad_column(r.groupBody, def.groupGap, 0);
    const size_t idx = s_tiles.size();
    s_tiles.push_back(r);
    lv_obj_add_event_cb(t, fitEvent, LV_EVENT_SIZE_CHANGED, (void*)(uintptr_t)idx);
    return;
  }

#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  if (def.type == TILE_BAMBU) {
    TileRt r = {};
    r.def = &def;
    r.page = page;
    r.obj = t;
    const size_t idx = s_tiles.size();
    s_tiles.push_back(r);
    makeBambu(s_tiles.back(), t, idx);
    applyBambu(s_tiles.back());
    lv_obj_add_event_cb(t, fitEvent, LV_EVENT_SIZE_CHANGED, (void*)(uintptr_t)idx);
    return;
  }
#endif

  if (def.type == TILE_HA_MEDIA) {
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
    TileRt r = {};
    r.def = &def;
    r.page = page;
    r.obj = t;
    const size_t idx = s_tiles.size();
    s_tiles.push_back(r);
    makeHaMedia(s_tiles.back(), t, idx);
    HaEntity e;
    applyHaMedia(s_tiles.back(), haGetEntity(def.entity, e) ? &e : nullptr);
    lv_obj_add_event_cb(t, fitEvent, LV_EVENT_SIZE_CHANGED, (void*)(uintptr_t)idx);
    return;
  }

  if (def.type == TILE_CLIMATE) {
    // The card's controls live on the tile itself; the empty background
    // does not open another modal and the individual buttons take touches.
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
    TileRt r = {};
    r.def = &def;
    r.page = page;
    r.obj = t;
    const size_t idx = s_tiles.size();
    s_tiles.push_back(r);
    makeInlineClimate(s_tiles.back(), t, idx);
    HaEntity e;
    applyInlineClimate(s_tiles.back(), haGetEntity(def.entity, e) ? &e : nullptr);
    lv_obj_add_event_cb(t, fitEvent, LV_EVENT_SIZE_CHANGED, (void*)(uintptr_t)idx);
    return;
  }

  if (def.type == TILE_SENSOR &&
      (def.style == METRIC_CHART || def.style == METRIC_HEADLINE || def.style == METRIC_WEATHER)) {
    TileRt r = {};
    r.def = &def;
    r.page = page;
    r.obj = t;
    if (def.style == METRIC_WEATHER) makeWeatherContent(r, t);
    else makeMetricContent(r, t);
    const size_t idx = s_tiles.size();
    s_tiles.push_back(r);
    applyHaToTile(s_tiles.back(), true);
    if (def.style == METRIC_CHART) applySensorHistory(s_tiles.back());
    lv_obj_add_event_cb(t, tileEvent, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)idx);
    lv_obj_add_event_cb(t, tileEvent, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)idx);
    lv_obj_add_event_cb(t, fitEvent, LV_EVENT_SIZE_CHANGED, (void*)(uintptr_t)idx);
    return;
  }

  // Weather clock is primarily read-only, but a short tap should open the
  // corresponding weather details just like a Weather sensor tile.
  if (def.type == TILE_WEATHER_CLOCK) {
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    TileRt r = {};
    r.def = &def;
    r.page = page;
    r.obj = t;
    r.icon = lv_label_create(t);
    if (def.hasIconColor)
      lv_obj_set_style_text_color(r.icon, lv_color_hex(def.iconColor), 0);
    if (themeUsesModernLayout(s_theme) && !def.clockBox && !def.parent.length()) {
      r.detail = lv_label_create(t);
      lv_label_set_long_mode(r.detail, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_add_flag(r.detail, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_text_color(r.detail, lv_color_hex(C_DIM), 0);
    }
    r.unit = lv_label_create(t);  // current weather temperature
    lv_obj_set_style_text_opa(r.unit, LV_OPA_90, 0);
    r.name = lv_label_create(t);  // date
    lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_opa(r.name, LV_OPA_90, 0);
    if (def.hasTextColor) {
      lv_obj_set_style_text_color(r.unit, lv_color_hex(def.textColor), 0);
      lv_obj_set_style_text_color(r.name, lv_color_hex(def.textColor), 0);
    }
    r.sub = lv_label_create(t);   // big time
    lv_label_set_text(r.sub, CLOCK_SYNC_TEXT);
    lv_obj_set_style_text_color(r.sub, lv_color_hex(def.hasColor ? def.color : C_CLOCK), 0);
    lv_obj_set_style_text_align(r.sub, LV_TEXT_ALIGN_CENTER, 0);
    const size_t idx = s_tiles.size();
    s_tiles.push_back(r);
    applyWeatherClock(s_tiles.back());
    lv_obj_add_event_cb(t, [](lv_event_t* ev) {
      const int tile = (int)(uintptr_t)lv_event_get_user_data(ev);
      if (tile >= 0 && tile < (int)s_tiles.size()) openModal(tile);
    }, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)idx);
    lv_obj_add_event_cb(t, fitEvent, LV_EVENT_SIZE_CHANGED, (void*)(uintptr_t)idx);
    return;
  }

  // Clock: the time ("color") over the date, centred; fitClock sizes them.
  if (def.type == TILE_CLOCK) {
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);  // read-only
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    TileRt r = {};
    r.def = &def;
    r.page = page;
    r.obj = t;
    lv_obj_t* row = lv_obj_create(t);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_remove_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_t* hm = lv_obj_create(row);
    lv_obj_remove_style_all(hm);
    lv_obj_set_size(hm, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hm, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(hm, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    if (def.hasColor) lv_obj_set_style_text_color(hm, lv_color_hex(def.color), 0);
    r.sub = lv_label_create(hm);
    r.colon = lv_label_create(hm);
    lv_label_set_text(r.colon, ":");
    r.colonLit = true;
    r.mins = lv_label_create(hm);
    r.unit = lv_label_create(row);
    lv_obj_set_style_text_opa(r.unit, LV_OPA_70, 0);
    r.name = lv_label_create(t);
    lv_obj_set_style_text_opa(r.name, LV_OPA_70, 0);
    const size_t idx = s_tiles.size();
    s_tiles.push_back(r);
    applyClock(s_tiles.back());
    lv_obj_add_event_cb(t, fitEvent, LV_EVENT_SIZE_CHANGED, (void*)(uintptr_t)idx);
    return;
  }

  // Slider: the level fill goes in first so it stays behind the content.
  lv_obj_t* fill = nullptr;
  if (def.type == TILE_SLIDER) {
    fill = lv_obj_create(t);
    lv_obj_remove_style_all(fill);
    lv_obj_add_flag(fill, (lv_obj_flag_t)(LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN));
    lv_obj_remove_flag(fill, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_radius(fill, radius, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(fill, s_theme == THEME_DAYLIGHT ? LV_OPA_40 : LV_OPA_50, 0);
    lv_obj_add_flag(t, LV_OBJ_FLAG_PRESS_LOCK);  // the drag may leave the tile
  }

  lv_obj_t* chip = lv_obj_create(t);
  lv_obj_remove_flag(chip, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_size(chip, 30, 30);
  const bool sensorCard = def.type == TILE_SENSOR && def.style == METRIC_CARD;
  lv_obj_set_style_radius(chip, sensorCard ? 9 : LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(chip, sensorCard ? 1 : 0, 0);
  if (sensorCard) {
    lv_obj_set_style_border_color(chip, lv_color_hex(lightTint(accent, C_TILE_LINE, 55)), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  }
  lv_obj_set_style_pad_all(chip, 0, 0);
  styleIconChip(chip, accent);
  if (def.iconPlain)
    lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, 0);
  // Navigation's chip is only an invisible positioning container for the
  // glyph. fitNav can expand it to the entire inner tile on both S3 and P4;
  // any chip border/outline then looks like a second nested navigation tile.
  // Keep the normal outer tile outline and press feedback untouched.
  if (def.type == TILE_NAV) {
    lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_outline_width(chip, 0, 0);
    lv_obj_set_style_shadow_width(chip, 0, 0);
  }
  if (def.hasIconColor) lv_obj_set_style_text_color(chip, lv_color_hex(def.iconColor), 0);
  lv_obj_t* ic = lv_label_create(chip);
  lv_obj_center(ic);

  // Fonts and places come from fitTile once the tile has its size.
  lv_obj_t* name = lv_label_create(t);
  lv_label_set_text(name, def.noName ? "" : tileLabel(def).c_str());
  lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_t* sub = lv_label_create(t);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);
  if (def.type == TILE_SENSOR) {
    lv_obj_set_style_text_opa(name, LV_OPA_70, 0);
  } else {
    lv_obj_set_style_text_opa(sub, LV_OPA_70, 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_RIGHT, 0);
  }

  const int iconPx = def.type == TILE_SENSOR ? 16 : def.type == TILE_NAV ? 28 : 20;
  TileRt r = {};
  r.def = &def;
  r.page = page;
  r.obj = t;
  r.name = name;
  r.sub = sub;
  r.icon = ic;
  r.chip = chip;
  r.iconPx = iconPx;
  r.iconCp = showIcon(ic, def, nullptr, false, iconPx, 0);
  if (themeUsesModernLayout(s_theme) && domain == "light")
    lv_obj_set_style_text_color(chip, lv_color_hex(0x8290A3), 0);
  r.fill = fill;
  if (glassNavLink(def)) {
    auto* face = r.navFace = lv_obj_create(t);
    lv_obj_remove_style_all(face);
    lv_obj_remove_flag(face, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_move_to_index(face, 0);
    lv_obj_set_style_radius(face, 16, 0);
    // Navigation faces are children of a bare tile, so they must explicitly
    // inherit the same Glass tint as the other modern card surfaces.
    const bool glassMode = themeUsesGlassMode(s_theme);
    const uint32_t faceBg = glassMode ? tileBg(def) : (def.hasBgColor ? def.bgColor : C_PANEL);
    const lv_opa_t faceOpa = glassMode
        ? (lv_opa_t)(tileTint(def) * LV_OPA_COVER / 100)
        : (lv_opa_t)(def.opacity >= 0 ? def.opacity * LV_OPA_COVER / 100 : LV_OPA_80);
    lv_obj_set_style_bg_color(face, lv_color_hex(faceBg), 0);
    lv_obj_set_style_bg_opa(face, faceOpa, 0);
    lv_obj_set_style_bg_color(face, lv_color_hex(lightTint(accent, faceBg, 65)), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(face, glassMode ? faceOpa : LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(face, glassMode ? lv_color_white() : lv_color_hex(C_TILE_LINE), 0);
    if (glassMode) lv_obj_set_style_outline_opa(face, LV_OPA_30, 0);
    lv_obj_set_style_outline_width(face, 1, 0);
    lv_obj_set_style_outline_pad(face, -1, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(tileText(def)), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, 0);
    r.navChevron = lv_label_create(t);
    lv_label_set_text(r.navChevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(r.navChevron, lv_color_hex(tileText(def)), 0);
    lv_obj_set_style_text_font(r.navChevron, LV_FONT_DEFAULT, 0);
    for (const auto code : {LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
      lv_obj_add_event_cb(t, [](lv_event_t* e) {
        auto* surface = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
        if (lv_event_get_code(e) == LV_EVENT_PRESSED) lv_obj_add_state(surface, LV_STATE_PRESSED);
        else lv_obj_remove_state(surface, LV_STATE_PRESSED);
      }, code, face);
  }
  if (def.type == TILE_SENSOR) {
    r.detail = makeDetail(t, def);
    r.rows = makeRows(t, def);
  }
  lv_label_set_text(sub, tileSub(r).c_str());
  const size_t idx = s_tiles.size();
  s_tiles.push_back(r);
  lv_obj_add_event_cb(t, fitEvent, LV_EVENT_SIZE_CHANGED, (void*)(uintptr_t)idx);
  lv_obj_add_event_cb(t, tileEvent, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)idx);
  lv_obj_add_event_cb(t, tileEvent, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)idx);
  if (fill)
    for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST,
                              LV_EVENT_SIZE_CHANGED})
      lv_obj_add_event_cb(t, sliderEvent, c, (void*)(uintptr_t)idx);
}

}  // namespace deckui

