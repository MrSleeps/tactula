// Inline climate controls: the existing ESP32 thermostat UI, rendered as a
// normal deck tile instead of requiring the climate detail popup.
#include "deck_ui_int.h"

namespace deckui {
namespace {
constexpr uint32_t INLINE_CLIMATE_SEND_MS = 700;
constexpr uint32_t INLINE_CLIMATE_SETTLE_MS = 5000;
constexpr size_t INLINE_MODES = 6;

lv_obj_t* makeControl(lv_obj_t* parent, const char* title, uint32_t accent, bool round) {
  lv_obj_t* button = lv_obj_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(button, round ? LV_RADIUS_CIRCLE : 10, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(themeUsesModernLayout(s_theme) ? modernColour(0x29496A) : C_TILE_PRESSED), 0);
  if (themeUsesModernLayout(s_theme)) {
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(modernColour(0x45678D)), 0);
  }
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(accent), LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(button, lv_color_hex(accent), LV_STATE_PRESSED);
  lv_obj_set_style_text_color(button, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_text_color(button, lv_color_hex(C_TEXT_ON), LV_STATE_CHECKED);
  lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_set_ext_click_area(button, 3);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, title);
  lv_obj_set_style_text_font(label, round ? &deck_font_20 : &deck_font_14, 0);
  lv_obj_center(label);
  return button;
}

bool climateAvailable(const HaEntity* e) {
  return e && e->known && e->state != "unavailable" && e->state != "unknown";
}

bool hasSetpoint(const HaEntity* e) {
  return climateAvailable(e) && e->state != "off" &&
         (isfinite(e->target) || climateRange(*e));
}

float stepSize(const HaEntity& e) {
  return e.tempStep > 0 ? e.tempStep : 0.5f;
}

float snapTemp(const HaEntity& e, float v) {
  const float step = stepSize(e);
  v = roundf(v / step) * step;
  if (isfinite(e.tempMin)) v = std::max(v, e.tempMin);
  if (isfinite(e.tempMax)) v = std::min(v, e.tempMax);
  return v;
}

float inlineTarget(const TileRt& r, const HaEntity& e) {
  const bool range = climateRange(e);
  if (r.climPending && isfinite(r.climWantHigh) == range) return r.climWant;
  return range ? e.targetLow : e.target;
}

bool queueInlineTarget(TileRt& r, const HaEntity& e, float desired) {
  float high = NAN;
  if (climateRange(e)) {
    const float low = inlineTarget(r, e);
    const float currentHigh = r.climPending && isfinite(r.climWantHigh) ? r.climWantHigh : e.targetHigh;
    high = currentHigh + (desired - low);
    if ((isfinite(e.tempMin) && desired < e.tempMin) ||
        (isfinite(e.tempMax) && high > e.tempMax)) return false;
    high = snapTemp(e, high);
  }
  r.climWant = snapTemp(e, desired);
  r.climWantHigh = high;
  r.climPending = true;
  r.climSent = false;
  r.climAt = millis();
  return true;
}

float dialMin(const HaEntity& e) { return isfinite(e.tempMin) ? e.tempMin : 5.0f; }
float dialMax(const HaEntity& e) { return isfinite(e.tempMax) ? e.tempMax : 35.0f; }

bool sendSetpoint(TileRt& r) {
  if (!r.climPending || !isfinite(r.climWant)) return true;
  char a[18], b[18], body[95];
  snprintf(a, sizeof(a), "%.4g", r.climWant);
  if (isfinite(r.climWantHigh)) {
    snprintf(b, sizeof(b), "%.4g", r.climWantHigh);
    snprintf(body, sizeof(body), "{\"target_temp_low\":%s,\"target_temp_high\":%s}", a, b);
  } else {
    snprintf(body, sizeof(body), "{\"temperature\":%s}", a);
  }
  return haLink() == HA_CONNECTED &&
         haCallService("climate", "set_temperature", r.def->entity, body);
}

void onInlineClimate(lv_event_t* ev) {
  const size_t key = (size_t)(uintptr_t)lv_event_get_user_data(ev);
  const size_t index = key / 8, part = key % 8;
  if (index >= s_tiles.size()) return;
  TileRt& r = s_tiles[index];
  if (!r.def || r.def->type != TILE_CLIMATE || !tilePinAccess(r)) return;
  HaEntity e;
  if (!haGetEntity(r.def->entity, e) || !climateAvailable(&e)) {
    deckToastShow("Home Assistant is offline", 1800);
    return;
  }
  if (part < 2) {
    if (!hasSetpoint(&e)) return;
    const float change = part == 0 ? -stepSize(e) : stepSize(e);
    if (!queueInlineTarget(r, e, inlineTarget(r, e) + change)) return;
  } else {
    const size_t mode = part - 2;
    if (mode >= r.climModeCount) return;
    const String& target = r.climModeIds[mode];
    if (haLink() != HA_CONNECTED ||
        !haCallService("climate", "set_hvac_mode", r.def->entity,
                       String("{\"hvac_mode\":\"") + target + "\"}")) {
      deckToastShow("Home Assistant is busy, try again", 1800);
      return;
    }
    // The selected mode is shown immediately until the HA state catches up.
    // As in the original modal, changing HVAC mode discards a queued
    // setpoint: sending it after switching to Off would be contradictory.
    r.climPending = false;
    r.climModePending = target;
    r.climModeAt = millis();
  }
  applyInlineClimate(r, &e);
  markPageStale(r.page);
}

void onInlineDial(lv_event_t* event) {
  const size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
  if (index >= s_tiles.size()) return;
  TileRt& r = s_tiles[index];
  if (!r.climDial || r.climDialSync || !tilePinAccess(r, false)) return;
  const auto code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    r.climDialDragging = true;
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (r.climDialDragging && r.climPending && !r.climSent) r.climAt = millis();
    r.climDialDragging = false;
  } else if (code != LV_EVENT_VALUE_CHANGED) return;
  HaEntity e;
  if (!haGetEntity(r.def->entity, e) || !hasSetpoint(&e) || haLink() != HA_CONNECTED) return;
  if (code == LV_EVENT_VALUE_CHANGED) {
    const float lo = dialMin(e), hi = dialMax(e);
    if (hi <= lo) return;
    const float target = snapTemp(e, lo + (hi - lo) * lv_arc_get_value(r.climDial) / 100.0f);
    if (target != inlineTarget(r, e)) queueInlineTarget(r, e, target);
  }
  applyInlineClimate(r, &e);
  markPageStale(r.page);
}

void updateModes(TileRt& r, const HaEntity* e) {
  if (!climateAvailable(e)) return;  // retain choices through brief disconnects
  String modes[INLINE_MODES];
  size_t count = 0;
  int from = 0;
  while (from < (int)e->hvacModes.length() && count < INLINE_MODES) {
    int comma = e->hvacModes.indexOf(',', from);
    if (comma < 0) comma = e->hvacModes.length();
    String mode = e->hvacModes.substring(from, comma);
    mode.trim();
    if (mode.length()) modes[count++] = mode;
    from = comma + 1;
  }
  bool same = count == r.climModeCount;
  for (size_t i = 0; i < count && same; ++i)
    if (r.climModeIds[i] != modes[i]) same = false;
  if (same) return;

  lv_obj_clean(r.climModeRow);
  const uint32_t accent = r.def->hasColor ? r.def->color : domainColor("climate");
  r.climModeCount = (uint8_t)count;
  for (size_t i = 0; i < INLINE_MODES; ++i) {
    r.climModeBtns[i] = nullptr;
    r.climModeIds[i] = i < count ? modes[i] : "";
  }
  const size_t index = &r - s_tiles.data();
  for (size_t i = 0; i < count; ++i) {
    lv_obj_t* b = makeControl(r.climModeRow, climateWord(modes[i]).c_str(), accent, false);
    lv_obj_add_event_cb(b, onInlineClimate, LV_EVENT_SHORT_CLICKED,
                        (void*)(uintptr_t)(index * 8 + 2 + i));
    r.climModeBtns[i] = b;
  }
  const int w = lv_obj_get_width(r.obj), h = lv_obj_get_height(r.obj);
  if (w > 0 && h > 0) {
    const int pad = tilePadding(w, h);
    fitInlineClimate(r, w - pad * 2, h - pad * 2);
  }
}
}  // namespace

void makeInlineClimate(TileRt& r, lv_obj_t* tile, size_t index) {
  const uint32_t accent = r.def->hasColor ? r.def->color : domainColor("climate");
  if (r.def->climateDial) {
    auto* dial = r.climDial = lv_arc_create(tile);
    lv_arc_set_range(dial, 0, 100);
    lv_arc_set_bg_angles(dial, 135, 45);
    lv_obj_set_style_arc_color(dial, lv_color_mix(lv_color_hex(C_TEXT), lv_color_hex(C_TILE), LV_OPA_20), LV_PART_MAIN);
    lv_obj_set_style_arc_color(dial, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(dial, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_border_color(dial, lv_color_hex(accent), LV_PART_KNOB);
    lv_obj_set_style_opa(dial, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_remove_flag(dial, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dial, LV_OBJ_FLAG_ADV_HITTEST);
    showObj(dial, false);
    for (auto event : {LV_EVENT_PRESSED, LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
      lv_obj_add_event_cb(dial, onInlineDial, event, (void*)(uintptr_t)index);
  }
  r.name = lv_label_create(tile);
  lv_label_set_text(r.name, tileLabel(*r.def).c_str());
  lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_font(r.name, &deck_font_20, 0);
  r.climValue = lv_label_create(tile);
  lv_label_set_text(r.climValue, "--");
  lv_label_set_long_mode(r.climValue, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_font(r.climValue, &deck_font_28, 0);
  lv_obj_set_style_text_align(r.climValue, LV_TEXT_ALIGN_CENTER, 0);
  r.climInfo = lv_label_create(tile);
  lv_label_set_text(r.climInfo, "Connecting...");
  lv_label_set_long_mode(r.climInfo, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_font(r.climInfo, &deck_font_14, 0);
  lv_obj_set_style_text_align(r.climInfo, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(r.climInfo, lv_color_hex(C_DIM), 0);
  for (lv_obj_t* label : {r.name, r.climValue}) {
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, [](lv_event_t* event) {
      const size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(event);
      if (idx < s_tiles.size()) openModal((int)idx);
    }, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)index);
  }
  r.climMinus = makeControl(tile, LV_SYMBOL_MINUS, accent, true);
  r.climPlus = makeControl(tile, LV_SYMBOL_PLUS, accent, true);
  lv_obj_add_event_cb(r.climMinus, onInlineClimate, LV_EVENT_SHORT_CLICKED,
                      (void*)(uintptr_t)(index * 8));
  lv_obj_add_event_cb(r.climPlus, onInlineClimate, LV_EVENT_SHORT_CLICKED,
                      (void*)(uintptr_t)(index * 8 + 1));
  r.climModeRow = lv_obj_create(tile);
  lv_obj_remove_style_all(r.climModeRow);
  lv_obj_remove_flag(r.climModeRow, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  r.climWant = r.climWantHigh = NAN;
  r.climModeCount = 0;
}

static bool fitInlineDial(TileRt& r, int cw, int ch) {
  const auto* nameFont = roomyWidget(cw, ch) ? &deck_font_28 : &deck_font_14;
  const int titleH = lineH(nameFont);
  const int gap = 8;
  const int modeH = r.climModeCount ? std::clamp(ch / 8, 30, 36) : 0;
  const int room = ch - titleH - gap - (modeH ? modeH + gap : 0);
  const int d = std::min({cw - 6, room, 440});
  if (d < 110) return false;

  const int x = (cw - d) / 2;
  const int y = titleH + gap + (room - d) / 2;
  const int stroke = themeUsesModernLayout(s_theme) ? std::max(6, d / 18) : std::max(5, d / 27);
  lv_obj_set_style_text_font(r.name, nameFont, 0);
  lv_obj_set_pos(r.name, 0, 0);
  lv_obj_set_size(r.name, cw, titleH);
  lv_obj_set_pos(r.climDial, x, y);
  lv_obj_set_size(r.climDial, d, d);
  lv_obj_set_style_arc_width(r.climDial, stroke, LV_PART_MAIN);
  lv_obj_set_style_arc_width(r.climDial, stroke, LV_PART_INDICATOR);
  if (themeUsesModernLayout(s_theme)) {
    lv_obj_set_style_arc_rounded(r.climDial, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(r.climDial, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(r.climDial, lv_color_hex(modernColour(0x304B65)), LV_PART_MAIN);
  }
  lv_obj_set_style_pad_all(r.climDial, std::max(3, stroke / 2), LV_PART_KNOB);
  lv_obj_set_style_border_width(r.climDial, std::max(2, stroke / 4), LV_PART_KNOB);
  showObj(r.climDial, true);

  const auto* valueFont = d >= 220 ? &lv_font_montserrat_40 : d >= 150 ? &deck_font_28 : &deck_font_20;
  const auto* infoFont = d >= 220 ? &deck_font_20 : d >= 150 ? &deck_font_14 : &deck_font_12;
  const int labelW = d * 3 / 4;
  const int labelX = (cw - labelW) / 2;
  lv_obj_set_style_text_font(r.climValue, valueFont, 0);
  lv_obj_set_pos(r.climValue, labelX, y + d * 27 / 100);
  lv_obj_set_size(r.climValue, labelW, lineH(valueFont));
  lv_obj_set_style_text_font(r.climInfo, infoFont, 0);
  lv_obj_set_pos(r.climInfo, labelX, y + d / 2);
  lv_obj_set_size(r.climInfo, labelW, lineH(infoFont));
  showObj(r.climValue, true);
  showObj(r.climInfo, true);

  const int button = std::clamp(d / 7, 26, 36);
  const int buttonY = y + d * 70 / 100;
  for (auto* b : {r.climMinus, r.climPlus}) {
    showObj(b, true);
    lv_obj_set_size(b, button, button);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &deck_font_14, 0);
  }
  lv_obj_set_pos(r.climMinus, cw / 2 - button - gap / 2, buttonY);
  lv_obj_set_pos(r.climPlus, cw / 2 + gap / 2, buttonY);

  if (modeH) {
    const int spacing = 8;
    const int count = r.climModeCount;
    const int bw = std::min(d >= 220 ? 80 : 64, (cw - spacing * (count - 1)) / count);
    const int rowW = bw * count + spacing * (count - 1);
    lv_obj_set_pos(r.climModeRow, (cw - rowW) / 2, ch - modeH);
    lv_obj_set_size(r.climModeRow, rowW, modeH);
    for (int i = 0; i < count; ++i) {
      auto* b = r.climModeBtns[i];
      lv_obj_set_pos(b, i * (bw + spacing), 0);
      lv_obj_set_size(b, bw, modeH);
      auto* label = lv_obj_get_child(b, 0);
      lv_obj_set_style_text_font(label, bw < 58 ? &deck_font_12 : &deck_font_14, 0);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_set_width(label, std::max(1, bw - 4));
    }
  }
  showObj(r.climModeRow, modeH != 0);
  return true;
}

void fitInlineClimate(TileRt& r, int cw, int ch) {
  if (cw <= 0 || ch <= 0) return;
  if (r.climDial) {
    invalidateSwipeZones();
    if (fitInlineDial(r, cw, ch)) return;
    showObj(r.climDial, false);
    showObj(r.climModeRow, true);
    for (auto* b : {r.climMinus, r.climPlus})
      lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &deck_font_20, 0);
    for (int i = 0; i < r.climModeCount; ++i)
      lv_obj_set_width(lv_obj_get_child(r.climModeBtns[i], 0), LV_SIZE_CONTENT);
  }
  // Grouped climate tiles need a condensed layout when their allocated
  // row is too short for the standalone thermostat's three content bands.
  // Keep the HVAC mode buttons usable; standalone tiles are unaffected.
  if (r.def->parent.length() && (ch < 145 || cw < 150)) {
    // Only grouped tiles use compact rendering. A narrow group cannot safely
    // fit a thermostat label, value and three side-by-side HVAC buttons.
    const bool narrow = cw < 150;
    const int nameHeight = lv_font_get_line_height(&deck_font_14);
    lv_obj_set_style_text_font(r.name, &deck_font_14, 0);
    lv_obj_set_pos(r.name, 0, 0);
    lv_obj_set_size(r.name, cw, nameHeight);
    showObj(r.climMinus, false);
    showObj(r.climPlus, false);
    showObj(r.climInfo, false);
    // Narrow containers prioritise usable HVAC choices over the setpoint.
    // Wide but short groups can show the setpoint above a single button row.
    showObj(r.climValue, !narrow && ch >= 66);
    lv_obj_set_style_text_font(r.climValue, &deck_font_20, 0);
    const int modesTop = narrow ? nameHeight + 2 : std::max(nameHeight, ch - 30);
    lv_obj_set_pos(r.climValue, 0, nameHeight + 1);
    lv_obj_set_size(r.climValue, cw, std::max(1, modesTop - nameHeight - 2));
    lv_obj_set_pos(r.climModeRow, 0, modesTop);
    const int available = std::max(1, ch - modesTop);
    lv_obj_set_size(r.climModeRow, cw, available);
    if (r.climModeCount) {
      const bool stack = narrow;
      const int spacing = stack ? 2 : 4;
      const int modeW = stack ? cw : std::max(1, (cw - spacing * (r.climModeCount - 1)) / r.climModeCount);
      const int modeH = stack ? std::max(1, (available - spacing * (r.climModeCount - 1)) / r.climModeCount) : available;
      for (int i = 0; i < r.climModeCount; ++i) {
        lv_obj_t* button = r.climModeBtns[i];
        if (!button) continue;
        lv_obj_set_pos(button, stack ? 0 : i * (modeW + spacing),
                       stack ? i * (modeH + spacing) : 0);
        lv_obj_set_size(button, modeW, modeH);
        lv_obj_t* label = lv_obj_get_child(button, 0);
        if (label) lv_obj_set_style_text_font(label, &deck_font_12, 0);
      }
    }
    return;
  }
  showObj(r.climMinus, true);
  showObj(r.climPlus, true);
  showObj(r.climInfo, true);
  showObj(r.climValue, true);
  const bool large = roomyWidget(cw, ch) && ch >= 195;
  const lv_font_t* infoFont = large ? &deck_font_20 : &deck_font_14;
  const int infoHeight = lv_font_get_line_height(infoFont);
  const int button = std::min(large ? 76 : 52, std::max(30, cw / 5));
  const int modeHeight = std::min(large ? 56 : 40, std::max(28, ch / 5));
  const int modeY = std::max(0, ch - modeHeight);
  const int controlY = std::max(25, (modeY - button - 20) / 2);
  const int infoY = std::min(modeY - infoHeight - 3, controlY + button + 10);
  const lv_font_t* nameFont = large ? &deck_font_28 : cw < 200 ? &deck_font_14 : &deck_font_20;
  lv_obj_set_style_text_font(r.name, nameFont, 0);
  lv_obj_set_pos(r.name, 0, 0);
  lv_obj_set_size(r.name, cw, lv_font_get_line_height(nameFont));
  for (lv_obj_t* b : {r.climMinus, r.climPlus}) lv_obj_set_size(b, button, button);
  lv_obj_set_pos(r.climMinus, 0, controlY);
  lv_obj_set_pos(r.climPlus, cw - button, controlY);
  const int valX = button + 3, valW = std::max(1, cw - 2 * (button + 3));
  const lv_font_t* valueFont = large ? &lv_font_montserrat_40 : cw < 210 ? &deck_font_20 : &deck_font_28;
  const int valueHeight = lv_font_get_line_height(valueFont);
  lv_obj_set_style_text_font(r.climValue, valueFont, 0);
  lv_obj_set_pos(r.climValue, valX, controlY + (button - valueHeight) / 2);
  lv_obj_set_size(r.climValue, valW, large ? valueHeight : 39);
  lv_obj_set_pos(r.climInfo, 0, infoY);
  lv_obj_set_style_text_font(r.climInfo, infoFont, 0);
  lv_obj_set_size(r.climInfo, cw, large ? infoHeight : 18);
  lv_obj_set_pos(r.climModeRow, 0, modeY);
  lv_obj_set_size(r.climModeRow, cw, modeHeight);
  if (r.climModeCount) {
    const int spacing = 5;
    const int bw = std::max(20, (cw - spacing * (r.climModeCount - 1)) / r.climModeCount);
    for (int i = 0; i < r.climModeCount; ++i) {
      lv_obj_t* b = r.climModeBtns[i];
      if (!b) continue;
      lv_obj_set_pos(b, i * (bw + spacing), 0);
      lv_obj_set_size(b, bw, modeHeight);
      lv_obj_t* label = lv_obj_get_child(b, 0);
      if (label) lv_obj_set_style_text_font(label, large && bw >= 90 ? &deck_font_20 : bw < 66 ? &deck_font_12 : &deck_font_14, 0);
    }
  }
}

void applyInlineClimate(TileRt& r, const HaEntity* e) {
  if (!r.climValue) return;
  updateModes(r, e);
  const bool available = climateAvailable(e);
  if (available && !r.def->label.length() && e->name.length())
    lv_label_set_text(r.name, e->name.c_str());

  if (available && r.climPending && r.climSent) {
    const float tol = stepSize(*e) / 2 + 0.001f;
    const bool match = isfinite(r.climWantHigh)
      ? fabsf(e->targetLow - r.climWant) < tol &&
        fabsf(e->targetHigh - r.climWantHigh) < tol
      : fabsf(e->target - r.climWant) < tol;
    if (match) r.climPending = false;
  }
  if (available && r.climModePending.length() && e->state == r.climModePending)
    r.climModePending = "";

  String value = "Unavailable";
  if (available) {
    if (r.climPending && isfinite(r.climWant)) {
      if (isfinite(r.climWantHigh)) {
        char low[18];
        formatReading(r.climWant, low, sizeof(low));
        value = String(low) + "-" + formatTemp(r.climWantHigh);
      } else value = formatTemp(r.climWant);
    } else if (hasSetpoint(e)) {
      value = climateTarget(*e);
    } else value = climateWord(e->state);
  }
  lv_label_set_text(r.climValue, value.c_str());
  String info;
  if (available && isfinite(e->current)) info = "Now " + formatTemp(e->current);
  if (available && e->hvacAction.length()) {
    if (info.length()) info += "  \xE2\x80\xA2  ";
    info += climateWord(e->hvacAction);
  }
  lv_label_set_text(r.climInfo, info.c_str());
  for (lv_obj_t* b : {r.climMinus, r.climPlus}) {
    if (hasSetpoint(e) && haLink() == HA_CONNECTED) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else lv_obj_add_state(b, LV_STATE_DISABLED);
  }
  if (themeUsesModernLayout(s_theme)) {
    const uint32_t tint = available && e->hvacAction == "heating" ? 0xFFB56B :
                          available && e->hvacAction == "cooling" ? 0x75BFFF : modernColour(0xA5BCD5);
    lv_obj_set_style_text_color(r.climInfo, lv_color_hex(tint), 0);
    if (r.climDial) {
      lv_obj_set_style_arc_color(r.climDial, lv_color_hex(tint), LV_PART_INDICATOR);
      lv_obj_set_style_border_color(r.climDial, lv_color_hex(tint), LV_PART_KNOB);
    }
  }
  if (r.climDial) {
    const bool canAdjust = hasSetpoint(e) && haLink() == HA_CONNECTED && dialMax(*e) > dialMin(*e);
    if (canAdjust) lv_obj_remove_state(r.climDial, LV_STATE_DISABLED);
    else lv_obj_add_state(r.climDial, LV_STATE_DISABLED);
    if (!r.climDialDragging) {
      const int pos = canAdjust ? constrain((int)lroundf((inlineTarget(r, *e) - dialMin(*e)) * 100 /
                                                        (dialMax(*e) - dialMin(*e))), 0, 100) : 0;
      r.climDialSync = true;
      lv_arc_set_value(r.climDial, pos);
      r.climDialSync = false;
    }
  }
  const String mode = r.climModePending.length() ? r.climModePending : available ? e->state : String();
  for (size_t i = 0; i < r.climModeCount; ++i) {
    lv_obj_t* b = r.climModeBtns[i];
    if (!b) continue;
    if (available && haLink() == HA_CONNECTED) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else lv_obj_add_state(b, LV_STATE_DISABLED);
    if (r.climModeIds[i] == mode) lv_obj_add_state(b, LV_STATE_CHECKED);
    else lv_obj_remove_state(b, LV_STATE_CHECKED);
  }
  lv_obj_set_style_opa(r.obj, available ? LV_OPA_COVER : LV_OPA_50, 0);
}

void inlineClimateTick() {
  const uint32_t now = millis();
  for (TileRt& r : s_tiles) {
    if (!r.def || r.def->type != TILE_CLIMATE) continue;
    bool refresh = false;
    if (r.climPending) {
      if (!r.climSent && !r.climDialDragging && now - r.climAt >= INLINE_CLIMATE_SEND_MS) {
        if (sendSetpoint(r)) {
          r.climSent = true;
          r.climAt = now;
        } else {
          r.climPending = false;
          refresh = true;
          deckToastShow("Home Assistant is busy, try again", 1800);
        }
      } else if (r.climSent && now - r.climAt >= INLINE_CLIMATE_SETTLE_MS) {
        r.climPending = false;
        refresh = true;
      }
    }
    if (r.climModePending.length() && now - r.climModeAt >= INLINE_CLIMATE_SETTLE_MS) {
      r.climModePending = "";
      refresh = true;
    }
    if (refresh) {
      HaEntity e;
      applyInlineClimate(r, haGetEntity(r.def->entity, e) ? &e : nullptr);
      markPageStale(r.page);
    }
  }
}

void inlineClimateFlush() {
  for (TileRt& r : s_tiles)
    if (r.def && r.def->type == TILE_CLIMATE && r.climPending && !r.climSent)
      sendSetpoint(r);
}

}  // namespace deckui
