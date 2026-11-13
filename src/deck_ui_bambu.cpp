// Bambu printer widgets for tactula, ported/adapted from the original
// TouchDeck LVGL printer renderer by Rafał Stolarek (Keralots).
// https://github.com/Keralots/TouchDeck/blob/v1.3.0/src/deck_ui_printer.cpp
// The layout/visual behaviours remain LVGL-native and editable in deck.json.
// Copyright for the upstream implementation remains with the original author;
// see repository LICENSE for the original MIT licence notice.
#include "deck_ui_int.h"
#include "deck_bambu.h"
#include "deck_office_bambu.h"
#include <math.h>

namespace deckui {
namespace {

constexpr uint32_t PRN_VIOLET = 0x927DFF;
constexpr uint32_t PRN_BLUE = 0x48A5FF;
constexpr uint32_t PRN_HEAT = 0xF0B56B; // actual heating warning, not a UI accent
constexpr uint32_t PRN_IDLE = 0x8795AC;
constexpr uint32_t PRN_ERROR = 0xFF929B;
constexpr uint32_t PRN_PANEL = 0x171E2C;
constexpr uint32_t PRN_INK = 0xEDF0F8;
constexpr uint32_t PRN_MUTED = 0xB8C1D2;
uint32_t s_bambuSeen = UINT32_MAX;

lv_obj_t* box(lv_obj_t* parent, lv_flex_flow_t direction) {
  lv_obj_t* p = lv_obj_create(parent);
  lv_obj_remove_style_all(p);
  lv_obj_remove_flag(p, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_flex_flow(p, direction);
  lv_obj_set_style_pad_row(p, 3, 0);
  lv_obj_set_style_pad_column(p, 5, 0);
  return p;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_opa_t opacity = LV_OPA_COVER) {
  lv_obj_t* o = lv_label_create(parent);
  lv_label_set_text(o, "");
  lv_label_set_long_mode(o, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_font(o, font, 0);
  lv_obj_set_style_text_opa(o, opacity, 0);
  return o;
}

lv_obj_t* progress(lv_obj_t* parent, int height) {
  lv_obj_t* b = lv_bar_create(parent);
  lv_obj_set_size(b, lv_pct(100), height);
  lv_bar_set_range(b, 0, 100);
  lv_bar_set_value(b, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(b, height / 2, LV_PART_MAIN);
  lv_obj_set_style_radius(b, height / 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(b, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, lv_color_hex(PRN_BLUE), LV_PART_INDICATOR);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  return b;
}

void setProgress(lv_obj_t* bar, int value, uint32_t color) {
  if (!bar) return;
  lv_bar_set_value(bar, constrain(value, 0, 100), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
}

void toggleVisible(lv_obj_t* object, bool shown) {
  if (!object) return;
  if (shown) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

void setText(lv_obj_t* object, const String& value) {
  if (object) lv_label_set_text(object, value.c_str());
}

String tempText(float now) {
  if (!isfinite(now)) return "--";
  return String(now, 0) + "°";
}

String duration(uint16_t minutes) {
  if (minutes >= 60) return String(minutes / 60) + "h " + String(minutes % 60) + "m";
  return String(minutes) + "m";
}

const PrinterTray* activeTray(const PrinterState& state) {
  if (state.activeUnit == 254) return state.extPresent ? &state.ext : nullptr;
  if (state.activeUnit >= state.amsCount || state.activeSlot >= PRN_AMS_SLOTS) return nullptr;
  const PrinterAmsUnit& unit = state.ams[state.activeUnit];
  if (!unit.present || state.activeSlot >= unit.slots) return nullptr;
  const PrinterTray& tray = unit.tray[state.activeSlot];
  return tray.present ? &tray : nullptr;
}

void lightPress(lv_event_t* event) {
  const size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
  if (index >= s_tiles.size()) return;
  PrinterState* s = new PrinterState();
  if (!s) return;
  const bool available = deck::printerRead("p1s", *s) &&
                         s->link == PRN_ONLINE && s->light >= 0;
  const bool next = available && !s->light;
  delete s;
  if (!available) {
    deckToastShow("Printer is offline", 1800);
    return;
  }
  if (!deck::officeBambuLight(next)) deckToastShow("Light command failed", 1800);
  else { hapticTap(); audioClick(); }
}

void heroAlertPress(lv_event_t* event) {
  const size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
  if (index >= s_tiles.size()) return;
  PrinterState* state = new PrinterState();
  if (!state) return;
  if (deck::printerRead("p1s", *state) && (state->hmsCount || state->printError)) {
    String message = String(state->hmsCount) + " printer alert(s)";
    if (state->err[0]) message += String(": ") + state->err;
    deckToastShow(message.c_str(), 3500);
  }
  delete state;
}

void configureTile(lv_obj_t* parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(parent, 4, 0);
}

// The hero/header, thermometers, filament spool and AMS geometry are adapted
// from upstream makePrinterContent()/fitPrinter(), not a second graphics system.
void makeHero(TileRt& r, lv_obj_t* parent) {
  configureTile(parent);
  // The blue/violet fill sits underneath the text and moves with progress.
  r.chip = box(parent, LV_FLEX_FLOW_ROW);
  lv_obj_add_flag(r.chip, (lv_obj_flag_t)(LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN));
  lv_obj_set_style_bg_color(r.chip, lv_color_hex(PRN_BLUE), 0);
  lv_obj_set_style_bg_opa(r.chip, LV_OPA_30, 0);
  lv_obj_set_style_radius(r.chip, 8, 0);
  lv_obj_move_to_index(r.chip, 0);
  // The active filament rides along the progress front, as in Keralots'
  // original printing animation. A lightweight position tick drives motion.
  r.bambuParts[6] = lv_obj_create(parent);
  lv_obj_remove_style_all(r.bambuParts[6]);
  lv_obj_add_flag(r.bambuParts[6], (lv_obj_flag_t)(LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN));
  lv_obj_set_size(r.bambuParts[6], 9, 9);
  lv_obj_set_style_radius(r.bambuParts[6], LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(r.bambuParts[6], LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(r.bambuParts[6], lv_color_hex(PRN_BLUE), 0);
  lv_obj_set_style_border_width(r.bambuParts[6], 1, 0);
  lv_obj_set_style_border_color(r.bambuParts[6], lv_color_white(), 0);
  lv_obj_set_style_shadow_width(r.bambuParts[6], 9, 0);
  lv_obj_set_style_shadow_opa(r.bambuParts[6], LV_OPA_50, 0);
  lv_obj_t* top = box(parent, LV_FLEX_FLOW_ROW);
  r.bambuParts[0] = top;
  lv_obj_set_width(top, lv_pct(100));
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  r.name = label(top, &deck_font_12);
  lv_obj_set_style_radius(r.name, 7, 0);
  lv_obj_set_style_bg_opa(r.name, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(r.name, lv_color_hex(PRN_VIOLET), 0);
  lv_obj_set_style_text_color(r.name, lv_color_white(), 0);
  lv_obj_set_style_pad_left(r.name, 6, 0);
  lv_obj_set_style_pad_right(r.name, 6, 0);
  lv_obj_set_style_pad_top(r.name, 2, 0);
  lv_obj_set_style_pad_bottom(r.name, 2, 0);
  r.sub = label(top, &deck_font_14);
  lv_obj_set_flex_grow(r.sub, 1);
  r.bambuParts[1] = label(top, &deck_font_12, LV_OPA_70); // model
  r.bambuParts[7] = label(top, &deck_font_12);
  lv_label_set_text(r.bambuParts[7], "DOOR OPEN");
  lv_obj_set_style_bg_color(r.bambuParts[7], lv_color_hex(PRN_HEAT), 0);
  lv_obj_set_style_bg_opa(r.bambuParts[7], LV_OPA_COVER, 0);
  lv_obj_set_style_radius(r.bambuParts[7], 6, 0);
  lv_obj_set_style_pad_left(r.bambuParts[7], 4, 0);
  lv_obj_set_style_pad_right(r.bambuParts[7], 4, 0);
  toggleVisible(r.bambuParts[7], false);
  lv_obj_add_event_cb(parent, heroAlertPress, LV_EVENT_SHORT_CLICKED,
                      (void*)(uintptr_t)(s_tiles.size() - 1));
  lv_obj_t* middle = box(parent, LV_FLEX_FLOW_ROW);
  r.bambuParts[2] = middle;
  lv_obj_set_width(middle, lv_pct(100));
  lv_obj_set_flex_align(middle, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_flex_grow(middle, 1);
  r.bambuParts[3] = label(middle, &deck_font_28); // time remaining
  r.bambuParts[4] = label(middle, &deck_font_12, LV_OPA_70); // ETA
  lv_obj_set_flex_grow(r.bambuParts[4], 1);
  r.bambuParts[5] = label(middle, &deck_font_20); // percent
  lv_obj_set_style_text_color(r.bambuParts[5], lv_color_hex(PRN_BLUE), 0);
  r.viz = progress(parent, 5);
}

void makeTemperature(TileRt& r, lv_obj_t* parent) {
  configureTile(parent);
  r.name = label(parent, &deck_font_12, LV_OPA_70);
  lv_label_set_text(r.name, r.def->label.c_str());
  lv_obj_t* reading = box(parent, LV_FLEX_FLOW_ROW);
  r.bambuParts[0] = reading;
  lv_obj_set_width(reading, lv_pct(100));
  lv_obj_set_flex_align(reading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_flex_grow(reading, 1);
  r.sub = label(reading, &deck_font_28);
  r.detail = label(reading, &deck_font_14, LV_OPA_70);
  r.viz = progress(parent, 3);
}

void makeFilament(TileRt& r, lv_obj_t* parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(parent, 10, 0);
  r.chip = box(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(r.chip, 42, 42);
  lv_obj_set_style_radius(r.chip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(r.chip, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(r.chip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(r.chip, lv_color_white(), 0);
  lv_obj_set_style_border_width(r.chip, 2, 0);
  lv_obj_set_style_border_opa(r.chip, LV_OPA_80, 0);
  lv_obj_t* col = box(parent, LV_FLEX_FLOW_COLUMN);
  r.bambuParts[0] = col;
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  r.name = label(col, &deck_font_12, LV_OPA_70);
  lv_obj_set_width(r.name, lv_pct(100));
  r.sub = label(col, &deck_font_20);
  lv_obj_set_width(r.sub, lv_pct(100));
  r.viz = progress(col, 4);
}

void makeLayer(TileRt& r, lv_obj_t* parent) {
  configureTile(parent);
  r.name = label(parent, &deck_font_12, LV_OPA_70);
  lv_label_set_text(r.name, "Layer");
  r.sub = label(parent, &deck_font_20);
  r.detail = label(parent, &deck_font_12, LV_OPA_60);
  lv_obj_set_flex_grow(r.name, 1);
}

void makeLight(TileRt& r, lv_obj_t* parent, size_t index) {
  configureTile(parent);
  r.chip = lv_obj_create(parent);
  lv_obj_remove_style_all(r.chip);
  lv_obj_set_size(r.chip, 29, 29);
  lv_obj_set_style_radius(r.chip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(r.chip, lv_color_hex(PRN_VIOLET), 0);
  lv_obj_set_style_bg_opa(r.chip, LV_OPA_COVER, 0);
  r.icon = label(r.chip, &deck_font_20);
  // Match the original TouchDeck chamber-light icon when the MDI pack is
  // available. Keep an LVGL-only fallback for installations without MDI.
  const uint32_t bulb = iconCodepoint("mdi:lightbulb");
  const lv_font_t* bulbFont = bulb ? iconFont(18) : nullptr;
  if (bulbFont) {
    char glyph[5];
    iconUtf8(bulb, glyph);
    lv_obj_set_style_text_font(r.icon, bulbFont, 0);
    lv_label_set_text(r.icon, glyph);
  } else lv_label_set_text(r.icon, LV_SYMBOL_CHARGE);
  lv_obj_set_style_text_color(r.icon, lv_color_hex(PRN_INK), 0);
  lv_obj_center(r.icon);
  r.name = label(parent, &deck_font_14);
  lv_label_set_text(r.name, "Light");
  r.sub = label(parent, &deck_font_12, LV_OPA_70);
  lv_obj_add_event_cb(parent, lightPress, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)index);
}

void slotSet(lv_obj_t* slot, const PrinterTray* tray, bool selected, int slotNumber) {
  if (!slot) return;
  lv_obj_t* txt = lv_obj_get_child(slot, 0);
  const bool present = tray && tray->present;
  const uint32_t colour = present ? tray->rgb & 0xFFFFFF : 0x303748;
  lv_obj_set_style_bg_color(slot, lv_color_hex(colour), 0);
  lv_obj_set_style_bg_opa(slot, present ? LV_OPA_COVER : LV_OPA_50, 0);
  lv_obj_set_style_border_width(slot, selected ? 2 : 1, 0);
  lv_obj_set_style_border_color(slot, lv_color_hex(selected ? PRN_VIOLET : 0x535D75), 0);
  const uint32_t brightness = ((colour >> 16) & 255) * 299 +
                              ((colour >> 8) & 255) * 587 +
                              (colour & 255) * 114;
  lv_obj_set_style_text_color(txt,
      lv_color_hex(present && brightness > 150000 ? 0x131725 : 0xFFFFFF), 0);
  const String text = String(slotNumber) + " " +
    (present ? (tray->type[0] ? String(tray->type) : "Loaded") : "Empty");
  lv_label_set_text(txt, text.c_str());
}

void makeAms(TileRt& r, lv_obj_t* parent, bool slots) {
  configureTile(parent);
  r.name = label(parent, &deck_font_14);
  lv_label_set_text(r.name, r.def->label.c_str());
  r.sub = label(parent, &deck_font_20);
  r.detail = label(parent, &deck_font_12, LV_OPA_70);
  if (!slots) return;
  lv_obj_set_style_text_font(r.sub, &deck_font_14, 0);
  r.rows = box(parent, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_width(r.rows, lv_pct(100));
  lv_obj_set_flex_grow(r.rows, 1);
  for (int i = 0; i < 8; i++) {
    lv_obj_t* cell = box(r.rows, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_left(cell, 4, 0);
    lv_obj_set_style_pad_right(cell, 4, 0);
    lv_obj_set_style_pad_top(cell, 3, 0);
    lv_obj_set_style_pad_bottom(cell, 3, 0);
    lv_obj_set_style_radius(cell, 6, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_t* txt = label(cell, &deck_font_12);
    lv_obj_set_width(txt, lv_pct(100));
  }
}
}  // namespace

void makeBambu(TileRt& r, lv_obj_t* parent, size_t index) {
  const String& kind = r.def->sub;
  if (kind == "hero") makeHero(r, parent);
  else if (kind == "nozzle_left" || kind == "nozzle_right" ||
           kind == "bed" || kind == "chamber" || kind == "temperatures")
    makeTemperature(r, parent);
  else if (kind == "filament") makeFilament(r, parent);
  else if (kind == "layer") makeLayer(r, parent);
  else if (kind == "light") makeLight(r, parent, index);
  else makeAms(r, parent, kind == "slots");
  if (kind != "light") lv_obj_remove_flag(parent, LV_OBJ_FLAG_CLICKABLE);
  // New tiles need an initial snapshot even if printer state has not changed.
  // Rebuilding pages otherwise leaves them at their placeholder values.
  s_bambuSeen = UINT32_MAX;
}

void fitBambu(TileRt& r, int cw, int ch) {
  if (cw <= 0 || ch <= 0) return;
  const String& kind = r.def->sub;
  if (kind == "hero") {
    // The original renderer hides ETA / printer name on narrow layouts.
    toggleVisible(r.bambuParts[1], cw >= 185);
    toggleVisible(r.bambuParts[4], cw >= 265);
    if (cw < 325) toggleVisible(r.bambuParts[7], false);
    lv_obj_set_style_text_font(r.bambuParts[3],
                              ch >= 72 ? &deck_font_28 : &deck_font_20, 0);
    lv_obj_set_style_text_font(r.bambuParts[5],
                              ch >= 66 ? &deck_font_20 : &deck_font_14, 0);
  } else if (kind == "nozzle_left" || kind == "nozzle_right" ||
             kind == "bed" || kind == "chamber" || kind == "temperatures") {
    const bool compact = ch < 50;
    lv_obj_set_style_text_font(r.sub, ch >= 67 && cw >= 88 ?
                              &deck_font_28 : &deck_font_20, 0);
    toggleVisible(r.detail, cw >= 75 && !compact);
    if (compact) {
      lv_obj_set_flex_flow(r.obj, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(r.obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(r.obj, 4, 0);
      lv_obj_set_width(r.name, LV_SIZE_CONTENT);
      lv_obj_set_flex_grow(r.bambuParts[0], 1);
      lv_obj_add_flag(r.viz, LV_OBJ_FLAG_FLOATING);
      lv_obj_align(r.viz, LV_ALIGN_BOTTOM_MID, 0, 0);
    } else {
      lv_obj_set_flex_flow(r.obj, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_row(r.obj, 3, 0);
      lv_obj_set_width(r.name, lv_pct(100));
      lv_obj_set_width(r.bambuParts[0], lv_pct(100));
      lv_obj_remove_flag(r.viz, LV_OBJ_FLAG_FLOATING);
    }
  } else if (kind == "filament") {
    const int diameter = LV_MIN(44, ch - 7);
    lv_obj_set_size(r.chip, LV_MAX(12, diameter), LV_MAX(12, diameter));
    lv_obj_set_style_text_font(r.sub, ch >= 62 ? &deck_font_20 : &deck_font_14, 0);
    toggleVisible(r.name, ch >= 48);
  } else if (kind == "layer") {
    lv_obj_set_style_text_font(r.sub, ch >= 58 ? &deck_font_20 : &deck_font_14, 0);
    toggleVisible(r.name, ch >= 33);
    toggleVisible(r.detail, ch >= 40);
  } else if (kind == "light") {
    toggleVisible(r.sub, ch >= 62);
  } else if (kind == "slots" && r.rows) {
    const int columns = cw >= 220 ? 4 : 2;
    const int slotWidth = LV_MAX(26, (cw - (columns - 1) * 5) / columns);
    const int slotHeight = LV_MIN(45, LV_MAX(24, (ch - 42) / (8 / columns) - 4));
    for (int i = 0; i < 8; ++i) {
      lv_obj_t* cell = lv_obj_get_child(r.rows, i);
      lv_obj_set_size(cell, slotWidth, slotHeight);
    }
  }
}

void applyBambu(TileRt& r) {
  const String& kind = r.def->sub;
  // A PrinterState can contain multiple AMS units. Heap allocation avoids
  // exhausting the ESP32's task stack while applying the tile.
  PrinterState* state = new PrinterState();
  if (!state) return;
  const bool have = deck::officeBambuEnabled() &&
                    deck::printerRead("p1s", *state);
  const bool online = have && state->link == PRN_ONLINE;

  if (kind == "hero") {
    const String status = !online ? "CONNECTING" :
      !strcmp(state->gcodeState, "RUNNING") ? "PRINTING" :
      !strcmp(state->gcodeState, "PAUSE") ? "PAUSED" :
      !strcmp(state->gcodeState, "FINISH") ? "DONE" :
      !strcmp(state->gcodeState, "FAILED") ? "FAILED" : "READY";
    const bool hasError = online && (state->printError || state->hmsCount);
    const uint32_t statusColor = hasError ? PRN_ERROR :
      status == "PRINTING" ? PRN_VIOLET :
      status == "PAUSED" ? PRN_HEAT :
      status == "FAILED" ? PRN_ERROR :
      status == "DONE" ? PRN_BLUE : PRN_IDLE;
    setText(r.name, hasError ? "ERROR" : status);
    lv_obj_set_style_bg_color(r.name, lv_color_hex(statusColor), 0);
    setText(r.sub, online ? (state->job[0] ? String(state->job) : "Printer ready") : "Waiting for printer");
    toggleVisible(r.bambuParts[7], online && state->door == 1 && r.fitW >= 325);
    if (online && hasError && state->err[0]) setText(r.sub, String(state->err));
    setText(r.bambuParts[1], "P1S");
    const bool active = online && (status == "PRINTING" || status == "PAUSED");
    setText(r.bambuParts[3], active ? duration(state->remainingMin) :
      online && status == "DONE" ? "Finished" : "--");
    String eta;
    if (active) {
      const time_t now = time(nullptr);
      if (now > 1609459200) {
        const time_t ending = now + time_t(state->remainingMin) * 60;
        struct tm local;
        localtime_r(&ending, &local);
        char tm[32];
        strftime(tm, sizeof(tm), "%H:%M", &local);
        eta = String("ETA ") + tm;
      }
    }
    setText(r.bambuParts[4], eta);
    setText(r.bambuParts[5], active || status == "DONE" ?
      String(status == "DONE" ? 100 : state->progress) + "%" : "");
    setProgress(r.viz, active ? state->progress : status == "DONE" ? 100 : 0, statusColor);
    const int fill = active ? state->progress : status == "DONE" ? 100 : 0;
    toggleVisible(r.chip, fill > 0);
    r.frac = (float)fill / 100.0f;
    r.on = active && fill > 0;
    toggleVisible(r.bambuParts[6], r.on);
    if (r.on) {
      const PrinterTray* spool = activeTray(*state);
      const uint32_t rgb = spool ? spool->rgb & 0xFFFFFF : PRN_BLUE;
      lv_obj_set_style_bg_color(r.bambuParts[6], lv_color_hex(rgb), 0);
      lv_obj_set_style_shadow_color(r.bambuParts[6], lv_color_hex(rgb), 0);
    }
    if (fill) {
      lv_obj_set_pos(r.chip, 0, 0);
      lv_obj_set_size(r.chip, lv_pct(fill), lv_pct(100));
      lv_obj_set_style_bg_color(r.chip, lv_color_hex(statusColor), 0);
    }
    lv_obj_set_style_text_color(r.bambuParts[5], lv_color_hex(statusColor), 0);
  } else if (kind == "nozzle_left" || kind == "nozzle_right" ||
             kind == "bed" || kind == "chamber" || kind == "temperatures") {
    float now = NAN, target = NAN, maximum = 300;
    const char* caption = r.def->label.c_str();
    if (kind == "nozzle_left" || kind == "nozzle_right") {
      const int nozzle = state->dualNozzle ?
         (kind == "nozzle_left" ? 1 : 0) : 0;
      if (!state->dualNozzle && kind == "nozzle_right") {
        caption = "No second nozzle";
      } else {
        now = state->nozzle[nozzle]; target = state->nozzleTarget[nozzle];
        if (!state->dualNozzle) caption = "Nozzle";
      }
    } else if (kind == "bed") {
      now = state->bed; target = state->bedTarget; maximum = 115;
    } else if (kind == "chamber") {
      now = state->chamber; maximum = 70;
    } else {
      now = state->nozzle[0]; target = state->nozzleTarget[0];
    }
    const bool heating = online && isfinite(target) && target > 0 && isfinite(now);
    const uint32_t colour = heating ? PRN_HEAT : PRN_INK;
    setText(r.name, caption);
    setText(r.sub, online ? tempText(now) : "--");
    setText(r.detail, heating ? String("/") + String(target, 0) : "");
    lv_obj_set_style_text_color(r.sub, lv_color_hex(colour), 0);
    setProgress(r.viz, online && isfinite(now) ?
      (int)(100 * now / (heating ? target : maximum)) : 0,
      heating ? PRN_HEAT : PRN_BLUE);
  } else if (kind == "filament") {
    const PrinterTray* tray = online ? activeTray(*state) : nullptr;
    if (tray) {
      const uint32_t rgb = tray->rgb & 0xFFFFFF;
      lv_obj_set_style_bg_color(r.chip, lv_color_hex(rgb), 0);
      lv_obj_set_style_bg_opa(r.chip, tray->alpha < 128 ? LV_OPA_50 : LV_OPA_COVER, 0);
      const String place = state->activeUnit == 254 ? "External spool" :
        "AMS " + String(state->activeUnit + 1) + ", slot " + String(state->activeSlot + 1);
      setText(r.name, place);
      setText(r.sub, tray->type[0] ? String(tray->type) : "Filament");
      toggleVisible(r.viz, tray->remain >= 0);
      setProgress(r.viz, tray->remain >= 0 ? tray->remain : 0, rgb);
    } else {
      lv_obj_set_style_bg_color(r.chip, lv_color_white(), 0);
      lv_obj_set_style_bg_opa(r.chip, LV_OPA_20, 0);
      setText(r.name, online ? String(state->amsCount) + " AMS unit(s)" : "");
      setText(r.sub, online ? "Not feeding" : "--");
      toggleVisible(r.viz, false);
    }
  } else if (kind == "layer") {
    setText(r.sub, online && state->totalLayers ? String(state->layer) : "--");
    setText(r.detail, online && state->totalLayers ?
      String("of ") + String(state->totalLayers) : "");
  } else if (kind == "light") {
    const bool lit = online && state->light == 1;
    const uint32_t colour = lit ? PRN_HEAT : PRN_VIOLET;
    lv_obj_set_style_bg_color(r.chip, lv_color_hex(colour), 0);
    setText(r.sub, !online ? "Offline" : state->light < 0 ? "Unavailable" :
      lit ? "On" : "Off");
  } else if (kind == "ams" || kind == "slots") {
    setText(r.sub, online ? String(state->amsCount) + " AMS unit(s)" : "Connecting");
    String info;
    if (online && state->amsCount) {
      const PrinterAmsUnit& unit = state->ams[0];
      unsigned loaded = 0;
      for (unsigned i = 0; i < unit.slots && i < PRN_AMS_SLOTS; ++i)
        if (unit.tray[i].present) ++loaded;
      info = String(loaded) + " slots loaded";
      if (unit.humidity <= 100) info += " · " + String(unit.humidity) + "% RH";
    }
    setText(r.detail, info);
    if (kind == "slots" && r.rows) {
      unsigned count = 0;
      for (unsigned u = 0; online && u < state->amsCount && count < 8; ++u) {
        const PrinterAmsUnit& unit = state->ams[u];
        if (!unit.present) continue;
        for (unsigned i = 0; i < unit.slots && i < PRN_AMS_SLOTS && count < 8; ++i) {
          lv_obj_t* cell = lv_obj_get_child(r.rows, count++);
          slotSet(cell, &unit.tray[i],
                  state->activeUnit == u && state->activeSlot == i,
                  (int)(u * 4 + i + 1));
          toggleVisible(cell, true);
        }
      }
      for (; count < 8; ++count)
        toggleVisible(lv_obj_get_child(r.rows, count), false);
    }
  }
  delete state;
  markPageStale(r.page);
}

void bambuSync() {
  const uint32_t version = deck::printersVersion();
  if (s_bambuSeen != version) {
    s_bambuSeen = version;
    // Use the real HA printer name, then a manually configured name, then
    // the normal editable page name. No layout JSON is modified.
    PrinterState* printer = new PrinterState();
    String sensorName;
    if (printer) {
      if (deck::printerRead("p1s", *printer) && printer->friendlyName[0])
        sensorName = printer->friendlyName;
      delete printer;
    }
    for (int i = 0; i < s_count; ++i) {
      Page& page = s_pages[i];
      if (!page.def || page.def->id != "office_printer" || !page.title) continue;
      const String manualName = deck::officeBambuDisplayName();
      const String lanName = deck::officeBambuLiveName();
      const String title = sensorName.length() ? sensorName :
          lanName.length() ? lanName :
          manualName.length() ? manualName :
          page.def->name.length() ? page.def->name : page.def->id;
      if (strcmp(lv_label_get_text(page.title), title.c_str())) {
        lv_label_set_text(page.title, title.c_str());
        page.stale = true;
      }
    }
    for (TileRt& tile : s_tiles)
      if (tile.def && tile.def->type == TILE_BAMBU) applyBambu(tile);
  }
  // LAN names can arrive independently of printer telemetry. Refresh only the
  // header (not the whole page) when that cached HA name becomes available.
  static uint32_t lastNameCheck = 0;
  if (millis() - lastNameCheck >= 1000) {
    lastNameCheck = millis();
    const String lanName = deck::officeBambuLiveName();
    if (lanName.length()) {
      for (int i = 0; i < s_count; ++i) {
        Page& page = s_pages[i];
        if (!page.def || page.def->id != "office_printer" || !page.title) continue;
        if (strcmp(lv_label_get_text(page.title), lanName.c_str())) {
          lv_label_set_text(page.title, lanName.c_str());
          page.stale = true;
        }
      }
    }
  }
  // ~12 fps is enough for a subtle moving filament head. No allocations and
  // no forced full-page repaint between actual printer state changes.
  static uint32_t lastFrame = 0;
  if (millis() - lastFrame < 85) return;
  lastFrame = millis();
  for (TileRt& tile : s_tiles) {
    if (!tile.def || tile.def->type != TILE_BAMBU || tile.def->sub != "hero" ||
        !tile.on || !tile.bambuParts[6]) continue;
    const int width = lv_obj_get_content_width(tile.obj);
    const int height = lv_obj_get_content_height(tile.obj);
    const int xpos = constrain((int)(width * tile.frac) - 4, 0, LV_MAX(0, width - 9));
    const int travel = LV_MAX(1, height - 11);
    const uint32_t phase = (millis() / 12) % (uint32_t)(travel * 2);
    const int ypos = (int)(phase < (uint32_t)travel ? phase : travel * 2 - phase);
    lv_obj_set_pos(tile.bambuParts[6], xpos, ypos);
  }
}
} // namespace deckui
