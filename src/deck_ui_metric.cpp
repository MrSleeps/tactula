// Home Assistant and HTTP sensor charts, rows and clock layout.
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
  lv_obj_set_style_pad_column(row, 4, 0);
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

String weatherTempText(const HaEntity& e, bool withUnit) {
  if (!e.known || e.state == "unavailable" || e.state == "unknown" ||
      !isfinite(e.weatherTemp)) return "--";
  char v[24];
  formatReading(e.weatherTemp, v, sizeof(v));
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

// Use a compact full-colour PNG on microSD, falling back to MDI if absent.
// The existing icon upload callback refreshes deck pages after a pack install.
static void updateWeatherClockImage(TileRt& r, const String& state) {
  if (!r.def->weatherIcons || !deck::wallOnSd()) {
    if (r.pngIcon) lv_obj_add_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(r.icon, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const bool skycons = r.def->weatherIcons == 3;
  const char* style = r.def->weatherIcons == 1 ? "fill" : r.def->weatherIcons == 2 ? "flat" : "sky";
  String slug = weatherClockSlug(state);
  if (skycons) {
    // Skycons has ten built-in weather states, no lightning or hail variant.
    if (state == "hail" || state == "snowy-rainy") slug = "sleet";
    if (state == "lightning" || state == "lightning-rainy") slug = "rain";
    if (state == "pouring") slug = "rain";
  }
  String safe = slug;
  safe.replace("-", "_");
  const String source = String("B:/touchdeck/icons/bedroom/wc_") + style + "_" + safe + "_off.png";
  const String path = source.substring(2);
  if (!deck::wallFs().exists(path)) {
    if (r.pngIcon) lv_obj_add_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(r.icon, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  deck::iconsRegisterSdFs();
  if (!r.pngIcon) {
    r.pngIcon = lv_image_create(r.obj);
    lv_obj_remove_flag(r.pngIcon, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }
  if (r.pngSource != source) {
    lv_image_header_t info = {};
    if (lv_image_decoder_get_info(source.c_str(), &info) != LV_RESULT_OK ||
        info.w == 0 || info.h == 0 || (skycons && (info.w != 512 || info.h != 64))) {
      lv_obj_add_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(r.icon, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    lv_image_set_src(r.pngIcon, source.c_str());
    r.pngWidth = info.w;
    r.pngHeight = info.h;
    r.pngSource = source;
  }
  // Skycons is an 8-frame horizontal atlas. Shift the decoded image
  // rather than changing files or decoding a PNG for every animation frame.
  const int size = r.iconPx >= 28 ? 30 : 22;
  const uint32_t native = skycons ? 64 : std::max<uint16_t>(1, r.pngWidth);
  const uint32_t scale = std::min<uint32_t>(256, std::min<uint32_t>(
      (uint32_t)size * 256 / native,
      (uint32_t)size * 256 / std::max<uint16_t>(1, r.pngHeight)));
  lv_obj_set_size(r.pngIcon, skycons ? 64 : r.pngWidth, skycons ? 64 : r.pngHeight);
  lv_image_set_scale(r.pngIcon, std::max<uint32_t>(1, scale));
  lv_image_set_offset_x(r.pngIcon, 0);
  // fitWeatherClock() owns the sprite position so it stays centred with
  // the temperature and date, including on subsequent weather updates.
  lv_obj_remove_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(r.icon, LV_OBJ_FLAG_HIDDEN);
}

// Alexa-style weather clock: compact icon/temperature and abbreviated date
// above one large time. Uses the same Home Assistant weather entity and local
// clock settings as the existing widgets; no external icon downloads.
void applyWeatherClock(TileRt& r) {
  static const char* const days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char* const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  char date[28] = "|  --, -- ---", clockText[16] = "--:--";
  const time_t epoch = time(nullptr);
  if (epoch >= 1609459200) {
    struct tm tmv;
    localtime_r(&epoch, &tmv);
    snprintf(date, sizeof(date), "|  %s, %d %s", days[tmv.tm_wday], tmv.tm_mday, months[tmv.tm_mon]);
    if (netSettings.use24h) snprintf(clockText, sizeof(clockText), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    else snprintf(clockText, sizeof(clockText), "%d:%02d", (tmv.tm_hour + 11) % 12 + 1, tmv.tm_min);
  }
  HaEntity weather;
  const bool known = r.def->entity.length() && haGetEntity(r.def->entity, weather) &&
      weather.known && weather.state != "unavailable" && weather.state != "unknown";
  const String temp = known && isfinite(weather.weatherTemp)
      ? weatherTempText(weather, false) + "\xC2\xB0" : String("--");
  const String iconName = String("mdi:") + weatherIcon(known ? weather.state : String(""));
  TileDef visual = *r.def;
  visual.icon = iconName;
  r.iconCp = showIcon(r.icon, visual, nullptr, false, r.iconPx ? r.iconPx : 28, 0);
  updateWeatherClockImage(r, known ? weather.state : String(""));
  const bool changed = strcmp(lv_label_get_text(r.sub), clockText) != 0 ||
      strcmp(lv_label_get_text(r.unit), temp.c_str()) != 0 ||
      strcmp(lv_label_get_text(r.name), date) != 0;
  setText(r.name, date);
  setText(r.unit, temp.c_str());
  setText(r.sub, clockText);
  if (changed) {
    // Refit the header when the temperature changes width, not just on time updates.
    r.fitW = 0;
    fitTile(r);
  }
  markPageStale(r.page);
}

// Advance only visible Skycons atlases. No JavaScript or image file swaps
// happen on the ESP32; the existing sprite is decoded and offset in LVGL.
void animateWeatherClocks() {
  static uint32_t previous = 0;
  static uint8_t frame = 0;
  const uint32_t now = millis();
  if (now - previous < 250) return; // 4 fps, deliberately conservative
  previous = now;
  frame = (frame + 1) & 7;
  if (sh.panelOpen || sh.modalOpen || s_ed.root) return;
  for (TileRt& r : s_tiles) {
    if (r.def->type != TILE_WEATHER_CLOCK || r.def->weatherIcons != 3 ||
        r.off || r.page != sh.active || !r.pngIcon ||
        lv_obj_has_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN)) continue;
    lv_image_set_offset_x(r.pngIcon, frame * 64);
    markPageStale(r.page);
  }
}

// Clock tile text; "--:--" until the time is synced.
void applyClock(TileRt& r) {
  static const char* const kDay[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* const kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char h[4] = "--", m[4] = "--", date[20] = "";
  const char* ap = "";
  const time_t epoch = time(nullptr);
  if (epoch >= 1609459200) {
    struct tm lt;
    localtime_r(&epoch, &lt);
    snprintf(h, sizeof(h), netSettings.use24h ? "%02d" : "%d",
             netSettings.use24h ? lt.tm_hour : (lt.tm_hour + 11) % 12 + 1);
    snprintf(m, sizeof(m), "%02d", lt.tm_min);
    if (!netSettings.use24h) ap = lt.tm_hour < 12 ? "AM" : "PM";
    snprintf(date, sizeof(date), "%s %d %s", kDay[lt.tm_wday], lt.tm_mday, kMon[lt.tm_mon]);
  }
  setText(r.sub, h);
  setText(r.mins, m);
  setText(r.unit, ap);
  setText(r.name, date);
  markPageStale(r.page);
}


}  // namespace deckui
