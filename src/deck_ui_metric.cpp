// Home Assistant and HTTP sensor charts, rows and clock layout.
#include "debug_log.h"
#include "deck_ui_int.h"

namespace deckui {

lv_obj_t* makeRows(lv_obj_t* t, const TileDef& d) {
  if (!d.http.enabled || d.rows.empty()) return nullptr;
  const auto plain = [](lv_obj_t* o) {
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  };
  lv_obj_t* rows = lv_obj_create(t);
  lv_obj_remove_style_all(rows);
  plain(rows);
  for (const SensorRowDef& row : d.rows) {
    lv_obj_t* box = lv_obj_create(rows);
    lv_obj_remove_style_all(box);
    plain(box);
    lv_obj_t* label = lv_label_create(box);
    lv_label_set_text(label, row.label.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_opa(label, LV_OPA_70, 0);
    lv_obj_t* bar = lv_bar_create(box);
    plain(bar);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t* value = lv_label_create(box);
    lv_label_set_text(value, "--");
    lv_label_set_long_mode(value, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
  }
  return rows;
}

lv_obj_t* makeDetail(lv_obj_t* t, const TileDef& d) {
  if (!d.http.enabled || !d.detail.length()) return nullptr;
  lv_obj_t* l = lv_label_create(t);
  lv_label_set_text(l, "");
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_opa(l, LV_OPA_70, 0);
  return l;
}

// Number big at the bottom, or a chart / bar under a smaller number (wide
// tiles only; fitMetric hides the chart on small ones).
void makeMetricContent(TileRt& r, lv_obj_t* t) {
  const TileDef& d = *r.def;
  const MetricStyle style = d.style;
  const uint32_t accent = sensorLine(r);

  r.name = lv_label_create(t);
  lv_label_set_text(r.name, d.noName ? "" : d.label.length() ? d.label.c_str() : tileLabel(d).c_str());
  // On glass the accent alone is hard to read: lift it toward white.
  const lv_color_t nameCol = lv_color_hex(accent);
  lv_obj_set_style_text_color(r.name, themeUsesWallpaper(s_theme) ? lv_color_mix(lv_color_white(), nameCol, 110) : nameCol, 0);
  lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_DOTS);

  // Value and unit side by side, bottoms aligned.
  lv_obj_t* row = lv_obj_create(t);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(row, style == METRIC_HEADLINE ? 0 : 4, 0);
  lv_obj_remove_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  r.sub = lv_label_create(row);
  lv_label_set_text(r.sub, "--");
  r.unit = lv_label_create(row);
  lv_label_set_text(r.unit, "");
  lv_obj_set_style_text_opa(r.unit, LV_OPA_70, 0);
  r.detail = makeDetail(t, d);

  // Fonts and places come from fitMetric once the tile has its size.
  if (style == METRIC_NUMBER || style == METRIC_HEADLINE) return;
  if (style == METRIC_BAR) {
    r.viz = lv_bar_create(t);
    lv_obj_set_size(r.viz, lv_pct(100), 8);
    lv_obj_align(r.viz, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(r.viz, 0, 100);
    lv_obj_set_style_bg_color(r.viz, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r.viz, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(r.viz, lv_color_hex(sensorLine(r)), LV_PART_INDICATOR);
    return;
  }
  // Native LVGL chart for Home Assistant and HTTP sensor history.
  r.viz = lv_chart_create(t);
  lv_obj_remove_style_all(r.viz);
  lv_obj_set_size(r.viz, lv_pct(100), lv_pct(62));
  lv_obj_align(r.viz, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_chart_set_type(r.viz, LV_CHART_TYPE_LINE);
  lv_chart_set_div_line_count(r.viz, 0, 0);
  const int count = d.http.enabled ? HTTP_HISTORY_POINTS : HA_HISTORY_POINTS;
  lv_chart_set_point_count(r.viz, count);
  lv_obj_set_style_line_width(r.viz, 2, LV_PART_ITEMS);
  lv_obj_set_style_width(r.viz, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(r.viz, 0, LV_PART_INDICATOR);
  lv_obj_remove_flag(r.viz, LV_OBJ_FLAG_CLICKABLE);
  r.ser = lv_chart_add_series(r.viz, lv_color_hex(sensorLine(r)), LV_CHART_AXIS_PRIMARY_Y);
  r.pts = (int32_t*)lv_malloc(sizeof(int32_t) * count);
  if (!r.pts) { lv_obj_delete(r.viz); r.viz = nullptr; r.ser = nullptr; return; }
  for (int i = 0; i < count; i++) r.pts[i] = LV_CHART_POINT_NONE;
  lv_chart_set_series_ext_y_array(r.viz, r.ser, r.pts);
}


// A climate entity's state is its HVAC mode ("heat", "auto", "off"), NOT
// a temperature. The headline temperature widget must use the live
// current_temperature attribute and must never substitute the target.
String climateCurrentUnit(const HaEntity& e) {
  // Some integrations include temperature_unit; weatherTempUnit is the HA
  // store field for that attribute. Most climate integrations do not supply
  // it, so fall back to the local deck's Celsius convention.
  if (e.weatherTempUnit.length()) return e.weatherTempUnit;
  if (e.unit.length()) return e.unit;
  return "\xC2\xB0" "C";
}

String climateCurrentText(const HaEntity& e, bool withUnit) {
  if (!e.known || e.state == "unavailable" || e.state == "unknown" ||
      !isfinite(e.current)) return "--";
  char text[32];
  formatReading(e.current, text, sizeof(text));
  return withUnit ? String(text) + climateCurrentUnit(e) : String(text);
}

const char* weatherIcon(const String& state) {
  // Home Assistant normalises the weather.* state, but its "partlycloudy"
  // state does not tell us whether to draw a sun or moon. Follow the actual
  // sun.sun state instead of hard-coding a night-time clock interval.
  HaEntity sun;
  const bool night = state == "clear-night" ||
      (haGetEntity("sun.sun", sun) && sun.known && sun.state == "below_horizon");
  if (state == "sunny") return night ? "weather-night" : "weather-sunny";
  if (state == "clear-night") return "weather-night";
  if (state == "cloudy") return "weather-cloudy";
  if (state == "partlycloudy") return night ? "weather-night-partly-cloudy"
                                            : "weather-partly-cloudy";
  if (state == "fog") return "weather-fog";
  if (state == "hail") return "weather-hail";
  if (state == "lightning") return "weather-lightning";
  if (state == "lightning-rainy") return "weather-lightning-rainy";
  if (state == "pouring") return "weather-pouring";
  if (state == "rainy") return "weather-rainy";
  if (state == "snowy") return "weather-snowy";
  if (state == "snowy-rainy") return "weather-snowy-rainy";
  if (state == "windy" || state == "windy-variant") return "weather-windy";
  if (state == "exceptional") return "weather-hurricane";
  return night ? "weather-night-partly-cloudy" : "weather-partly-cloudy";
}

String weatherCondition(const String& state) {
  if (state == "partlycloudy") return "Partly cloudy";
  if (state == "clear-night") return "Clear night";
  if (state == "lightning-rainy") return "Thunderstorms";
  if (state == "snowy-rainy") return "Sleet";
  if (state == "unknown" || state == "unavailable" || !state.length()) return "Unavailable";
  String s = state;
  s.replace("-", " ");
  s[0] = toupper((unsigned char)s[0]);
  return s;
}

String weatherTempText(const HaEntity& e, bool withUnit, bool apparent) {
  const float temperature = apparent ? e.weatherFeels : e.weatherTemp;
  if (!e.known || e.state == "unavailable" || e.state == "unknown" ||
      !isfinite(temperature)) return "--";
  char v[24];
  formatReading(temperature, v, sizeof(v));
  return withUnit ? String(v) + e.weatherTempUnit : String(v);
}

// A compact weather tile uses the same low-overhead labels as a sensor,
// rather than a chart or a downloaded icon/image.
void makeWeatherContent(TileRt& r, lv_obj_t* tile) {
  r.name = lv_label_create(tile);
  lv_label_set_text(r.name, tileLabel(*r.def).c_str());
  lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_opa(r.name, LV_OPA_80, 0);
  lv_obj_t* row = lv_obj_create(tile);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_remove_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  r.sub = lv_label_create(row);
  lv_label_set_text(r.sub, "--");
  r.unit = lv_label_create(row);
  lv_label_set_text(r.unit, "");
  lv_obj_set_style_text_opa(r.unit, LV_OPA_80, 0);
  r.detail = lv_label_create(tile);
  lv_label_set_text(r.detail, "Waiting for weather");
  lv_label_set_long_mode(r.detail, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_opa(r.detail, LV_OPA_70, 0);
  r.chip = lv_obj_create(tile);
  lv_obj_remove_flag(r.chip, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_style_radius(r.chip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(r.chip, 0, 0);
  lv_obj_set_style_pad_all(r.chip, 0, 0);
  styleIconChip(r.chip, r.def->hasColor ? r.def->color : domainColor("weather"));
  r.icon = lv_label_create(r.chip);
  lv_obj_center(r.icon);
  r.iconPx = 28;
  r.iconCp = showIcon(r.icon, *r.def, nullptr, false, r.iconPx, 0);
}

// Match the same weather state to one of the Meteocons static filenames.
static const char* weatherClockSlug(const String& state) {
  HaEntity sun;
  const bool night = state == "clear-night" ||
      (haGetEntity("sun.sun", sun) && sun.known && sun.state == "below_horizon");
  if (state == "sunny") return night ? "clear-night" : "clear-day";
  if (state == "clear-night") return "clear-night";
  if (state == "partlycloudy") return night ? "partly-cloudy-night" : "partly-cloudy-day";
  if (state == "cloudy") return "cloudy";
  if (state == "fog") return "fog";
  if (state == "hail") return "hail";
  if (state == "rainy" || state == "pouring") return "rain";
  if (state == "lightning" || state == "lightning-rainy") return "thunderstorms";
  if (state == "snowy" || state == "snowy-rainy") return "snow";
  if (state == "windy" || state == "windy-variant") return "wind";
  return night ? "partly-cloudy-night" : "partly-cloudy-day";
}

String outlineWeatherSlug(const String& state) {
  String slug = weatherClockSlug(state);
  // Tactula Outline extends the original Skycons-compatible filenames.
  // Leave static Meteocons mappings untouched.
  if (state == "hail") slug = "hail";
  if (state == "snowy-rainy") slug = "sleet";
  if (state == "lightning") slug = "lightning";
  if (state == "lightning-rainy") slug = "thunderstorms";
  if (state == "pouring") slug = "pouring";
  if (state == "windy-variant") slug = "windy-variant";
  if (state == "exceptional") slug = "exceptional";
  return slug;
}

static constexpr int SKYCONS_FRAME_SIZE = 64;
static constexpr int SKYCONS_FRAME_COUNT = 8;
static uint8_t s_skyconsFrame = 0;

#if defined(BOARD_IS_JC8012P4A1C)
struct SkyconsFrames {
  lv_draw_buf_t* frames[SKYCONS_FRAME_COUNT]{};
  // HD popup: retain only the displayed frame; decode its replacement
  // before releasing it, so peak retained artwork is two 160px buffers.
  String hdPath;
  int hdIndex = -1;
  ~SkyconsFrames() {
    for (auto* frame : frames) {
      if (!frame) continue;
      lv_image_cache_drop(frame);
      lv_draw_buf_destroy(frame);
    }
  }
};

static void clearSkyconsFrames(lv_obj_t* image) {
  auto* frames = static_cast<SkyconsFrames*>(lv_obj_get_user_data(image));
  if (!frames) return;
  lv_image_set_src(image, nullptr);
  lv_obj_set_user_data(image, nullptr);
  delete frames;
}

// Area-average the sprite once at its display size. Weight RGB by alpha so
// transparent pixels do not darken the edges of the icon.
static void resizeSkyconsFrame(const lv_draw_buf_t& atlas, int index, lv_draw_buf_t& frame) {
  const int size = frame.header.w;
  const int extent = atlas.header.h;
  for (int y = 0; y < size; ++y) {
    uint8_t* out = frame.data + y * frame.header.stride;
    const int top = y * extent, bottom = top + extent;
    for (int x = 0; x < size; ++x, out += 4) {
      const int left = x * extent, right = left + extent;
      unsigned opacity = 0, color[3]{};
      for (int sy = top / size; sy * size < bottom; ++sy) {
        const unsigned dy = std::min(bottom, (sy + 1) * size) - std::max(top, sy * size);
        const uint8_t* row = atlas.data + sy * atlas.header.stride + index * extent * 4;
        for (int sx = left / size; sx * size < right; ++sx) {
          const unsigned dx = std::min(right, (sx + 1) * size) - std::max(left, sx * size);
          const uint8_t* pixel = row + sx * 4;
          const unsigned weight = dx * dy * pixel[3];
          opacity += weight;
          for (int channel = 0; channel < 3; ++channel) color[channel] += weight * pixel[channel];
        }
      }
      for (int channel = 0; channel < 3; ++channel) {
        out[channel] = opacity ? (color[channel] + opacity / 2) / opacity : 0;
      }
      const unsigned area = extent * extent;
      out[3] = (opacity + area / 2) / area;
    }
  }
}

static String hdFramePath(const String& atlas, int frame) {
  return atlas.substring(0, atlas.length() - 8) + "_f" + String(frame) + "_off.png";
}

static void loadSkyconsFrames(lv_obj_t* image, const String& path, int size) {
  if (path.indexOf("/wc_outline_hd_") >= 0) {
    // Never decode the legacy HD atlas: its 800 KiB output alone exceeds
    // the available contiguous heap on a busy P4.
    for (int i = 0; i < SKYCONS_FRAME_COUNT; ++i) {
      if (!deck::wallFs().exists(hdFramePath(path, i))) {
        debugSerial.printf("Deck: HD Skycons frame missing; reinstall pack: %s\n",
                           hdFramePath(path, i).c_str());
        return;
      }
    }
    auto* frames = new (std::nothrow) SkyconsFrames;
    if (!frames) return;
    bool opened = false;
    auto* frame = loadTilePng(hdFramePath(path, s_skyconsFrame), true, opened);
    if (!frame || frame->header.w != 160 || frame->header.h != 160) {
      if (frame) lv_draw_buf_destroy(frame);
      delete frames;
      return;
    }
    frames->frames[0] = frame;
    frames->hdPath = path;
    frames->hdIndex = s_skyconsFrame;
    lv_obj_set_user_data(image, frames);
    debugSerial.printf("Deck: HD Skycons bounded frame loading ready: %s\n", path.c_str());
    return;
  }
  bool opened = false;
  auto* atlas = loadTilePng(path, true, opened);
  if (!atlas) return;
  const bool valid = (atlas->header.h == SKYCONS_FRAME_SIZE || atlas->header.h == 160) &&
                     atlas->header.w == atlas->header.h * SKYCONS_FRAME_COUNT;
  auto* frames = valid ? new (std::nothrow) SkyconsFrames : nullptr;
  if (frames) {
    for (int i = 0; i < SKYCONS_FRAME_COUNT; ++i) {
      auto*& frame = frames->frames[i];
      frame = lv_draw_buf_create(size, size, LV_COLOR_FORMAT_ARGB8888, 0);
      if (!frame) {
        delete frames;
        frames = nullptr;
        break;
      }
      resizeSkyconsFrame(*atlas, i, *frame);
    }
  }
  lv_draw_buf_destroy(atlas);
  if (!frames)
    debugSerial.printf("Deck: Skycons %s: %s\n", path.c_str(),
                       valid ? "frame allocation failed" : "invalid Skycons atlas dimensions");
  lv_obj_set_user_data(image, frames);
}
#endif

static bool showSkyconsFrame(lv_obj_t* image) {
#if defined(BOARD_IS_JC8012P4A1C)
  auto* frames = static_cast<SkyconsFrames*>(lv_obj_get_user_data(image));
  if (!frames) return false;
  if (frames->hdIndex >= 0) {
    if (frames->hdIndex == s_skyconsFrame) {
      if (lv_image_get_src(image) != frames->frames[0])
        lv_image_set_src(image, frames->frames[0]);
      return lv_image_get_src(image) == frames->frames[0];
    }
    bool opened = false;
    auto* next = loadTilePng(hdFramePath(frames->hdPath, s_skyconsFrame), true, opened);
    if (!next || next->header.w != 160 || next->header.h != 160) {
      if (next) lv_draw_buf_destroy(next);
      return false;
    }
    auto* old = frames->frames[0];
    lv_image_set_src(image, next);
    if (lv_image_get_src(image) != next) {
      lv_image_cache_drop(next);
      lv_draw_buf_destroy(next);
      return false;
    }
    frames->frames[0] = next;
    frames->hdIndex = s_skyconsFrame;
    lv_image_cache_drop(old);
    lv_draw_buf_destroy(old);
    return true;
  }
  const auto* frame = frames->frames[s_skyconsFrame];
  if (lv_image_get_src(image) != frame) lv_image_set_src(image, frame);
#else
  const int frameWidth = SKYCONS_FRAME_SIZE * lv_image_get_scale(image) / LV_SCALE_NONE;
  lv_obj_set_x(image, -s_skyconsFrame * frameWidth);
#endif
  return true;
}

void fitWeatherClockImage(TileRt& r, int x, int y, int iconW, int headerH, int maxFrameSize) {
  if (!r.pngIcon) return;
  if (r.def->weatherIcons != 3) {
    const int sourceSize = std::max(r.pngWidth, r.pngHeight);
    if (!sourceSize) return;
    const int scale = std::min<int>(LV_SCALE_NONE, std::min(iconW, headerH) * LV_SCALE_NONE / sourceSize);
    lv_image_set_scale(r.pngIcon, std::max(1, scale));
    lv_obj_set_pos(r.pngIcon, x + (iconW - r.pngWidth) / 2, y + (headerH - r.pngHeight) / 2);
    return;
  }
  const int size = std::min({maxFrameSize, iconW, headerH});
#if defined(BOARD_IS_JC8012P4A1C)
  if (r.pngSource.isEmpty()) return;
  if (r.pngWidth != size) {
    clearSkyconsFrames(r.pngIcon);
    r.pngWidth = size;
    r.pngFailures = 0;
  }
  if (!lv_obj_get_user_data(r.pngIcon) && (!r.pngFailures || tilePngRetryDue(r, millis()))) {
    loadSkyconsFrames(r.pngIcon, r.pngSource.substring(2), size);
    if (lv_obj_get_user_data(r.pngIcon)) {
      debugSerial.printf("Deck: Skycons %s ready (%u frames)\n",
                         r.pngSource.c_str(), (unsigned)SKYCONS_FRAME_COUNT);
      r.pngFailures = 0;
    } else {
      recordTilePngFailure(r);
    }
  }
  const bool ready = lv_obj_get_user_data(r.pngIcon) != nullptr;
  showObj(r.pngIcon, ready);
  showObj(r.icon, !ready);
  if (!ready) return;
  lv_obj_set_size(r.pngIcon, size, size);
  lv_obj_set_pos(r.pngIcon, x + (iconW - size) / 2, y + (headerH - size) / 2);
#else
  lv_image_set_scale(r.pngIcon, size * LV_SCALE_NONE / SKYCONS_FRAME_SIZE);
  // Clip the scaled strip to one frame and centre it in the weather icon slot.
  lv_obj_t* viewport = lv_obj_get_parent(r.pngIcon);
  lv_obj_set_size(viewport, size, size);
  lv_obj_set_pos(viewport, x + (iconW - size) / 2, y + (headerH - size) / 2);
#endif
  showSkyconsFrame(r.pngIcon);
}

static void hideWeatherClockImage(TileRt& r) {
  if (r.pngIcon) {
    showObj(r.pngIcon, false);
#if defined(BOARD_IS_JC8012P4A1C)
    if (r.def->weatherIcons == 3 && !r.pngSource.isEmpty()) {
      clearSkyconsFrames(r.pngIcon);
      r.pngSource = "";
      r.pngWidth = 0;
      r.pngFailures = 0;
    }
#endif
  }
  showObj(r.icon, true);
}

// Use a compact full-colour PNG on microSD, falling back to MDI if absent.
// The existing icon upload callback refreshes deck pages after a pack install.
static void updateWeatherClockImage(TileRt& r, const String& state, bool reloadSkycons, bool preferHd = false) {
  if (!r.def->weatherIcons || !deck::wallOnSd()) {
    hideWeatherClockImage(r);
    return;
  }
  const bool skycons = r.def->weatherIcons == 3;
  const char* style = r.def->weatherIcons == 1 ? "fill" : r.def->weatherIcons == 2 ? "flat" : "outline_anim";
  String slug = weatherClockSlug(state);
  if (skycons) {
    slug = outlineWeatherSlug(state);
  }
  String safe = slug;
  safe.replace("-", "_");
  String source = String("B:/touchdeck/icons/bedroom/wc_") + style + "_" + safe + "_off.png";
#if defined(BOARD_IS_JC8012P4A1C)
  if (skycons && preferHd) {
    const String hd = String("B:/touchdeck/icons/bedroom/wc_outline_hd_") + safe + "_off.png";
    if (deck::wallFs().exists(hdFramePath(hd.substring(2), 0)) ||
        deck::wallFs().exists(hd.substring(2))) source = hd;
  }
#endif
  const String path = source.substring(2);
#if defined(BOARD_IS_JC8012P4A1C)
  const bool checkExists = !skycons;  // The P4 loader reports and retries missing Skycons files.
#else
  const bool checkExists = true;
  (void)reloadSkycons;
#endif
  if (checkExists && !deck::wallFs().exists(path)) {
    hideWeatherClockImage(r);
    return;
  }
  deck::iconsRegisterSdFs();
  if (!r.pngIcon) {
    lv_obj_t* parent = r.obj;
#if !defined(BOARD_IS_JC8012P4A1C)
    if (skycons) {
      parent = lv_obj_create(r.obj);
      lv_obj_remove_style_all(parent);
      lv_obj_remove_flag(parent, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE |
          LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_OVERFLOW_VISIBLE));
    }
#endif
    r.pngIcon = lv_image_create(parent);
    lv_obj_remove_flag(r.pngIcon, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    if (skycons) {
      lv_image_set_inner_align(r.pngIcon, LV_IMAGE_ALIGN_TOP_LEFT);
      lv_image_set_pivot(r.pngIcon, 0, 0);
#if defined(BOARD_IS_JC8012P4A1C)
      lv_obj_add_event_cb(r.pngIcon, [](lv_event_t* event) {
        auto* image = lv_event_get_target_obj(event);
        delete static_cast<SkyconsFrames*>(lv_obj_get_user_data(image));
      }, LV_EVENT_DELETE, nullptr);
#endif
    }
  }
#if defined(BOARD_IS_JC8012P4A1C)
  if (skycons) {
    if (reloadSkycons || r.pngSource != source) {
      clearSkyconsFrames(r.pngIcon);
      r.pngSource = source;
      r.pngWidth = 0;  // The next fit loads frames at the display size.
      r.pngFailures = 0;
    }
    const bool ready = lv_obj_get_user_data(r.pngIcon) != nullptr;
    showObj(r.pngIcon, ready);
    showObj(r.icon, !ready);
    return;
  }
#endif
  if (r.pngSource != source) {
    lv_image_header_t info = {};
    if (lv_image_decoder_get_info(source.c_str(), &info) != LV_RESULT_OK ||
        info.w == 0 || info.h == 0 ||
        (skycons && (info.w != SKYCONS_FRAME_COUNT * SKYCONS_FRAME_SIZE || info.h != SKYCONS_FRAME_SIZE))) {
      lv_obj_add_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(r.icon, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    r.pngWidth = info.w;
    r.pngHeight = info.h;
    r.pngSource = source;
    lv_image_set_src(r.pngIcon, source.c_str());
    lv_obj_set_size(r.pngIcon, r.pngWidth, r.pngHeight);
  }
  lv_obj_remove_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(r.icon, LV_OBJ_FLAG_HIDDEN);
}

// Modal artwork owns its own image and frame cache. Closing the modal deletes
// the image (and its frame buffers), while the underlying clock stays untouched.
static TileRt s_modalWeatherArt{};
static TileDef s_modalWeatherArtDef{};
static String s_modalWeatherArtState;
static bool s_modalWeatherHdFailed = false;
static int s_modalWeatherArtSize = 0;

void resetModalWeatherArtwork() {
  s_modalWeatherArt = TileRt{};
  s_modalWeatherArtState = "";
  s_modalWeatherHdFailed = false;
  s_modalWeatherArtSize = 0;
}

void updateModalWeatherArtwork(lv_obj_t* parent, lv_obj_t* fallback,
                               const TileDef& source, const String& state, int size) {
  if (!parent || !fallback || size <= 0) return;
  auto& art = s_modalWeatherArt;
  if (art.obj != parent) {
    resetModalWeatherArtwork();
    s_modalWeatherArtDef.weatherIcons = source.weatherIcons;
    art.def = &s_modalWeatherArtDef;
    art.obj = parent;
    art.icon = fallback;
  }
  s_modalWeatherArtSize = size;
  if (s_modalWeatherArtState != state) {
    s_modalWeatherArtState = state;
    s_modalWeatherHdFailed = false;
  }
  // A failed HD load must not strand the popup on its tiny MDI fallback.
  // Try the ordinary animated pack once, then keep that result until state
  // changes or the popup is reopened.
  updateWeatherClockImage(art, state, false, size > 64 && !s_modalWeatherHdFailed);
  bool hd = art.pngSource.indexOf("/wc_outline_hd_") >= 0;
  fitWeatherClockImage(art, 0, 0, size, size, hd ? 160 : SKYCONS_FRAME_SIZE);
#if defined(BOARD_IS_JC8012P4A1C)
  if (hd && art.pngIcon && !lv_obj_get_user_data(art.pngIcon)) {
    debugSerial.printf("Deck: weather popup HD load failed; trying standard Skycons\n");
    s_modalWeatherHdFailed = true;
    updateWeatherClockImage(art, state, false, false);
    fitWeatherClockImage(art, 0, 0, size, size, SKYCONS_FRAME_SIZE);
  }
#endif
  if (!art.pngIcon || lv_obj_has_flag(art.pngIcon, LV_OBJ_FLAG_HIDDEN)) return;
#if defined(BOARD_IS_JC8012P4A1C)
  if (art.def->weatherIcons == 3) {
    // HD popup retains one native frame; the clock keeps its small cache.
    lv_image_set_pivot(art.pngIcon, 0, 0);
    const auto* frames = static_cast<const SkyconsFrames*>(lv_obj_get_user_data(art.pngIcon));
    const int nativeSize = frames && frames->frames[0] ? frames->frames[0]->header.w : art.pngWidth;
    lv_image_set_scale(art.pngIcon, size * LV_SCALE_NONE / std::max<int>(1, nativeSize));
    lv_obj_set_pos(art.pngIcon, 0, 0);
  } else
#endif
  if (art.def->weatherIcons != 3 && art.pngWidth && art.pngHeight) {
    const int scale = size * LV_SCALE_NONE / std::max(art.pngWidth, art.pngHeight);
    lv_image_set_pivot(art.pngIcon, 0, 0);
    lv_image_set_scale(art.pngIcon, std::max(1, scale));
    lv_obj_set_pos(art.pngIcon, (size - art.pngWidth * scale / LV_SCALE_NONE) / 2,
                   (size - art.pngHeight * scale / LV_SCALE_NONE) / 2);
  }
}

void applyWeatherClock(TileRt& r, bool reloadSkycons) {
  static const char* const days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char* const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  static const char* const fullDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                       "Thursday", "Friday", "Saturday"};
  static const char* const fullMonths[] = {"January", "February", "March", "April", "May", "June",
      "July", "August", "September", "October", "November", "December"};
  char date[32] = "", clockText[16] = {};
  strlcpy(clockText, CLOCK_SYNC_TEXT, sizeof(clockText));
  const time_t epoch = time(nullptr);
  if (epoch >= 1609459200) {
    struct tm tmv;
    localtime_r(&epoch, &tmv);
    if (r.detail) {
      snprintf(date, sizeof(date), "%s, %d %s", fullDays[tmv.tm_wday], tmv.tm_mday, fullMonths[tmv.tm_mon]);
    } else {
      snprintf(date, sizeof(date), "%s, %d %s", days[tmv.tm_wday], tmv.tm_mday, months[tmv.tm_mon]);
    }
    if (netSettings.use24h) snprintf(clockText, sizeof(clockText), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    else snprintf(clockText, sizeof(clockText), "%d:%02d", (tmv.tm_hour + 11) % 12 + 1, tmv.tm_min);
  }
  HaEntity weather;
  const bool known = r.def->entity.length() && haGetEntity(r.def->entity, weather) &&
      weather.known && weather.state != "unavailable" && weather.state != "unknown";
  String temp = known ? weatherTempText(weather, false, r.def->weatherApparent) : String("--");
  if (temp != "--") temp += "\xC2\xB0";
  const String iconName = String("mdi:") + weatherIcon(known ? weather.state : String(""));
  TileDef visual = *r.def;
  visual.icon = iconName;
  const uint32_t previousIcon = r.iconCp;
  const bool previousIconVisible = !lv_obj_has_flag(r.icon, LV_OBJ_FLAG_HIDDEN);
  r.iconCp = showIcon(r.icon, visual, nullptr, false, r.iconPx ? r.iconPx : 28, 0);
  const lv_obj_t* oldImage = r.pngIcon;
  const String oldSource = r.pngSource;
  updateWeatherClockImage(r, known ? weather.state : String(""), reloadSkycons);
  bool conditionChanged = false;
  if (r.detail) {
    String condition = weatherCondition(known ? weather.state : String(""));
    if (r.def->label.length() && !r.def->noName) condition += String("  ·  ") + r.def->label;
    conditionChanged = strcmp(lv_label_get_text(r.detail), condition.c_str()) != 0;
    setText(r.detail, condition.c_str());
  }
  // The layout adds the separator when the date is beside the temperature.
  const char* currentDate = lv_label_get_text(r.name);
  constexpr size_t separatorLen = sizeof(WEATHER_DATE_SEPARATOR) - 1;
  if (strncmp(currentDate, WEATHER_DATE_SEPARATOR, separatorLen) == 0) currentDate += separatorLen;
  const bool dateChanged = strcmp(currentDate, date) != 0;
  const bool changed = reloadSkycons || conditionChanged || r.pngIcon != oldImage ||
      r.pngSource != oldSource ||
      strcmp(lv_label_get_text(r.sub), clockText) != 0 ||
      strcmp(lv_label_get_text(r.unit), temp.c_str()) != 0 || dateChanged;
  if (dateChanged) setText(r.name, date);
  setText(r.unit, temp.c_str());
  setText(r.sub, clockText);
  if (changed || previousIcon != r.iconCp ||
      previousIconVisible != !lv_obj_has_flag(r.icon, LV_OBJ_FLAG_HIDDEN)) {
    r.fitW = 0;
    fitTile(r);
    markPageStale(r.page);
  }
}

// Standard Skycons advance from memory; HD popup decodes one SD frame per tick.
void animateWeatherClocks() {
  static uint32_t previous = 0;
  const uint32_t now = millis();
  if (now - previous < 250) return; // 4 fps, deliberately conservative
  previous = now;
  s_skyconsFrame = (s_skyconsFrame + 1) % SKYCONS_FRAME_COUNT;
  if (sh.modalOpen) {
    auto& art = s_modalWeatherArt;
    if (art.pngIcon && art.def && art.def->weatherIcons == 3 &&
        !lv_obj_has_flag(art.pngIcon, LV_OBJ_FLAG_HIDDEN)) {
      if (!showSkyconsFrame(art.pngIcon) && art.pngSource.indexOf("/wc_outline_hd_") >= 0) {
        debugSerial.printf("Deck: HD Skycons animation frame failed; trying standard Skycons\n");
        s_modalWeatherHdFailed = true;
        updateModalWeatherArtwork(art.obj, art.icon, *art.def,
                                  s_modalWeatherArtState, s_modalWeatherArtSize);
      }
    }
    return;
  }
  if (sh.panelOpen || s_ed.root) return;
  for (TileRt& r : s_tiles) {
    if (r.def->type != TILE_WEATHER_CLOCK || r.def->weatherIcons != 3 ||
        r.off || r.page != sh.active || !r.pngIcon ||
        lv_obj_has_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN)) continue;
    showSkyconsFrame(r.pngIcon);
    markPageStale(r.page);
  }
}

// Clock tile text and startup sync status.
void applyClock(TileRt& r) {
  static const char* const kDay[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* const kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char h[4] = {}, m[4] = {}, date[20] = "";
  const char* ap = "";
  const time_t epoch = time(nullptr);
  const bool syncing = epoch < 1609459200;
  const bool wasSyncing = strcmp(lv_label_get_text(r.sub), CLOCK_SYNC_TEXT) == 0;
  if (!syncing) {
    struct tm lt;
    localtime_r(&epoch, &lt);
    snprintf(h, sizeof(h), netSettings.use24h ? "%02d" : "%d",
             netSettings.use24h ? lt.tm_hour : (lt.tm_hour + 11) % 12 + 1);
    snprintf(m, sizeof(m), "%02d", lt.tm_min);
    if (!netSettings.use24h) ap = lt.tm_hour < 12 ? "AM" : "PM";
    snprintf(date, sizeof(date), "%s %d %s", kDay[lt.tm_wday], lt.tm_mday, kMon[lt.tm_mon]);
  }
  setText(r.sub, syncing ? CLOCK_SYNC_TEXT : h);
  showObj(r.colon, !syncing);
  showObj(r.mins, !syncing);
  showObj(r.unit, !syncing);
  setText(r.mins, m);
  setText(r.unit, ap);
  setText(r.name, date);
  if (syncing != wasSyncing) {
    r.fitW = 0;
    fitTile(r);
  }
  markPageStale(r.page);
}


}  // namespace deckui
