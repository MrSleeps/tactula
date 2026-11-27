// Deck UI: toast, detail card, confirm card and the PC pairing card.
#include "deck_ui_int.h"
#include "deck_daily.h"
#include <src/misc/cache/instance/lv_image_cache.h>

namespace deckui {

uint32_t lightColourRgb(const HaEntity& e);

void cinematicSurface(lv_obj_t* obj, int palette);

static void cinematicPopupSurface(lv_obj_t* card) {
  cinematicSurface(card, 1);
}

static void studioPanel(lv_obj_t* parent, int x, int y, int w, int h) {
  auto* panel = lv_obj_create(parent);
  lv_obj_remove_style_all(panel);
  lv_obj_set_pos(panel, x, y);
  lv_obj_set_size(panel, w, h);
  lv_obj_remove_flag(panel, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  cinematicInsetStyle(panel);
}

static lv_obj_t* s_studioBrightness = nullptr;
static lv_obj_t* s_studioColour = nullptr;
static lv_obj_t* s_studioKelvin = nullptr;
static lv_obj_t* s_studioPower = nullptr;
static lv_obj_t* s_studioPowerKnob = nullptr;

static void modalLightIcon(lv_obj_t* icon, const TileDef& d, const HaEntity* e) {
  if (!themeUsesModernLayout(s_theme) || entityDomain(d.entity) != "light") return;
  const bool lit = e && e->known && e->state == "on";
  const uint32_t rgb = lit ? lightColourRgb(*e) : 0xFFFFFFFFu;
  lv_obj_set_style_text_color(icon, lv_color_hex(lit ? (rgb == 0xFFFFFFFFu ? 0xFFBF60u : rgb) : 0x8290A3u), 0);
}

// --- Toast and detail card ------------------------------------------------------------
lv_obj_t* s_toast = nullptr;
lv_obj_t* s_modalScrim = nullptr;
lv_obj_t* s_modalCard = nullptr;
lv_draw_buf_t* s_modalGlass = nullptr;  // Glass theme: the card's frosted background
int s_modalTile = -1;
lv_obj_t* s_modalState = nullptr;
lv_obj_t* s_modalBtnLbl = nullptr;
lv_obj_t* s_modalSlider = nullptr;
static lv_obj_t* s_modalHue = nullptr;
static int32_t s_modalHueRgb = -1;
static lv_obj_t* s_modalWarmth = nullptr;
static int s_modalWarmthKelvin = -1;
static constexpr uint32_t LIGHT_PRESETS[] = {0xFFF4DA, 0xFFBE73, 0xF46D7B, 0xB78AF4, 0x69A7FF, 0x78DBC7, 0xEDF4FF};
static constexpr int LIGHT_PRESET_COUNT = sizeof(LIGHT_PRESETS) / sizeof(LIGHT_PRESETS[0]);
static lv_obj_t* s_modalSwatches[LIGHT_PRESET_COUNT] = {};
lv_obj_t* s_modalIcon = nullptr;
uint32_t s_modalIconCp = 0;
lv_obj_t* s_modalChart = nullptr;
lv_obj_t* s_modalRange = nullptr;
lv_obj_t* s_modalWeatherCondition = nullptr;
static lv_obj_t* s_weatherFeelsLabel = nullptr;
static int s_weatherArtworkSize = 0;
constexpr int WEATHER_CARD_COUNT = 10;
lv_obj_t* s_weatherCardBoxes[WEATHER_CARD_COUNT] = {};
lv_obj_t* s_weatherCardNames[WEATHER_CARD_COUNT] = {};
lv_obj_t* s_weatherCardValues[WEATHER_CARD_COUNT] = {};
lv_chart_series_t* s_modalSeries = nullptr;
int32_t* s_modalPoints = nullptr;
uint32_t s_modalHistoryVersion = 0;
uint8_t s_modalHours = 24;
float s_modalMin = NAN, s_modalMax = NAN;
bool s_modalBuilding = false;  // no history request before the open animation runs
// Confirm dialog (shares the card slot): which action waits, the tile state it
// was asked for, and the auto-cancel timer.
int8_t s_confirmHold = -1;  // -1 = the card is a detail card
bool s_confirmOn = false;
uint32_t s_confirmMs = 0;
lv_timer_t* s_confirmTimer = nullptr;
constexpr uint32_t CONFIRM_TIMEOUT_MS = 8000;
constexpr uint32_t CONFIRM_ARM_MS = 400;  // a stray second tap must not confirm

bool modalShown() { return s_modalCard != nullptr; }

// The page under a card, dimmed like the scrim, as an opaque picture on top of
// the screen: LVGL skips the page under it while the card is open.
lv_obj_t* s_modalUnder = nullptr;
lv_draw_buf_t* s_underBuf = nullptr;  // kept once used

void cardUnderFree() {
  if (s_modalCard || s_modalUnder || compMode() != COMP_NONE || !s_underBuf) return;
  lv_image_cache_drop(s_underBuf);
  lv_draw_buf_destroy(s_underBuf);
  s_underBuf = nullptr;
}

bool mediaCardOpen() {
  return s_modalCard && s_confirmHold < 0 && s_modalTile >= 0 &&
      size_t(s_modalTile) < s_tiles.size() && s_tiles[s_modalTile].def &&
      s_tiles[s_modalTile].def->type == TILE_HA_MEDIA;
}

bool coverPage() {
  const int w = logicalW(), h = logicalH();
  if (s_underBuf && (s_underBuf->header.w != w || s_underBuf->header.h != h)) {
    lv_draw_buf_destroy(s_underBuf);
    s_underBuf = nullptr;
  }
  if (!s_underBuf) s_underBuf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, 0);
  if (!s_underBuf) return false;
  lv_image_cache_drop(s_underBuf);
  const uint16_t* fb = lastFrame();
  for (int y = 0; y < h; y++) {
    uint16_t* d = (uint16_t*)(s_underBuf->data + y * s_underBuf->header.stride);
    const uint16_t* s = fb + y * w;
    for (int x = 0; x < w; x++) d[x] = (s[x] >> 1) & 0x7BEF;  // half, as the compositor dims
  }
  s_modalUnder = lv_image_create(lv_screen_active());
  lv_obj_add_flag(s_modalUnder, (lv_obj_flag_t)(LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING));
  lv_obj_remove_flag(s_modalUnder, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_pos(s_modalUnder, 0, 0);
  lv_image_set_src(s_modalUnder, s_underBuf);
  // An image alone never reports "covers": the opaque background does.
  lv_obj_set_style_bg_color(s_modalUnder, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_modalUnder, LV_OPA_COVER, 0);
  return true;
}

void onModalScrim(lv_event_t*);

// The scrim for a card: transparent over the covered page, else half black.
lv_obj_t* makeScrim(bool freezePage = true) {
  // A cinematic sheet deliberately extends below the viewport to hide its
  // lower rounded corners. That overflow must never scroll the top layer.
  lv_obj_t* layer = lv_layer_top();
  lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_scroll_to(layer, 0, 0, LV_ANIM_OFF);
  lv_obj_t* s = lv_obj_create(layer);
  lv_obj_add_flag(s, LV_OBJ_FLAG_FLOATING);
  lv_obj_remove_style_all(s);
  lv_obj_set_size(s, logicalW(), logicalH());
  lv_obj_set_style_bg_color(s, lv_color_black(), 0);
  const bool covered = freezePage && coverPage();
  lv_obj_set_style_bg_opa(s, covered ? LV_OPA_TRANSP : freezePage ? LV_OPA_50 : LV_OPA_40, 0);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s, onModalScrim, LV_EVENT_CLICKED, nullptr);
  return s;
}

// Thin line on the top edge while daily pictures download: swipes may hitch then.
lv_obj_t* s_dailyBar = nullptr;

void dailyBarSync() {
  int done = 0, total = 0;
  if (!deck::dailyProgress(done, total) || !total) {
    if (s_dailyBar) lv_obj_add_flag(s_dailyBar, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (!s_dailyBar) {
    s_dailyBar = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dailyBar);
    lv_obj_set_style_bg_color(s_dailyBar, lv_color_hex(C_OK), 0);
    lv_obj_set_style_bg_opa(s_dailyBar, LV_OPA_80, 0);
    lv_obj_remove_flag(s_dailyBar, LV_OBJ_FLAG_CLICKABLE);
  }
  // Half a step for the picture in progress, so the line moves from the start.
  lv_obj_set_size(s_dailyBar, std::max(6, logicalW() * (2 * done + 1) / (2 * total)), 3);
  lv_obj_set_pos(s_dailyBar, 0, 0);
  lv_obj_remove_flag(s_dailyBar, LV_OBJ_FLAG_HIDDEN);
}

void setModalOpen(bool open) {
  portENTER_CRITICAL(&s_mux);
  sh.modalOpen = open;
  portEXIT_CRITICAL(&s_mux);
}

void toastOpa(void* obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0); }
void toastDone(lv_anim_t* a) {
  if (lv_obj_get_style_opa((lv_obj_t*)a->var, LV_PART_MAIN) == LV_OPA_TRANSP)
    lv_obj_add_flag((lv_obj_t*)a->var, LV_OBJ_FLAG_HIDDEN);
}

void deckToastShow(const char* text, uint32_t holdMs) {
  if (!s_toast) {
    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_toast);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x171E2C), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(s_toast, 16, 0);
    lv_obj_set_style_pad_ver(s_toast, 9, 0);
    lv_obj_set_style_text_color(s_toast, lv_color_hex(0xEDF0F8), 0);  // dark pill on both themes
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_label_create(s_toast);
  }
  lv_obj_t* label = lv_obj_get_child(s_toast, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_max_width(label, logicalW() - 40 - 32, 0);
  lv_label_set_text(label, text);
  // Above the page dots, which the flush task draws at the very bottom.
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_toast);  // above cards and the editor made after it
  lv_anim_delete(s_toast, toastOpa);
  lv_anim_t in;
  lv_anim_init(&in);
  lv_anim_set_var(&in, s_toast);
  lv_anim_set_exec_cb(&in, toastOpa);
  lv_anim_set_values(&in, lv_obj_get_style_opa(s_toast, LV_PART_MAIN), LV_OPA_COVER);
  lv_anim_set_duration(&in, 120);
  lv_anim_start(&in);
  lv_anim_t out;
  lv_anim_init(&out);
  lv_anim_set_var(&out, s_toast);
  lv_anim_set_exec_cb(&out, toastOpa);
  lv_anim_set_values(&out, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_delay(&out, 120 + holdMs);
  lv_anim_set_duration(&out, 250);
  lv_anim_set_completed_cb(&out, toastDone);
  lv_anim_start(&out);
}

// --- Climate controls on the detail card ---------------------------------------------
// +/- move the setpoint the card shows at once; the call goes out once the
// taps stop, so five taps are one set_temperature. The asked value stays on
// the card until Home Assistant reports it or CLIMATE_SETTLE_MS pass.
bool s_climate = false;
static lv_obj_t* s_climCurrent = nullptr;
static lv_obj_t* s_climAction = nullptr;
lv_obj_t* s_climMinus = nullptr;
lv_obj_t* s_climPlus = nullptr;
lv_obj_t* s_climInfo = nullptr;
lv_obj_t* s_climGauge = nullptr;
bool s_climGaugeSync = false;     // Ignore our own refresh-driven arc updates.
bool s_climGaugeDragging = false; // Do not send HA commands mid-drag.
std::vector<lv_obj_t*> s_climModeBtns;
std::vector<String> s_climModes;
float s_climWant = NAN, s_climWantHigh = NAN;  // high: heat_cool range only
bool s_climSent = false;
lv_timer_t* s_climTimer = nullptr;
String s_climModeWant;
uint32_t s_climModeMs = 0;
constexpr uint32_t CLIMATE_SEND_MS = 700;
constexpr uint32_t CLIMATE_SETTLE_MS = 5000;

void modalCallFailed() {
  deckToastShow(haLink() == HA_CONNECTED ? "Home Assistant is busy, try again" :
                                        "Home Assistant is offline", 1800);
}

bool modalTileOk() { return s_modalTile >= 0 && s_modalTile < (int)s_tiles.size(); }
const String& modalEntity() { return s_tiles[s_modalTile].def->entity; }

float climateStep(const HaEntity& e) { return e.tempStep > 0 ? e.tempStep : 0.5f; }

float climateClamp(const HaEntity& e, float v) {
  const float step = climateStep(e);
  v = roundf(v / step) * step;
  if (isfinite(e.tempMin) && v < e.tempMin) v = e.tempMin;
  if (isfinite(e.tempMax) && v > e.tempMax) v = e.tempMax;
  return v;
}

bool climateHasSetpoint(const HaEntity& e) {
  return e.known && e.state != "off" && e.state != "unavailable" && e.state != "unknown" &&
         (isfinite(e.target) || climateRange(e));
}

void climateWantClear() {
  s_climWant = s_climWantHigh = NAN;
  s_climSent = false;
  if (s_climTimer) lv_timer_delete(s_climTimer);
  s_climTimer = nullptr;
}

bool climateSend(const String& entity) {
  char data[80];
  char a[16], b[16];
  snprintf(a, sizeof a, "%.4g", s_climWant);
  if (isfinite(s_climWantHigh)) {
    snprintf(b, sizeof b, "%.4g", s_climWantHigh);
    snprintf(data, sizeof data, "{\"target_temp_low\":%s,\"target_temp_high\":%s}", a, b);
  } else {
    snprintf(data, sizeof data, "{\"temperature\":%s}", a);
  }
  if (haCallService("climate", "set_temperature", entity, data)) return true;
  modalCallFailed();
  return false;
}

void onClimateTimer(lv_timer_t*) {
  s_climTimer = nullptr;  // one-shot: it deletes itself
  if (!modalTileOk() || !s_climate || !isfinite(s_climWant)) return;
  if (s_climSent) {  // Home Assistant never reported it: show what it has
    climateWantClear();
    refreshModal();
    return;
  }
  if (!climateSend(modalEntity())) {
    climateWantClear();
    refreshModal();
    return;
  }
  s_climSent = true;
  s_climTimer = lv_timer_create(onClimateTimer, CLIMATE_SETTLE_MS, nullptr);
  lv_timer_set_repeat_count(s_climTimer, 1);
}

void climateArm(uint32_t ms) {
  if (s_climTimer) lv_timer_delete(s_climTimer);
  s_climTimer = lv_timer_create(onClimateTimer, ms, nullptr);
  lv_timer_set_repeat_count(s_climTimer, 1);
}

void onClimateStep(lv_event_t* ev) {
  if (!modalTileOk() || !s_climate) return;
  const int dir = (int)(intptr_t)lv_event_get_user_data(ev);
  HaEntity e;
  if (!haGetEntity(modalEntity(), e) || !climateHasSetpoint(e)) return;
  const float step = climateStep(e) * dir;
  if (climateRange(e)) {
    float lo = isfinite(s_climWant) ? s_climWant : e.targetLow;
    float hi = isfinite(s_climWantHigh) ? s_climWantHigh : e.targetHigh;
    // The range moves as one, and stops at either limit.
    if ((isfinite(e.tempMin) && lo + step < e.tempMin) || (isfinite(e.tempMax) && hi + step > e.tempMax))
      return;
    s_climWant = climateClamp(e, lo + step);
    s_climWantHigh = climateClamp(e, hi + step);
  } else {
    const float base = isfinite(s_climWant) ? s_climWant : e.target;
    s_climWant = climateClamp(e, base + step);
    s_climWantHigh = NAN;
  }
  s_climSent = false;
  climateArm(CLIMATE_SEND_MS);
  refreshModal();
}

// LVGL tracks the pointer around the arc. Convert its position into a real
// Home Assistant setpoint, snapped to the climate entity's step and limits.
// Updates remain local while dragging; the existing debounced HA service
// path sends one command after the finger is lifted.
void onClimateGauge(lv_event_t* ev) {
  if (!modalTileOk() || !s_climate || !s_climGauge || s_climGaugeSync) return;
  const lv_event_code_t code = lv_event_get_code(ev);
  if (code == LV_EVENT_PRESSED) {
    s_climGaugeDragging = true;
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (s_climGaugeDragging && isfinite(s_climWant) && !s_climSent)
      climateArm(CLIMATE_SEND_MS);
    s_climGaugeDragging = false;
    return;
  }
  if (code != LV_EVENT_VALUE_CHANGED) return;

  HaEntity e;
  if (!haGetEntity(modalEntity(), e) || !climateHasSetpoint(e)) return;
  const float lo = isfinite(e.tempMin) ? e.tempMin : 5.0f;
  const float hi = isfinite(e.tempMax) ? e.tempMax : 35.0f;
  if (hi <= lo) return;
  const float desired = climateClamp(e, lo + (hi - lo) * lv_arc_get_value(s_climGauge) / 100.0f);
  if (climateRange(e)) {
    // Heat/cool entities expose two setpoints; move the range together
    // without changing its width or exceeding either entity limit.
    const float baseLow = isfinite(s_climWant) ? s_climWant : e.targetLow;
    const float baseHigh = isfinite(s_climWantHigh) ? s_climWantHigh : e.targetHigh;
    if (!isfinite(baseLow) || !isfinite(baseHigh)) return;
    const float delta = desired - baseLow;
    if ((isfinite(e.tempMin) && baseLow + delta < e.tempMin) ||
        (isfinite(e.tempMax) && baseHigh + delta > e.tempMax)) return;
    s_climWant = climateClamp(e, baseLow + delta);
    s_climWantHigh = climateClamp(e, baseHigh + delta);
  } else {
    s_climWant = desired;
    s_climWantHigh = NAN;
  }
  s_climSent = false;
  // A tap on the arc also counts; if no drag is in progress debounce normally.
  if (!s_climGaugeDragging) climateArm(CLIMATE_SEND_MS);
  refreshModal();
}

void onClimateMode(lv_event_t* ev) {
  if (!modalTileOk() || !s_climate) return;
  const size_t i = (size_t)(intptr_t)lv_event_get_user_data(ev);
  if (i >= s_climModes.size()) return;
  const String& mode = s_climModes[i];
  if (!haCallService("climate", "set_hvac_mode", modalEntity(), "{\"hvac_mode\":\"" + mode + "\"}")) {
    modalCallFailed();
    return;
  }
  climateWantClear();  // the setpoint may change with the mode
  s_climModeWant = mode;
  s_climModeMs = millis();
  refreshModal();
}

// A setpoint still waiting for its call goes out when the card closes.
void climateClose() {
  if (s_climate && modalTileOk() && isfinite(s_climWant) && !s_climSent)
    climateSend(modalEntity());
  climateWantClear();
  s_climate = false;
  s_climCurrent = s_climAction = nullptr;
  s_climMinus = s_climPlus = s_climInfo = s_climGauge = nullptr;
  s_climGaugeSync = false;
  s_climGaugeDragging = false;
  s_climModeBtns.clear();
  s_climModes.clear();
  s_climModeWant = "";
}

void climateRefresh(const HaEntity* e) {
  const bool known = e != nullptr;
  // The asked setpoint has arrived: drop it, Home Assistant's own is the same.
  if (known && s_climSent && isfinite(s_climWant)) {
    const float tol = climateStep(*e) / 2;
    const bool same = isfinite(s_climWantHigh)
        ? fabsf(e->targetLow - s_climWant) < tol && fabsf(e->targetHigh - s_climWantHigh) < tol
        : fabsf(e->target - s_climWant) < tol;
    if (same) climateWantClear();
  }
  if (s_climModeWant.length() &&
      ((known && e->state == s_climModeWant) || millis() - s_climModeMs >= CLIMATE_SETTLE_MS))
    s_climModeWant = "";
  const bool setpoint = known && climateHasSetpoint(*e);
  String big;
  if (!known || !e->known || e->state == "unavailable" || e->state == "unknown") big = "Unavailable";
  else if (isfinite(s_climWant)) {
    if (isfinite(s_climWantHigh)) {
      char lo[16];
      formatReading(s_climWant, lo, sizeof lo);
      big = String(lo) + "-" + formatTemp(s_climWantHigh);
    } else big = formatTemp(s_climWant);
  } else if (setpoint) big = climateTarget(*e);
  else big = climateWord(e->state);
  lv_label_set_text(s_modalState, big.c_str());
  for (lv_obj_t* b : {s_climMinus, s_climPlus}) {
    if (setpoint) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else lv_obj_add_state(b, LV_STATE_DISABLED);
  }
  // "Now 20.5°  ·  Heating"
  String info;
  if (known && isfinite(e->current)) info = "Now " + formatTemp(e->current);
  if (known && e->hvacAction.length()) {
    if (info.length()) info += "  \xE2\x80\xA2  ";
    info += climateWord(e->hvacAction);
  }
  lv_label_set_text(s_climInfo, info.c_str());
  if (s_climCurrent) {
    lv_label_set_text(s_climCurrent, known && isfinite(e->current) ? climateCurrentText(*e, true).c_str() : "--");
    const String action = known && e->hvacAction.length() ? climateWord(e->hvacAction) : known ? climateWord(e->state) : String("Unavailable");
    lv_label_set_text(s_climAction, action.c_str());
  }
  if (s_climGauge) {
    if (themeUsesModernLayout(s_theme)) {
      const uint32_t tint = known && e->hvacAction == "heating" ? 0xFFB56B :
                            known && e->hvacAction == "cooling" ? 0x75BFFF : 0x709DCB;
      if (s_climAction) lv_obj_set_style_text_color(s_climAction, lv_color_hex(tint), 0);
      lv_obj_set_style_arc_color(s_climGauge, lv_color_hex(tint), LV_PART_INDICATOR);
      lv_obj_set_style_border_color(s_climGauge, lv_color_hex(tint), LV_PART_KNOB);
    }
    const float target = isfinite(s_climWant) ? s_climWant :
                         known ? (climateRange(*e) ? e->targetLow : e->target) : NAN;
    const float lo = known && isfinite(e->tempMin) ? e->tempMin : 5.0f;
    const float hi = known && isfinite(e->tempMax) ? e->tempMax : 35.0f;
    const int pct = setpoint && isfinite(target) && hi > lo
        ? constrain((int)lroundf((target - lo) * 100.0f / (hi - lo)), 0, 100) : 0;
    if (setpoint) lv_obj_remove_state(s_climGauge, LV_STATE_DISABLED);
    else lv_obj_add_state(s_climGauge, LV_STATE_DISABLED);
    if (!s_climGaugeDragging) {
      s_climGaugeSync = true;
      lv_arc_set_value(s_climGauge, pct);
      s_climGaugeSync = false;
    }
  }
  const String& mode = s_climModeWant.length() ? s_climModeWant : known ? e->state : String();
  for (size_t i = 0; i < s_climModeBtns.size(); i++) {
    if (s_climModes[i] == mode) lv_obj_add_state(s_climModeBtns[i], LV_STATE_CHECKED);
    else lv_obj_remove_state(s_climModeBtns[i], LV_STATE_CHECKED);
  }
}

// All themes share the draggable thermostat dial and Home Assistant controls.
void climateBuild(lv_obj_t* c, int cw, int ch, uint32_t accent, const HaEntity* e, bool large) {
  s_climate = true;
  const bool portrait = cw < ch;
  const auto px = [large](int n) { return large ? n * 2 : n; };
  const bool cinematic = themeUsesModernLayout(s_theme);
  const bool compact = cinematic && !large;
  const bool wide = cinematic && (cw >= 700 || (compact && cw >= 400)) && cw > ch;
  const bool hasModes = e && e->hvacModes.indexOf(',') >= 0;
  const int modesH = hasModes ? px(52) : 0;
  const int header = cinematic ? (large ? 105 : compact ? 48 : 68) : 0;
  const int panelInset = compact ? 8 : px(12);
  const int panelGap = compact ? 8 : px(12);
  const int panelW = compact && wide ? (cw - 2 * panelInset - panelGap) * 60 / 100 : (cw - 2 * panelInset - panelGap) / 2;
  const int panelH = ch - header - modesH - px(8);
  const int D = std::max(60, std::min({wide ? panelW - px(24) : cw - px(32),
      wide ? panelH - px(16) : ch - header - modesH - px(14), px(portrait ? 236 : 220)}));
  const int yDial = wide ? header + (panelH - D) / 2 :
      cinematic ? header + std::max(0, (ch - header - D - modesH) / 2) :
      std::max(px(12), (ch - D - modesH) / 2 - px(portrait ? 4 : 2));
  const lv_font_t* valueFont = large ? &lv_font_montserrat_40 : &deck_font_28;
  const lv_font_t* infoFont = large ? &deck_font_20 : &deck_font_12;
  const int left = wide ? panelInset + (panelW - D) / 2 : (cw - D) / 2;
  if (wide) {
    const int rh = panelH;
    studioPanel(c, panelInset, header, panelW, rh);
    lv_obj_move_to_index(lv_obj_get_child(c, lv_obj_get_child_count(c) - 1), 0);
    const int rx = panelInset + panelW + panelGap, rw = cw - rx - panelInset;
    const int metricsH = compact ? std::min(rh, 148) : rh;
    const int metricsY = header + (rh - metricsH) / 2;
    studioPanel(c, rx, metricsY, rw, metricsH);
    auto metric = [&](const char* caption, int y, const lv_font_t* font) {
      auto* title = lv_label_create(c);
      lv_label_set_text(title, caption);
      lv_obj_set_style_text_font(title, infoFont, 0);
      lv_obj_set_style_text_color(title, lv_color_hex(modernColour(0xA5BCD5)), 0);
      lv_obj_set_width(title, rw - px(28));
      lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(title, rx + px(14), y);
      auto* val = lv_label_create(c);
      lv_label_set_text(val, "--");
      lv_obj_set_style_text_font(val, font, 0);
      lv_obj_set_width(val, rw - px(28));
      lv_label_set_long_mode(val, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(val, rx + px(14), y + lv_font_get_line_height(infoFont) + px(5));
      return val;
    };
    const lv_font_t* actionFont = large ? &deck_font_28 : &deck_font_20;
    const int currentH = lv_font_get_line_height(infoFont) + px(5) + lv_font_get_line_height(valueFont);
    const int actionH = lv_font_get_line_height(infoFont) + px(5) + lv_font_get_line_height(actionFont);
    s_climCurrent = metric(compact ? "Current temp" : "Current temperature", metricsY + metricsH / 4 - currentH / 2, valueFont);
    s_climAction = metric(compact ? "Heating status" : "Heating / cooling status", metricsY + metricsH * 3 / 4 - actionH / 2, actionFont);
  }
  s_climGauge = lv_arc_create(c);
  lv_obj_set_pos(s_climGauge, left, yDial);
  lv_obj_set_size(s_climGauge, D, D);
  lv_arc_set_range(s_climGauge, 0, 100);
  lv_arc_set_bg_angles(s_climGauge, 135, 45);
  lv_obj_set_style_arc_width(s_climGauge, px(cinematic ? 11 : 8), LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_climGauge, px(cinematic ? 11 : 8), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_climGauge, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_climGauge, true, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_climGauge, lv_color_mix(lv_color_hex(C_TEXT), lv_color_hex(C_PANEL), LV_OPA_20), LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_climGauge, lv_color_hex(cinematic ? 0x75BFFF : C_OK), LV_PART_INDICATOR);
  lv_obj_move_foreground(s_modalState);
  // LVGL draws this knob at the live end of the blue arc. Keeping it
  // visually distinct also makes the drag target discoverable.
  lv_obj_set_style_bg_color(s_climGauge, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(s_climGauge, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(s_climGauge, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_pad_all(s_climGauge, px(4), LV_PART_KNOB);
  lv_obj_set_style_border_width(s_climGauge, px(2), LV_PART_KNOB);
  lv_obj_set_style_border_color(s_climGauge, lv_color_hex(C_OK), LV_PART_KNOB);
  lv_obj_set_ext_click_area(s_climGauge, px(9));
  lv_obj_remove_flag(s_climGauge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_climGauge, onClimateGauge, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(s_climGauge, onClimateGauge, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s_climGauge, onClimateGauge, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(s_climGauge, onClimateGauge, LV_EVENT_PRESS_LOST, nullptr);

  lv_obj_set_style_text_align(s_modalState, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_modalState, valueFont, 0);
  lv_obj_set_pos(s_modalState, left + px(8), wide ?
      yDial + D / 2 - lv_font_get_line_height(valueFont) / 2 : yDial + D * 29 / 100);
  lv_obj_set_size(s_modalState, D - px(16), lv_font_get_line_height(valueFont));

  s_climInfo = lv_label_create(c);
  lv_obj_set_style_text_font(s_climInfo, infoFont, 0);
  lv_obj_set_style_text_color(s_climInfo, lv_color_hex(C_DIM), 0);
  lv_obj_set_style_text_align(s_climInfo, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_climInfo, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_pos(s_climInfo, left + px(7), yDial + D * 49 / 100);
  lv_obj_set_size(s_climInfo, D - px(14), lv_font_get_line_height(infoFont));
  if (wide) lv_obj_add_flag(s_climInfo, LV_OBJ_FLAG_HIDDEN);

  auto step = [&](int x, const char* symbol, int dir) {
    lv_obj_t* b = qpButton(c, x, yDial + D * (compact ? 75 : 70) / 100, px(40), symbol, false);
    lv_obj_set_size(b, px(40), px(40));
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), large ? &deck_font_28 : &deck_font_14, 0);
    lv_obj_set_style_radius(b, cinematic ? LV_RADIUS_CIRCLE : px(11), 0);
    if (cinematic) lv_obj_set_style_bg_color(b, lv_color_hex(modernColour(0x29496A)), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(C_TILE_LINE), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_ext_click_area(b, px(6));
    lv_obj_add_event_cb(b, onClimateStep, LV_EVENT_CLICKED, (void*)(intptr_t)dir);
    return b;
  };
  s_climMinus = step(left + D / 2 - px(47), LV_SYMBOL_MINUS, -1);
  s_climPlus = step(left + D / 2 + px(7), LV_SYMBOL_PLUS, 1);

  s_climModes.clear();
  s_climModeBtns.clear();
  if (e) {
    int from = 0;
    while (from < (int)e->hvacModes.length() && s_climModes.size() < 8) {
      int comma = e->hvacModes.indexOf(',', from);
      if (comma < 0) comma = e->hvacModes.length();
      if (comma > from) s_climModes.push_back(e->hvacModes.substring(from, comma));
      from = comma + 1;
    }
  }
  if (s_climModes.size() > 1) {
    lv_obj_t* row = lv_obj_create(c);
    lv_obj_remove_style_all(row);
    lv_obj_set_pos(row, px(16), (wide || compact) ? ch - modesH + (modesH - px(40)) / 2 :
                   yDial + D + px(10));
    lv_obj_set_size(row, cw - px(32), px(40));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, s_climModes.size() <= 3 ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, px(7), 0);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    for (size_t i = 0; i < s_climModes.size(); i++) {
      lv_obj_t* b = qpButton(row, 0, 0, 10, climateWord(s_climModes[i]).c_str(), false);
      lv_obj_set_width(b, LV_SIZE_CONTENT);
      lv_obj_set_height(b, px(40));
      lv_obj_set_style_text_font(lv_obj_get_child(b, 0), large ? &deck_font_20 : &deck_font_14, 0);
      lv_obj_set_style_pad_hor(b, px(10), 0);
      lv_obj_set_style_min_width(b, px(52), 0);
      lv_obj_set_style_radius(b, px(10), 0);
      lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_CHECKED);
      lv_obj_set_style_border_width(b, 1, 0);
      lv_obj_set_style_border_color(b, lv_color_hex(C_TILE_LINE), 0);
      lv_obj_add_event_cb(b, onClimateMode, LV_EVENT_CLICKED, (void*)(intptr_t)i);
      s_climModeBtns.push_back(b);
    }
  } else s_climModes.clear();
}

// Pack the available Home Assistant observations into metric cards.
void refreshWeatherCards(const HaEntity* e) {
  struct Entry { const char* name; float value; String unit; };
  if (!e || !e->known || e->state == "unavailable" || e->state == "unknown") {
    for (int i = 0; i < WEATHER_CARD_COUNT; ++i)
      if (s_weatherCardBoxes[i]) lv_obj_add_flag(s_weatherCardBoxes[i], LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const Entry entries[] = {
    {"Feels like", e->weatherFeels, e->weatherTempUnit},
    {"Humidity", e->weatherHumidity, "%"},
    {"Wind", e->weatherWind, e->weatherWindUnit.length() ? String(" ") + e->weatherWindUnit : String()},
    {"Rain", e->weatherRainChance, "%"},
    {"Dew point", e->weatherDew, e->weatherTempUnit},
    {"Pressure", e->weatherPressure, e->weatherPressureUnit.length() ? String(" ") + e->weatherPressureUnit : String()},
    {"UV index", e->weatherUV, ""},
    {"Gusts", e->weatherGust, e->weatherWindUnit.length() ? String(" ") + e->weatherWindUnit : String()},
    {"Visibility", e->weatherVisibility, e->weatherVisibilityUnit.length() ? String(" ") + e->weatherVisibilityUnit : String()},
    {"Direction", e->weatherBearing, "\xC2\xB0"}
  };
  int shown = 0;
  for (const Entry& entry : entries) {
    if (!isfinite(entry.value) || (s_weatherFeelsLabel && !strcmp(entry.name, "Feels like"))) continue;
    if (shown >= WEATHER_CARD_COUNT) break;
    char num[24];
    formatReading(entry.value, num, sizeof(num));
    const String value = String(num) + entry.unit;
    lv_label_set_text(s_weatherCardNames[shown], entry.name);
    lv_label_set_text(s_weatherCardValues[shown], value.c_str());
    lv_obj_remove_flag(s_weatherCardBoxes[shown], LV_OBJ_FLAG_HIDDEN);
    ++shown;
  }
  for (; shown < WEATHER_CARD_COUNT; ++shown)
    lv_obj_add_flag(s_weatherCardBoxes[shown], LV_OBJ_FLAG_HIDDEN);
}

void closeModal() {
  if (s_modalSlider && s_modalTile >= 0 && s_modalTile < (int)s_tiles.size()) {
    if (Wish* w = findWish(s_tiles[s_modalTile].def->entity)) {
      if (w->holding) w->level = {};
      w->dragging = w->holding = false;
    }
  }
  climateClose();
  resetHaMediaSheet();
  if (s_modalCard) lv_obj_delete(s_modalCard);
  if (s_modalScrim) lv_obj_delete(s_modalScrim);
  if (s_modalUnder) lv_obj_delete(s_modalUnder);
  s_modalCard = s_modalScrim = s_modalUnder = nullptr;
  if (s_modalGlass) lv_draw_buf_destroy(s_modalGlass);
  s_modalGlass = nullptr;
  s_modalSlider = nullptr;
  s_studioBrightness = s_studioColour = s_studioKelvin = s_studioPower = s_studioPowerKnob = nullptr;
  s_modalHue = nullptr;
  s_modalHueRgb = -1;
  s_modalWarmth = nullptr;
  s_modalWarmthKelvin = -1;
  for (auto& swatch : s_modalSwatches) swatch = nullptr;
  s_modalIcon = nullptr;
  s_modalChart = s_modalRange = nullptr;
  s_modalWeatherCondition = nullptr;
  s_weatherFeelsLabel = nullptr;
  s_weatherArtworkSize = 0;
  resetModalWeatherArtwork();
  for (int i = 0; i < WEATHER_CARD_COUNT; ++i) {
    s_weatherCardBoxes[i] = nullptr;
    s_weatherCardNames[i] = nullptr;
    s_weatherCardValues[i] = nullptr;
  }
  s_modalSeries = nullptr;
  heap_caps_free(s_modalPoints);
  s_modalPoints = nullptr;
  s_modalHistoryVersion = 0;
  s_modalMin = s_modalMax = NAN;
  haDetailHistory("");
  s_modalTile = -1;
  s_confirmHold = -1;
  if (s_confirmTimer) lv_timer_delete(s_confirmTimer);
  s_confirmTimer = nullptr;
  setModalOpen(false);
}

// Calm: the detail card shrinks back into its tile; confirm cards just close.
void onModalScrim(lv_event_t*) {
  if (s_confirmHold < 0 && !themeUsesModernLayout(s_theme)) fxCardClose(s_modalCard, s_modalTile);
  else closeModal();
}

void onModalPrimary(lv_event_t*) {
  if (s_modalTile < 0 || s_modalTile >= (int)s_tiles.size()) return;
  TileRt& r = s_tiles[s_modalTile];
  if (needsConfirm(r, r.def->tap)) {
    openConfirm(s_modalTile, false);  // replaces this card
    return;
  }
  runAction(r, r.def->tap);
  refreshModal();
}

// The card gets a level slider for lights and for entities that report a level.
bool modalHasSlider() {
  if (s_modalTile < 0) return false;
  const String& id = s_tiles[s_modalTile].def->entity;
  const String d = entityDomain(id);
  if (d == "light") return true;
  HaEntity e;
  return sliderDomain(d) && haGetEntity(id, e) && sliderFrac(d, e) >= 0;
}

void sendLevel(int pct, bool dragging) {
  if (s_modalTile < 0) return;
  const String& entity = s_tiles[s_modalTile].def->entity;
  if (dragging) setWish(entity, true, pct / 100.0f, true);
  else releaseLevel(entity);
}

void onModalSlider(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (s_studioBrightness) lv_label_set_text_fmt(s_studioBrightness, "%d%%", (int)lv_slider_get_value(s_modalSlider));
  if (code == LV_EVENT_PRESSED && s_modalTile >= 0)
    holdLevel(s_tiles[s_modalTile].def->entity, lv_slider_get_value(s_modalSlider) / 100.0f);
  else if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    sendLevel(lv_slider_get_value(s_modalSlider), code == LV_EVENT_VALUE_CHANGED);
}

static void onStudioBrightnessPreset(lv_event_t* e) {
  if (!modalTileOk() || !s_modalSlider) return;
  const int pct = (int)(uintptr_t)lv_event_get_user_data(e);
  lv_slider_set_value(s_modalSlider, pct, LV_ANIM_OFF);
  if (s_studioBrightness) lv_label_set_text_fmt(s_studioBrightness, "%d%%", pct);
  sendLevel(pct, true);
  sendLevel(pct, false);
}

static void sendModalColour(const char* data) {
  if (!modalTileOk()) return;
  if (!haCallService("light", "turn_on", modalEntity(), data)) modalCallFailed();
}

static void onModalHue(lv_event_t* e) {
  auto* slider = lv_event_get_target_obj(e);
  const int hue = lv_slider_get_value(slider);
  char payload[32];
  snprintf(payload, sizeof(payload), "{\"hs_color\":[%d,100]}", hue);
  sendModalColour(payload);
}

static void onModalSwatch(lv_event_t* e) {
  const size_t index = (uintptr_t)lv_event_get_user_data(e);
  if (index >= LIGHT_PRESET_COUNT) return;
  const uint32_t rgb = LIGHT_PRESETS[index];
  char payload[40];
  snprintf(payload, sizeof(payload), "{\"rgb_color\":[%u,%u,%u]}",
           (unsigned)(rgb >> 16), (unsigned)((rgb >> 8) & 255), (unsigned)(rgb & 255));
  sendModalColour(payload);
}

static void onModalWarmth(lv_event_t* e) {
  const int kelvin = lv_slider_get_value(lv_event_get_target_obj(e));
  char payload[40];
  snprintf(payload, sizeof(payload), "{\"color_temp_kelvin\":%d}", kelvin);
  sendModalColour(payload);
}

static void modalColourTitle(lv_obj_t* card, const char* text, int inset, int y, bool large = false) {
  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, text);
  lv_obj_set_style_text_font(title, large ? &deck_font_20 : &deck_font_14, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(C_DIM), 0);
  lv_obj_set_pos(title, inset, y);
}

struct ModalSliderSize {
  int inset, width, height, knobPad, touchPad;
};

static lv_obj_t* makeModalSlider(lv_obj_t* card, const ModalSliderSize& size, int y) {
  lv_obj_t* slider = lv_slider_create(card);
  lv_obj_set_pos(slider, size.inset, y);
  lv_obj_set_size(slider, size.width, size.height);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, size.knobPad, LV_PART_KNOB);
  lv_obj_set_ext_click_area(slider, size.touchPad);
  return slider;
}

static void buildModalColour(lv_obj_t* card, const ModalSliderSize& track, int y, int labelGap,
                             int swatchScale, bool presets, bool both = false, bool compact = false) {
  modalColourTitle(card, "Colour", track.inset, y, both && !compact);
  const int trackY = y + labelGap;
  if (presets || both) {
    const int size = std::min(swatchScale * 2 + 4, (track.width - 4 * (LIGHT_PRESET_COUNT - 1)) / LIGHT_PRESET_COUNT);
    const int gap = both ? (track.width - size * LIGHT_PRESET_COUNT) / (LIGHT_PRESET_COUNT - 1) : std::min(12, (track.width - size * LIGHT_PRESET_COUNT) / (LIGHT_PRESET_COUNT - 1));
    for (int i = 0; i < LIGHT_PRESET_COUNT; ++i) {
      lv_obj_t* swatch = lv_obj_create(card);
      lv_obj_remove_style_all(swatch);
      lv_obj_add_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(swatch, size, size);
      lv_obj_set_pos(swatch, track.inset + i * (size + gap), trackY + (both ? track.height + (compact ? 8 : 46) : 0));
      lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(swatch, lv_color_hex(LIGHT_PRESETS[i]), 0);
      lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
      lv_obj_set_style_outline_width(swatch, 2, LV_STATE_CHECKED);
      lv_obj_set_style_outline_pad(swatch, 2, LV_STATE_CHECKED);
      lv_obj_set_style_outline_color(swatch, lv_color_hex(C_TEXT), LV_STATE_CHECKED);
      lv_obj_add_event_cb(swatch, onModalSwatch, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
      s_modalSwatches[i] = swatch;
    }
    if (!both) return;
  }
  const uint32_t vivid[] = {0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0xFF00FF, 0xFF0000};
  const uint32_t soft[] = {0xFF5656, 0xFFE55B, 0x60DC91, 0x51D7EB, 0x6475FF, 0xDF65ED, 0xFF5656};
  const uint32_t* hueStops = both ? soft : vivid;
  const int radius = track.height / 2;
  // Solid end caps under the rainbow give it the same outline as the other tracks.
  for (const int x : {track.inset, track.inset + track.width - track.height}) {
    lv_obj_t* cap = lv_obj_create(card);
    lv_obj_remove_style_all(cap);
    lv_obj_remove_flag(cap, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_pos(cap, x, trackY);
    lv_obj_set_size(cap, track.height, track.height);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap, lv_color_hex(hueStops[0]), 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
  }
  const int rainbowW = track.width - 2 * radius;
  for (int i = 0; i < 6; ++i) {
    const int x1 = rainbowW * i / 6, x2 = rainbowW * (i + 1) / 6;
    lv_obj_t* segment = lv_obj_create(card);
    lv_obj_remove_style_all(segment);
    lv_obj_remove_flag(segment, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_pos(segment, track.inset + radius + x1, trackY);
    lv_obj_set_size(segment, x2 - x1, track.height);
    lv_obj_set_style_bg_color(segment, lv_color_hex(hueStops[i]), 0);
    lv_obj_set_style_bg_grad_color(segment, lv_color_hex(hueStops[i + 1]), 0);
    lv_obj_set_style_bg_grad_dir(segment, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
  }
  s_modalHue = makeModalSlider(card, track, trackY);
  lv_slider_set_range(s_modalHue, 0, 359);
  lv_obj_set_style_bg_opa(s_modalHue, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_modalHue, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_add_event_cb(s_modalHue, onModalHue, LV_EVENT_RELEASED, nullptr);
}

static void buildModalWarmth(lv_obj_t* card, const ModalSliderSize& track, int y, int labelGap,
                             int warm, int cool) {
  modalColourTitle(card, "White temperature", track.inset, y, logicalW() >= 1000 && themeUsesModernLayout(s_theme));
  s_modalWarmth = makeModalSlider(card, track, y + labelGap);
  lv_slider_set_range(s_modalWarmth, warm, cool);
  lv_slider_set_value(s_modalWarmth, warm + (cool - warm) / 2, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_modalWarmth, lv_color_hex(0xFFAD5C), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(s_modalWarmth, lv_color_hex(0xD0E8FF), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(s_modalWarmth, LV_GRAD_DIR_HOR, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_modalWarmth, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_modalWarmth, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_add_event_cb(s_modalWarmth, onModalWarmth, LV_EVENT_RELEASED, nullptr);
}

static void refreshModalColour(const HaEntity& e) {
  if (s_modalWarmth && e.colorTempKelvin > 0 && e.colorTempKelvin != s_modalWarmthKelvin &&
      !lv_obj_has_state(s_modalWarmth, LV_STATE_PRESSED)) {
    lv_slider_set_value(s_modalWarmth, e.colorTempKelvin, LV_ANIM_OFF);
    s_modalWarmthKelvin = e.colorTempKelvin;
  }
  if (s_modalHue && e.rgb >= 0 && e.rgb != s_modalHueRgb && !lv_obj_has_state(s_modalHue, LV_STATE_PRESSED)) {
    const auto hsv = lv_color_to_hsv(lv_color_hex((uint32_t)e.rgb));
    if (hsv.s > 2) lv_slider_set_value(s_modalHue, hsv.h % 360, LV_ANIM_OFF);
    s_modalHueRgb = e.rgb;
  }
  if (s_modalSwatches[0]) {
    for (int i = 0; i < LIGHT_PRESET_COUNT; ++i) {
      if (e.state == "on" && e.rgb == (int32_t)LIGHT_PRESETS[i]) lv_obj_add_state(s_modalSwatches[i], LV_STATE_CHECKED);
      else lv_obj_remove_state(s_modalSwatches[i], LV_STATE_CHECKED);
    }
  }
}

void refreshModal() {
  if (s_modalTile < 0 || !s_modalCard) return;
  const TileRt& r = s_tiles[s_modalTile];
  if (s_confirmHold >= 0) {
    // The state changed under the question (HA, another device): ask again.
    if (r.on != s_confirmOn) closeModal();
    return;
  }
  String st = tileSub(r);
  HaEntity e;
  const bool haKnown = r.def->entity.length() && haGetEntity(r.def->entity, e);
  if (haKnown) refreshModalColour(e);
  if (s_studioColour) {
    const uint32_t rgb = haKnown ? lightColourRgb(e) : 0xFFFFFFFFu;
    lv_obj_set_style_bg_color(s_studioColour, lv_color_hex(rgb == 0xFFFFFFFFu ? 0xFFBF60u : rgb), 0);
  }
  if (s_studioKelvin) {
    if (haKnown && e.colorTempKelvin > 0) lv_label_set_text_fmt(s_studioKelvin, "%d K", e.colorTempKelvin);
    else lv_label_set_text(s_studioKelvin, "-- K");
  }
  if (s_studioPower) {
    const bool lit = haKnown && wishOn(r.def->entity, e);
    lv_obj_set_style_bg_color(s_studioPower, lv_color_hex(lit ? modernColour(0x5286C3) : modernColour(0x364B64)), 0);
    lv_obj_align(s_studioPowerKnob, lit ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, lit ? -5 : 5, 0);
  }
  if (haKnown) st = wishText(r.def->entity, e, false);
  if (r.def->type == TILE_HA_MEDIA && themeUsesModernLayout(s_theme)) {
    refreshHaMediaSheet(r, haKnown ? &e : nullptr);
  } else if (s_climate) {
    climateRefresh(haKnown ? &e : nullptr);
  } else if (s_modalWeatherCondition) {
    const bool good = haKnown && e.known && e.state != "unavailable" && e.state != "unknown";
    lv_label_set_text(s_modalState, good ? weatherTempText(e, true).c_str() : "--");
    lv_label_set_text(s_modalWeatherCondition, good ? weatherCondition(e.state).c_str() : "Waiting for weather");
    if (s_weatherFeelsLabel) {
      String feels = good && isfinite(e.weatherFeels) ? String("Feels like ") + weatherTempText(e, true, true) : String("");
      lv_label_set_text(s_weatherFeelsLabel, feels.c_str());
    }
    refreshWeatherCards(good ? &e : nullptr);
  } else if (r.def->type == TILE_SENSOR && r.def->style == METRIC_HEADLINE &&
             entityDomain(r.def->entity) == "climate") {
    lv_label_set_text(s_modalState, haKnown ? climateCurrentText(e, true).c_str() : "--");
  } else if (s_modalSlider) {
    const float f = haKnown ? wishFrac(r.def->entity, e) : -1;
    const int pct = f > 0 ? (int)lroundf(f * 100) : 0;
    if (!lv_obj_has_state(s_modalSlider, LV_STATE_PRESSED)) lv_slider_set_value(s_modalSlider, pct, LV_ANIM_OFF);
    if (s_studioBrightness && !lv_obj_has_state(s_modalSlider, LV_STATE_PRESSED)) lv_label_set_text_fmt(s_studioBrightness, "%d%%", pct);
    lv_label_set_text(s_modalState, (haKnown ? wishText(r.def->entity, e, true) : st).c_str());
  } else {
    lv_label_set_text(s_modalState, st.c_str());
  }
  if (r.def->type == TILE_TOGGLE || (r.def->type == TILE_SLIDER && r.def->tap.kind == ACT_TOGGLE))
    lv_label_set_text(s_modalBtnLbl, actionVerb(r, r.def->tap));
  if (s_modalChart) {
    float value;
    const bool numeric = haKnown && e.known && haNumericState(e.state, value);
    if (numeric) {
      if (!s_modalBuilding) haDetailHistory(r.def->entity, s_modalHours);
      copyHistoryChart(*r.def, s_modalHours, s_modalHistoryVersion, s_modalChart,
                       s_modalPoints, s_modalMin, s_modalMax);
    }
    showObj(s_modalChart, numeric);
    showObj(s_modalRange, numeric);
    lv_chart_set_series_color(s_modalChart, s_modalSeries, lv_color_hex(sensorLine(r)));
    lv_obj_set_style_text_color(s_modalState, lv_color_hex(sensorColour(r)), 0);
    if (numeric) {
      char range[128];
      if (isfinite(s_modalMin))
        snprintf(range, sizeof(range), "Last %u h    Min %.3g    Max %.3g %s",
                 s_modalHours, s_modalMin, s_modalMax, e.unit.c_str());
      else snprintf(range, sizeof(range), "Last %u h    No history yet", s_modalHours);
      lv_label_set_text(s_modalRange, range);
    }
  }
  const bool on = haKnown && wishOn(r.def->entity, e);
  const int iconPx = s_weatherArtworkSize >= 100 ? 64 : themeUsesModernLayout(s_theme) ? 40 : 28;
  if (s_modalWeatherCondition && haKnown &&
      (!r.def->icon.length() || r.def->type == TILE_WEATHER_CLOCK)) {
    TileDef visual = *r.def;
    visual.icon = String("mdi:") + weatherIcon(e.state);
    s_modalIconCp = showIcon(s_modalIcon, visual, &e, on, iconPx, s_modalIconCp);
  } else
    s_modalIconCp = showIcon(s_modalIcon, *r.def, haKnown ? &e : nullptr, on, iconPx, s_modalIconCp);
  if (s_weatherArtworkSize && s_modalWeatherCondition && s_modalIcon && haKnown && e.known && e.state != "unknown" && e.state != "unavailable")
    updateModalWeatherArtwork(lv_obj_get_parent(s_modalIcon), s_modalIcon, *r.def, e.state, s_weatherArtworkSize);
  modalLightIcon(s_modalIcon, *r.def, haKnown ? &e : nullptr);
}

// Detail card for a tile (hold action "more-info"): name, entity, state, a
// level slider (light brightness, cover position...), and the tap action as a
// button.
// Touch effects stop, and LVGL draws the page once without them: the card's
// background and animation take their picture of the page from that frame.
void cleanPage() {
  if (fxCancelRipples()) lv_refr_now(s_disp);
}

void openModal(int tileIdx) {
  if (tileIdx < 0 || size_t(tileIdx) >= s_tiles.size() || !tilePinAccess(s_tiles[tileIdx])) return;
  closeModal();
  cleanPage();
  const TileRt& r = s_tiles[tileIdx];
  const TileDef& d = *r.def;
  const int w = logicalW(), h = logicalH();
  const bool sheet = themeUsesModernLayout(s_theme);
  const bool media = sheet && d.type == TILE_HA_MEDIA;
  const uint32_t accent = sheet ? tileAccent(d) : d.hasColor ? d.color : domainColor(entityDomain(d.entity));
  s_modalTile = tileIdx;
  const bool light = !media && modalHasSlider();
  HaEntity he;
  float hv;
  const bool haKnown = d.entity.length() && haGetEntity(d.entity, he);
  // Climate-backed temperature headlines open the same thermostat as climate tiles.
  const bool climate = entityDomain(d.entity) == "climate" && !d.http.enabled;
  const bool weather = !climate && ((d.type == TILE_SENSOR && d.style == METRIC_WEATHER) ||
                                    d.type == TILE_WEATHER_CLOCK);
  const bool knownLight = !climate && !weather && entityDomain(d.entity) == "light" && haKnown && he.known;
  const bool rgbLight = knownLight && he.lightSupportsColor;
  const int warm = he.minColorTempKelvin > 0 ? he.minColorTempKelvin : 2000;
  const int cool = he.maxColorTempKelvin > 0 ? he.maxColorTempKelvin : 6535;
  const bool warmth = knownLight && he.lightSupportsWarmth && warm < cool;
  const bool bothLightControls = rgbLight && warmth;
  const bool compactLightCard = h < 360;
  const bool studio = sheet && knownLight && w >= 1000 && w > h;
  const bool temperatureStudio = sheet && !climate && !weather && !d.http.enabled &&
      d.type == TILE_SENSOR && haKnown &&
      (he.deviceClass == "temperature" || he.unit == "°C" || he.unit == "°F");
  const bool graph = !climate && !weather && !rgbLight && !warmth && d.type == TILE_SENSOR &&
                     (d.style != METRIC_HEADLINE || temperatureStudio) && haKnown && he.known && haNumericState(he.state, hv);

  s_modalScrim = makeScrim(!sheet);

  const bool largePanel = std::min(w, h) >= 800;
  const bool fullScreen = !sheet && (climate || weather);
  const auto px = [largePanel, weather, sheet](int n) { return largePanel && (weather || sheet) ? n * 7 / 4 : n; };
  const int cw = sheet ? (w * 98 + 50) / 100 : fullScreen ? w : std::min(w - 40, largePanel ? 520 : 380);
  const int cardH = bothLightControls ? (compactLightCard ? 304 : 356) : (rgbLight || warmth) ? 294 :
                    graph ? (light ? 304 : 284) : light ? 236 : 196;
  const bool compactWeather = sheet && weather && !largePanel;
  const bool compactSheet = sheet && !largePanel;
  const int ch = sheet ? (compactSheet ? h - 8 : media && largePanel ? h * 7 / 10 : h * 3 / 4) : fullScreen ? h : cardH;
  lv_obj_t* c = lv_obj_create(lv_layer_top());
  s_modalCard = c;
  lv_obj_add_flag(c, LV_OBJ_FLAG_FLOATING);
  lv_obj_remove_flag(c, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER));
  lv_obj_remove_style_all(c);
  const int sheetRadius = compactSheet ? 22 : 54;
  // Extend the rounded rectangle off-screen so only its top corners show.
  lv_obj_set_size(c, cw, sheet ? ch + sheetRadius : ch);
  if (sheet) lv_obj_set_pos(c, (w - cw) / 2, h - ch);
  else if (fullScreen) lv_obj_set_pos(c, 0, 0);
  else lv_obj_center(c);
  lv_obj_set_style_bg_color(c, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, sheet ? sheetRadius : fullScreen ? 0 : 22, 0);
  if (sheet) {
    cinematicPopupSurface(c);
    // Keep the palette and gradient, but prevent dashboard text showing
    // through the compact weather sheet in Glass mode.
    if (compactSheet) lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(c, 1, 0);
    lv_obj_set_style_outline_pad(c, -1, 0);
    lv_obj_set_style_outline_color(c, lv_color_hex(C_TILE_LINE), 0);
  }
  if (!sheet && themeUsesWallpaper(s_theme) && !fullScreen &&
      (s_modalGlass = frostedCopy((w - cw) / 2, (h - ch) / 2, cw, ch, 22)))
    frostStyle(c, s_modalGlass);
  lv_obj_set_style_text_color(c, lv_color_hex(C_TEXT), 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);  // taps on the card stay on the card
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* chip = lv_obj_create(c);
  lv_obj_remove_style_all(chip);
  lv_obj_set_size(chip, px(44), px(44));
  lv_obj_set_pos(chip, px(16), px(16));
  lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  styleIconChip(chip, accent);
  lv_obj_set_style_text_font(chip, &deck_font_20, 0);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE);
  s_modalIcon = lv_label_create(chip);
  s_modalIconCp = 0;
  lv_label_set_text(s_modalIcon, "");
  lv_obj_center(s_modalIcon);
  if (climate && (!sheet || d.noName)) lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
  if (weather) {
    lv_obj_set_pos(chip, px(16), px(14));
    lv_obj_set_size(chip, px(42), px(42));
    lv_obj_set_style_radius(chip, px(12), 0);
  }

  // Name and entity id: one line each, cut with dots (ids get long).
  lv_obj_t* name = lv_label_create(c);
  String title = tileLabel(d);
  if (!d.label.length() && haKnown && he.name.length())
    title = he.name;
  if (weather && d.weatherPopupTitle.length()) title = d.weatherPopupTitle;
  lv_label_set_text(name, title.c_str());
  const lv_font_t* nameFont = largePanel ? &deck_font_28 : &deck_font_20;
  lv_obj_set_style_text_font(name, nameFont, 0);
  lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_size(name, cw - px(sheet ? (knownLight ? 220 : 136) : 88), lv_font_get_line_height(nameFont));
  lv_obj_set_pos(name, px(72), px(15));
  if ((climate && (!sheet || d.noName)) || (weather && d.hideWeatherPopupTitle))
    lv_obj_add_flag(name, LV_OBJ_FLAG_HIDDEN);
  if ((climate && sheet) || media) {
    // Single-line headings are centred vertically on their chip.
    lv_obj_set_pos(name, px(72), px(16) + (px(44) - lv_font_get_line_height(nameFont)) / 2);
  }
  if (weather) {
    const int headingInset = sheet ? px(12) : px(68);
    lv_obj_set_pos(name, headingInset, px(16));
    lv_obj_set_width(name, cw - headingInset - px(50));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
  }
  lv_obj_t* ent = lv_label_create(c);
  lv_label_set_text(ent, d.entity.length() ? d.entity.c_str() : "No entity");
  // Weather clocks have a clean header. Weather sensor tiles can still show their ID.
  if (media || climate || (sheet && knownLight) || d.type == TILE_WEATHER_CLOCK ||
      (d.type == TILE_SENSOR && !d.showEntityId))
    lv_obj_add_flag(ent, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(ent, lv_color_hex(C_DIM), 0);
  lv_label_set_long_mode(ent, LV_LABEL_LONG_MODE_DOTS);
  const lv_font_t* detailFont = largePanel && weather ? &deck_font_20 : &deck_font_14;
  lv_obj_set_style_text_font(ent, detailFont, 0);
  lv_obj_set_size(ent, cw - px(sheet ? 136 : 88), lv_font_get_line_height(detailFont));
  lv_obj_set_pos(ent, px(weather ? 68 : 72), px(weather ? 46 : sheet ? 40 : largePanel ? 52 : 41));
  if (weather) {
    const int headingInset = sheet ? px(12) : px(68);
    lv_obj_set_pos(ent, headingInset, px(d.hideWeatherPopupTitle ? 16 : 38));
    lv_obj_set_width(ent, cw - headingInset - px(50));
  }

  if (media) {
    s_modalState = nullptr;
    buildHaMediaSheet(c, cw, ch, compactSheet ? 52 : px(64), tileIdx, accent, largePanel);
  } else {
    s_modalState = lv_label_create(c);
    const lv_font_t* stateFont = &deck_font_28;
    lv_obj_set_style_text_font(s_modalState, stateFont, 0);
    lv_label_set_long_mode(s_modalState, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(s_modalState, cw - px(32), lv_font_get_line_height(stateFont));
    lv_obj_set_pos(s_modalState, sheet && knownLight ? px(72) : px(16),
                   sheet && knownLight ? px(38) : px(bothLightControls && compactLightCard ? 60 : 74));
    if (sheet && knownLight) {
      lv_obj_set_style_text_font(s_modalState, &deck_font_14, 0);
      lv_obj_set_size(s_modalState, cw - px(220), lv_font_get_line_height(&deck_font_14));
    }
  }

  s_modalSlider = nullptr;
  if (weather) {
    const bool landscape = cw > ch;
    const bool studioWeather = sheet;
    const bool compactWide = compactWeather && landscape;
    const bool wideWeather = studioWeather && (cw >= 700 || compactWide) && landscape;
    const int inset = compactWeather ? 8 : px(12);
    const bool weatherEntityVisible = !lv_obj_has_flag(ent, LV_OBJ_FLAG_HIDDEN);
    // Keep a small top strip for the close button, but reclaim the unused title row.
    const int heroY = compactWeather
        ? (weatherEntityVisible && !d.hideWeatherPopupTitle ? 58 : 42)
        : px(d.hideWeatherPopupTitle ? (weatherEntityVisible ? 46 : 42) : (weatherEntityVisible ? 62 : 46));
    const int heroW = wideWeather ? cw * 34 / 100 - inset : cw - 2 * inset;
    const int heroH = wideWeather ? ch - heroY - inset : compactWeather ? 96 : px(112);
    const int infoX = studioWeather ? (wideWeather ? heroW + inset * 2 : inset) : px(landscape ? 194 : 16);
    const int infoY = studioWeather ? (wideWeather ? heroY : heroY + heroH + inset) : px(landscape ? 76 : 192);
    const int gap = px(8);
    const int columns = wideWeather && !compactWide ? 3 : 2;
    const int gridW = cw - infoX - inset;
    const int colW = (gridW - (columns - 1) * gap) / columns;
    const int gridH = std::max(24, ch - infoY - inset);
    const int rowH = compactWeather ? 54 : wideWeather ? std::max(px(55), (gridH - 2 * gap) / 3) : studioWeather ? px(61) : px(landscape ? 65 : 68);
    const lv_font_t* tempFont = &lv_font_montserrat_40;
#if LV_FONT_MONTSERRAT_48
    if (largePanel) tempFont = &lv_font_montserrat_48;
#endif
    const lv_font_t* conditionFont = largePanel ? &deck_font_28 : &deck_font_20;
    const lv_font_t* metricLabelFont = largePanel ? &deck_font_20 : &deck_font_12;

    if (studioWeather) {
      studioPanel(c, inset, heroY, heroW, heroH);
      lv_obj_move_to_index(lv_obj_get_child(c, lv_obj_get_child_count(c) - 1), 0);
    }
    lv_obj_set_style_text_font(s_modalState, tempFont, 0);
    lv_obj_set_style_text_align(s_modalState, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(s_modalState, px(landscape ? 20 : 24), px(landscape ? 110 : 86));
    lv_obj_set_size(s_modalState, landscape ? px(168) : cw - px(48),
                    lv_font_get_line_height(tempFont));

    s_modalWeatherCondition = lv_label_create(c);
    lv_obj_set_pos(s_modalWeatherCondition, px(landscape ? 23 : 26), px(landscape ? 172 : 138));
    lv_obj_set_size(s_modalWeatherCondition, landscape ? px(168) : cw - px(52),
                    lv_font_get_line_height(conditionFont));
    lv_obj_set_style_text_font(s_modalWeatherCondition, conditionFont, 0);
    lv_obj_set_style_text_color(s_modalWeatherCondition, lv_color_hex(C_DIM), 0);
    lv_label_set_long_mode(s_modalWeatherCondition, LV_LABEL_LONG_MODE_DOTS);

    if (studioWeather) {
      const int iconD = compactWide ? 64 : wideWeather ? std::min(heroW - px(24), heroH * 36 / 100) : px(44);
      s_weatherArtworkSize = iconD;
      lv_obj_set_size(chip, iconD, iconD);
      lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, 0);
      lv_obj_set_pos(chip, wideWeather ? inset + (heroW - iconD) / 2 : inset + px(10),
                     heroY + (wideWeather ? px(6) : px(8)));
      const int tx = wideWeather ? inset + px(12) : inset + iconD + px(18);
      const int tw = wideWeather ? heroW - px(24) : heroW - iconD - px(28);
      const int ty = compactWide ? heroY + 76 : wideWeather ? heroY + heroH * 40 / 100 : heroY + px(8);
      lv_obj_set_pos(s_modalState, tx, ty);
      lv_obj_set_size(s_modalState, tw, lv_font_get_line_height(tempFont));
      lv_obj_set_style_text_align(s_modalState, wideWeather ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
      const int cy = ty + lv_font_get_line_height(tempFont) + px(4);
      lv_obj_set_pos(s_modalWeatherCondition, tx, cy);
      lv_obj_set_size(s_modalWeatherCondition, tw, lv_font_get_line_height(conditionFont));
      lv_obj_set_style_text_align(s_modalWeatherCondition, wideWeather ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
      lv_obj_set_style_text_color(s_modalWeatherCondition, lv_color_hex(modernColour(0xA5BCD5)), 0);
      s_weatherFeelsLabel = lv_label_create(c);
      lv_obj_set_style_text_font(s_weatherFeelsLabel, metricLabelFont, 0);
      lv_obj_set_style_text_color(s_weatherFeelsLabel, lv_color_hex(modernColour(0x85BCFF)), 0);
      lv_obj_set_style_text_align(s_weatherFeelsLabel, wideWeather ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
      lv_obj_set_pos(s_weatherFeelsLabel, tx, cy + lv_font_get_line_height(conditionFont) + px(4));
      lv_obj_set_size(s_weatherFeelsLabel, tw, lv_font_get_line_height(metricLabelFont));
      lv_label_set_long_mode(s_weatherFeelsLabel, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_move_foreground(s_modalState);
    }

    // Extra observations scroll inside the grid on smaller screens.
    lv_obj_t* grid = lv_obj_create(c);
    lv_obj_remove_style_all(grid);
    lv_obj_set_pos(grid, infoX, infoY);
    lv_obj_set_size(grid, gridW, gridH);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(grid, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN_HOR |
        LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_SCROLL_ELASTIC));
    lv_obj_set_style_bg_color(grid, lv_color_hex(C_DIM), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(grid, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(grid, px(4), LV_PART_SCROLLBAR);

    for (int i = 0; i < WEATHER_CARD_COUNT; ++i) {
      const int col = i % columns;
      const int row = i / columns;
      lv_obj_t* panel = lv_obj_create(grid);
      lv_obj_remove_style_all(panel);
      lv_obj_set_pos(panel, col * (colW + gap), row * (rowH + gap));
      lv_obj_set_size(panel, colW, rowH);
      lv_obj_set_style_bg_color(panel, lv_color_hex(C_TILE), 0);
      lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(panel, px(12), 0);
      lv_obj_set_style_border_width(panel, 1, 0);
      lv_obj_set_style_border_color(panel, lv_color_hex(C_TILE_LINE), 0);
      lv_obj_remove_flag(panel, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
      if (studioWeather) cinematicSurface(panel, 2);
      s_weatherCardBoxes[i] = panel;

      lv_obj_t* label = lv_label_create(panel);
      lv_obj_set_style_text_font(label, metricLabelFont, 0);
      lv_obj_set_style_text_color(label, lv_color_hex(C_DIM), 0);
      lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_set_pos(label, px(10), px(8));
      lv_obj_set_size(label, colW - px(20), lv_font_get_line_height(metricLabelFont));
      s_weatherCardNames[i] = label;

      lv_obj_t* value = lv_label_create(panel);
      lv_obj_set_style_text_font(value, conditionFont, 0);
      lv_obj_set_style_text_color(value, lv_color_hex(C_TEXT), 0);
      lv_label_set_long_mode(value, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_set_pos(value, px(10), compactWeather ? std::max<int>(23, rowH - lv_font_get_line_height(conditionFont) - 5) : studioWeather ? std::max<int>(px(26), rowH - lv_font_get_line_height(conditionFont) - px(10)) : px(29));
      lv_obj_set_size(value, colW - px(20), lv_font_get_line_height(conditionFont));
      s_weatherCardValues[i] = value;
    }
  }
  if (climate) climateBuild(c, cw, ch, accent, haKnown ? &he : nullptr, largePanel);
  if (temperatureStudio) {
    const int y = px(66), inset = px(16);
    const int ph = px(82);
    studioPanel(c, inset, y, cw - inset * 2, ph);
    lv_obj_move_to_index(lv_obj_get_child(c, lv_obj_get_child_count(c) - 1), 0);
    auto* caption = lv_label_create(c);
    lv_label_set_text(caption, "Current temperature");
    lv_obj_set_style_text_font(caption, largePanel ? &deck_font_20 : &deck_font_14, 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(modernColour(0xA5BCD5)), 0);
    lv_obj_set_pos(caption, inset + px(12), y + px(8));
    const auto* f = largePanel ? &lv_font_montserrat_40 : &deck_font_28;
    lv_obj_set_style_text_font(s_modalState, f, 0);
    lv_obj_set_pos(s_modalState, inset + px(12), y + px(33));
    lv_obj_set_size(s_modalState, cw - inset * 4, lv_font_get_line_height(f));
    lv_obj_move_foreground(s_modalState);
  }
  if (light && !climate && !weather) {
    const bool compactStudio = compactSheet && knownLight;
    const bool useStudio = studio || compactStudio;
    const int sliderY = sheet ? ch / 2 - (bothLightControls ? px(32) : 0) :
                        bothLightControls && compactLightCard ? 104 : 128;
    const int inset = px(26);
    const int split = (rgbLight || warmth) ? cw * (compactStudio ? 36 : 37) / 100 : cw - px(14);
    const int panelY = compactStudio ? 58 : px(70), panelH = ch - panelY - px(12);
    const int panelX = px(14), rightX = split + px(8), rightW = cw - rightX - panelX;
    const int colourH = warmth && (!compactStudio || rgbLight) ? panelH * (compactStudio ? 49 : 58) / 100 : panelH;
    if (useStudio) {
      studioPanel(c, panelX, panelY, split - panelX, panelH);
      if (rgbLight) studioPanel(c, rightX, panelY, rightW, colourH);
      if (warmth) studioPanel(c, rightX, panelY + (rgbLight ? colourH + px(7) : 0),
                              rightW, rgbLight ? panelH - colourH - px(7) : panelH);
    }
    const int controlInset = compactStudio ? 9 : px(18);
    const ModalSliderSize track{useStudio ? rightX + controlInset : inset, useStudio ? rightW - 2 * controlInset : cw - 2 * inset, px(14), compactStudio ? 4 : px(7), px(7)};
    const int brightW = compactStudio ? 58 : px(72);
    const bool splitPresets = compactStudio && (rgbLight || warmth);
    const int brightTop = panelY + (compactStudio ? 30 : px(35));
    const int brightHeight = compactStudio ? panelH - (splitPresets ? 122 : 92) : panelH - px(98);
    const ModalSliderSize brightnessTrack{useStudio ? (panelX + split - brightW) / 2 : inset, useStudio ? brightW : track.width, useStudio ? brightHeight : track.height, useStudio ? 0 : px(7), px(7)};
    lv_obj_t* sl = makeModalSlider(c, brightnessTrack, useStudio ? brightTop : sliderY);
    if (useStudio) {
      modalColourTitle(c, "Brightness", panelX + (compactStudio ? 6 : px(14)), panelY + px(10), !compactStudio);
      s_studioBrightness = lv_label_create(c);
      lv_obj_set_style_text_font(s_studioBrightness, compactStudio ? &deck_font_20 : &deck_font_28, 0);
      lv_obj_remove_flag(s_studioBrightness, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_text_color(s_studioBrightness, lv_color_hex(C_TEXT), 0);
      lv_obj_set_style_text_align(s_studioBrightness, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_width(s_studioBrightness, brightW);
      lv_obj_set_pos(s_studioBrightness, brightnessTrack.inset, brightTop + brightnessTrack.height / 2 - lv_font_get_line_height(compactStudio ? &deck_font_20 : &deck_font_28) / 2);
      const int gap = px(4), pad = compactStudio ? 5 : px(12);
      const int cols = splitPresets ? 2 : 4;
      const int bw = (split - panelX - 2 * pad - (cols - 1) * gap) / cols;
      const int bh = compactStudio ? 29 : px(25);
      for (int i = 0; i < 4; ++i) {
        const int pct = 25 * (i + 1);
        char label[8];
        snprintf(label, sizeof(label), "%d%%", pct);
        auto* preset = qpButton(c, panelX + pad + (i % cols) * (bw + gap), panelY + panelH - (splitPresets ? 2 * bh + gap + 8 : compactStudio ? bh + 10 : px(37)) + (i / cols) * (bh + gap), bw, label, false);
        lv_obj_set_height(preset, bh);
        lv_obj_set_style_radius(preset, px(8), 0);
        lv_obj_set_style_bg_color(preset, lv_color_hex(modernColour(0x304B6B)), 0);
        lv_obj_set_style_text_color(preset, lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_text_font(lv_obj_get_child(preset, 0), compactStudio ? &deck_font_12 : &deck_font_20, 0);
        lv_obj_add_event_cb(preset, onStudioBrightnessPreset, LV_EVENT_CLICKED, (void*)(uintptr_t)pct);
      }
    }
    s_modalSlider = sl;
    lv_slider_set_range(sl, 0, 100);
    lv_obj_set_style_bg_color(sl, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(sheet && knownLight ? modernColour(0x5286C3) : accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_INDICATOR);
    if (useStudio) {
      lv_obj_set_style_bg_color(sl, lv_color_hex(modernColour(0x0D192A)), LV_PART_MAIN);
      lv_obj_set_style_border_width(sl, 2, LV_PART_MAIN);
      lv_obj_set_style_border_color(sl, lv_color_hex(modernColour(0x496681)), LV_PART_MAIN);
      lv_obj_set_style_pad_all(sl, 5, LV_PART_MAIN);
      lv_obj_set_style_radius(sl, px(22), LV_PART_MAIN);
      lv_obj_set_style_radius(sl, px(19), LV_PART_INDICATOR);
      lv_obj_set_style_bg_grad_color(sl, lv_color_hex(modernColour(0x82BCF9)), LV_PART_INDICATOR);
      lv_obj_set_style_bg_grad_dir(sl, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
      lv_obj_set_style_opa(sl, LV_OPA_TRANSP, LV_PART_KNOB);
    }
    lv_obj_add_event_cb(sl, onModalSlider, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(sl, onModalSlider, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(sl, onModalSlider, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(sl, onModalSlider, LV_EVENT_PRESS_LOST, nullptr);
    const int colourY = useStudio ? panelY + px(12) : sliderY + px(32);
    const int labelGap = compactStudio ? 23 : px(useStudio ? 29 : 22);
    if (rgbLight) {
      buildModalColour(c, track, colourY, labelGap, px(useStudio ? 14 : 16), d.lightPopupPresets, useStudio, compactStudio);
      if (useStudio) {
        s_studioColour = lv_obj_create(c);
        lv_obj_remove_style_all(s_studioColour);
        lv_obj_set_size(s_studioColour, px(18), px(18));
        lv_obj_set_pos(s_studioColour, track.inset + track.width - px(18), colourY);
        lv_obj_set_style_radius(s_studioColour, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_studioColour, LV_OPA_COVER, 0);
        lv_obj_remove_flag(s_studioColour, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
      }
    }
    const int warmthY = useStudio ? panelY + (rgbLight ? colourH + (compactStudio ? 12 : px(19)) : px(12)) :
                          colourY + (rgbLight ? px(d.lightPopupPresets ? 64 : 54) : 0);
    if (warmth) {
      buildModalWarmth(c, track, warmthY, labelGap, warm, cool);
      if (useStudio) {
        s_studioKelvin = lv_label_create(c);
        lv_obj_set_style_text_font(s_studioKelvin, &deck_font_14, 0);
        lv_obj_set_style_text_align(s_studioKelvin, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(s_studioKelvin, compactStudio ? 65 : px(100));
        lv_obj_set_pos(s_studioKelvin, track.inset + track.width - (compactStudio ? 65 : px(100)), warmthY);
        if (compactStudio && track.width < 195) lv_obj_set_pos(s_studioKelvin, track.inset + track.width - 65, warmthY + 43);
        if (!compactStudio || track.width >= 195) {
          modalColourTitle(c, "Warm", track.inset, warmthY + (compactStudio ? 44 : px(useStudio ? 54 : 43)));
          modalColourTitle(c, "Cool", track.inset + track.width - px(28), warmthY + (compactStudio ? 44 : px(useStudio ? 54 : 43)));
        }
      }
    }
  }

  if (graph) {
    s_modalHours = d.style == METRIC_CHART ? d.hours : 24;
    s_modalPoints = (int32_t*)heap_caps_malloc(sizeof(int32_t) * HA_HISTORY_POINTS, MALLOC_CAP_SPIRAM);
    if (s_modalPoints) {
      s_modalChart = lv_chart_create(c);
      lv_obj_remove_style_all(s_modalChart);
      const int chartY = sheet ? (temperatureStudio ? px(158) : light ? ch / 2 + px(20) : px(111)) : light ? 154 : 112;
      const int chartH = sheet ? std::max(12, ch - chartY - px(36)) : light ? 50 : 76;
      lv_obj_set_pos(s_modalChart, px(16), chartY);
      lv_obj_set_size(s_modalChart, cw - px(32), chartH);
      lv_chart_set_type(s_modalChart, LV_CHART_TYPE_LINE);
      lv_chart_set_div_line_count(s_modalChart, 0, 0);
      lv_chart_set_point_count(s_modalChart, HA_HISTORY_POINTS);
      lv_obj_set_style_line_width(s_modalChart, 2, LV_PART_ITEMS);
      lv_obj_set_style_width(s_modalChart, 0, LV_PART_INDICATOR);
      lv_obj_set_style_height(s_modalChart, 0, LV_PART_INDICATOR);
      lv_obj_remove_flag(s_modalChart, LV_OBJ_FLAG_CLICKABLE);
      s_modalSeries = lv_chart_add_series(s_modalChart, lv_color_hex(sensorLine(r)), LV_CHART_AXIS_PRIMARY_Y);
      for (int i = 0; i < HA_HISTORY_POINTS; i++) s_modalPoints[i] = LV_CHART_POINT_NONE;
      lv_chart_set_series_ext_y_array(s_modalChart, s_modalSeries, s_modalPoints);
      s_modalRange = lv_label_create(c);
      lv_obj_set_style_text_font(s_modalRange, &deck_font_12, 0);
      lv_label_set_long_mode(s_modalRange, LV_LABEL_LONG_MODE_WRAP);
      lv_obj_set_size(s_modalRange, cw - px(32), px(18));
      lv_obj_set_pos(s_modalRange, px(16), sheet ? chartY + chartH + px(8) : light ? 212 : 196);
    }
  }

  const int splitWidth = (cw - px(32) - px(10)) / 2;
  const int bw = sheet ? std::min(splitWidth, 310) : splitWidth;
  const int by = ch - px(16) - px(44);
  const bool headerPower = sheet && knownLight;
  lv_obj_t* primary = qpButton(c, headerPower ? cw - px(132) : px(16), headerPower ? px(14) : by, headerPower ? px(84) : bw, "", false);
  lv_obj_set_height(primary, px(headerPower ? 34 : 44));
  lv_obj_set_style_bg_color(primary, lv_color_hex(headerPower ? modernColour(0x29496A) : accent), 0);
  lv_obj_set_style_text_color(primary, lv_color_hex(headerPower ? C_TEXT : C_TEXT_ON), 0);
  s_modalBtnLbl = lv_obj_get_child(primary, 0);
  const lv_font_t* buttonFont = largePanel ? &deck_font_20 : &deck_font_14;
  lv_obj_set_style_text_font(s_modalBtnLbl, buttonFont, 0);
  lv_label_set_text(s_modalBtnLbl, "Run");
  lv_obj_add_event_cb(primary, onModalPrimary, LV_EVENT_CLICKED, nullptr);
  // Nothing to run when the tap only opens this card (sensors, numbers).
  const bool noPrimary = media || d.type == TILE_SENSOR || d.type == TILE_CLIMATE || d.tap.kind == ACT_MORE_INFO;
  if (noPrimary || climate || weather) lv_obj_add_flag(primary, LV_OBJ_FLAG_HIDDEN);
  if (headerPower && !noPrimary) {
    lv_obj_add_flag(primary, LV_OBJ_FLAG_HIDDEN);
    s_studioPower = lv_obj_create(c);
    lv_obj_remove_style_all(s_studioPower);
    lv_obj_remove_flag(s_studioPower, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_studioPower, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_studioPower, cw - px(112), px(18));
    lv_obj_set_size(s_studioPower, px(58), px(29));
    lv_obj_set_style_radius(s_studioPower, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_studioPower, LV_OPA_COVER, 0);
    s_studioPowerKnob = lv_obj_create(s_studioPower);
    lv_obj_remove_style_all(s_studioPowerKnob);
    lv_obj_remove_flag(s_studioPowerKnob, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_size(s_studioPowerKnob, px(21), px(21));
    lv_obj_set_style_radius(s_studioPowerKnob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_studioPowerKnob, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_studioPowerKnob, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_studioPower, onModalPrimary, LV_EVENT_CLICKED, nullptr);
  }
  if (sheet || fullScreen) {
    const int climateScale = !sheet && climate && largePanel ? 2 : 1;
    const int size = px(sheet ? 34 : 48) * climateScale;
    const int inset = px(8) * climateScale;
    lv_obj_t* close = lv_obj_create(c);
    lv_obj_remove_style_all(close);
    lv_obj_remove_flag(close, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(close, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(close, cw - size - inset, inset);
    lv_obj_set_size(close, size, size);
    lv_obj_set_ext_click_area(close, inset);
    lv_obj_t* cross = lv_label_create(close);
    lv_label_set_text(cross, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(cross, largePanel ? &deck_font_28 : &deck_font_14, 0);
    lv_obj_set_style_text_color(cross, lv_color_hex(C_TEXT), 0);
    lv_obj_center(cross);
    lv_obj_remove_flag(cross, LV_OBJ_FLAG_CLICKABLE);
    // A plain X remains visually small, but the whole surrounding area is a touch target.
    lv_obj_set_ext_click_area(close, media ? px(18) : inset);
    lv_obj_add_event_cb(close, onModalScrim, LV_EVENT_CLICKED, nullptr);
  } else {
    lv_obj_t* close = qpButton(c, noPrimary ? px(16) : px(16) + bw + px(10), by,
                               noPrimary ? cw - px(32) : bw, "Close", false);
    lv_obj_set_height(close, px(44));
    lv_obj_set_style_text_font(lv_obj_get_child(close, 0), buttonFont, 0);
    lv_obj_add_event_cb(close, onModalScrim, LV_EVENT_CLICKED, nullptr);
  }

  setModalOpen(true);
  s_modalBuilding = true;
  refreshModal();
  s_modalBuilding = false;
  if (!sheet) fxCardOpen(c, tileIdx);
  // The history reply lands on core 0; HA waits for the animation started above.
  if (s_modalChart) refreshModal();
}

void onConfirmOk(lv_event_t*) {
  if (s_modalTile < 0 || s_confirmHold < 0 || millis() - s_confirmMs < CONFIRM_ARM_MS) return;
  TileRt& r = s_tiles[s_modalTile];
  const TileAction& a = s_confirmHold ? r.def->hold : r.def->tap;
  closeModal();
  runAction(r, a);
}

void onConfirmTimeout(lv_timer_t*) {
  s_confirmTimer = nullptr;  // a one-shot timer deletes itself
  closeModal();
}

// Confirmation card for tiles with "confirm". Cancel, the scrim, the
// timeout or a state change close it without running anything.
void openConfirm(int tileIdx, bool hold) {
  if (tileIdx < 0 || size_t(tileIdx) >= s_tiles.size() || !tilePinAccess(s_tiles[tileIdx])) return;
  closeModal();
  cleanPage();
  const TileRt& r = s_tiles[tileIdx];
  const TileDef& d = *r.def;
  const TileAction& a = hold ? d.hold : d.tap;
  const int w = logicalW(), h = logicalH();
  const uint32_t accent = d.hasColor ? d.color : domainColor(entityDomain(d.entity));
  constexpr uint32_t C_DANGER = 0xE5534B;
  s_modalTile = tileIdx;
  s_confirmHold = hold ? 1 : 0;
  s_confirmOn = r.on;
  s_confirmMs = millis();

  s_modalScrim = makeScrim();

  const bool largePanel = std::min(w, h) >= 800;
  const int cw = std::min(w - 40, largePanel ? 480 : 360);
  const int ch = largePanel ? 212 : 188;
  const int buttonH = largePanel ? 56 : 44;
  const lv_font_t* buttonFont = largePanel ? &deck_font_20 : &deck_font_14;
  lv_obj_t* c = lv_obj_create(lv_layer_top());
  s_modalCard = c;
  lv_obj_remove_style_all(c);
  lv_obj_set_size(c, cw, ch);
  lv_obj_center(c);
  lv_obj_set_style_bg_color(c, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 22, 0);
  if (themeUsesModernLayout(s_theme)) cinematicPopupSurface(c);
  else if (themeUsesWallpaper(s_theme) &&
      (s_modalGlass = frostedCopy((w - cw) / 2, (h - ch) / 2, cw, ch, 22)))
    frostStyle(c, s_modalGlass);
  lv_obj_set_style_text_color(c, lv_color_hex(C_TEXT), 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* chip = lv_obj_create(c);
  lv_obj_remove_style_all(chip);
  lv_obj_set_size(chip, 44, 44);
  lv_obj_set_pos(chip, 16, 16);
  lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  styleIconChip(chip, accent);
  lv_obj_set_style_text_font(chip, &deck_font_20, 0);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE);
  s_modalIcon = lv_label_create(chip);
  lv_label_set_text(s_modalIcon, "");
  lv_obj_center(s_modalIcon);
  HaEntity he;
  const bool haKnown = d.entity.length() && haGetEntity(d.entity, he);
  s_modalIconCp = showIcon(s_modalIcon, d, haKnown ? &he : nullptr, r.on, 28, 0);
  modalLightIcon(s_modalIcon, d, haKnown ? &he : nullptr);

  String title = tileLabel(d);
  if (!d.label.length() && haKnown && he.name.length()) title = he.name;
  lv_obj_t* name = lv_label_create(c);
  lv_label_set_text(name, title.c_str());
  lv_obj_set_style_text_font(name, &deck_font_20, 0);
  lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_size(name, cw - 88, lv_font_get_line_height(&deck_font_20));
  const String state = haKnown ? prettyState(entityDomain(d.entity), he, d.decimals) : tileSub(r);
  lv_obj_set_pos(name, 72, state.length() ? 15 : 26);
  if (state.length()) {
    lv_obj_t* st = lv_label_create(c);
    lv_label_set_text(st, state.c_str());
    // Do not inherit the panel's default font: it can be much larger on P4.
    // Measure the label using the same font that LVGL actually renders.
    const lv_font_t* statusFont = &deck_font_14;
    lv_obj_set_style_text_font(st, statusFont, 0);
    lv_obj_set_style_text_color(st, lv_color_hex(C_DIM), 0);
    lv_label_set_long_mode(st, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(st, cw - 88, lv_font_get_line_height(statusFont));
    lv_obj_set_pos(st, 72, 15 + lv_font_get_line_height(&deck_font_20) + 3);
  }

  const char* verb = actionVerb(r, a);
  lv_obj_t* q = lv_label_create(c);
  lv_label_set_text_fmt(q, "%s?", verb);
  lv_obj_set_style_text_font(q, &deck_font_28, 0);
  lv_label_set_long_mode(q, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_size(q, cw - 32, lv_font_get_line_height(&deck_font_28));
  lv_obj_set_pos(q, 16, 74);

  const int bw = (cw - 16 * 2 - 10) / 2;
  const int by = ch - 16 - buttonH;
  lv_obj_t* cancel = qpButton(c, 16, by, bw, "Cancel", false);
  lv_obj_set_height(cancel, buttonH);
  lv_obj_set_style_text_font(lv_obj_get_child(cancel, 0), buttonFont, 0);
  lv_obj_add_event_cb(cancel, onModalScrim, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ok = qpButton(c, 16 + bw + 10, by, bw, verb, false);
  lv_obj_set_height(ok, buttonH);
  lv_obj_set_style_text_font(lv_obj_get_child(ok, 0), buttonFont, 0);
  lv_obj_set_style_bg_color(ok, lv_color_hex(C_DANGER), 0);
  lv_obj_set_style_text_color(ok, lv_color_white(), 0);
  lv_obj_add_event_cb(ok, onConfirmOk, LV_EVENT_CLICKED, nullptr);

  s_confirmTimer = lv_timer_create(onConfirmTimeout, CONFIRM_TIMEOUT_MS, nullptr);
  lv_timer_set_repeat_count(s_confirmTimer, 1);
  setModalOpen(true);
}

}  // namespace deckui
