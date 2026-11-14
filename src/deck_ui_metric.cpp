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
  lv_label_set_text(r.name, d.label.length() ? d.label.c_str() : !d.noName ? tileLabel(d).c_str() : "");
  // On glass the accent alone is hard to read: lift it toward white.
  const lv_color_t nameCol = lv_color_hex(accent);
  lv_obj_set_style_text_color(r.name, s_theme == THEME_GLASS ? lv_color_mix(lv_color_white(), nameCol, 110) : nameCol, 0);
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
