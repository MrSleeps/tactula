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
#include <atomic>

namespace deckui {
void cinematicSurface(lv_obj_t* obj, int palette);
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

// One shared camera frame for all hero tiles. Only the UI task owns LVGL
// objects and descriptors. The worker publishes a finished RGB565 buffer.
struct CameraJob {
  String entity;
  int w = 0, h = 0;
  uint16_t* pixels = nullptr;
};
CameraJob s_cameraJob;
std::atomic<bool> s_cameraBusy{false}, s_cameraDone{false};
lv_image_dsc_t s_cameraImage{};
uint16_t* s_cameraPixels = nullptr;
String s_cameraEntity;
uint32_t s_cameraLast = 0;
String s_cameraUpdated;
int s_cameraW = 0, s_cameraH = 0;

void cameraWorker() {
  uint8_t* jpeg = nullptr;
  size_t bytes = 0;
  if (haCameraSnapshot(s_cameraJob.entity, jpeg, bytes)) {
    auto* pixels = (uint16_t*)heap_caps_malloc(
        (size_t)s_cameraJob.w * s_cameraJob.h * 2, MALLOC_CAP_SPIRAM);
    if (pixels && wallDecodeCamera(jpeg, bytes, s_cameraJob.w, s_cameraJob.h, pixels))
      s_cameraJob.pixels = pixels;
    else free(pixels);
    free(jpeg);
  }
  s_cameraDone.store(true, std::memory_order_release);
}


// Bambu Dashboard widgets share geometry, but must adopt each theme's
// surfaces, text and border colours. Classic remains an explicit opt-out.
constexpr uint32_t BAMBU_CARD_BLUE = 0x4F8CFF;
bool dashboardBambu(const TileDef& d) { return d.bambuLook != 1; }
bool cinematicBambu(const TileDef& d) { return dashboardBambu(d) && themeUsesModernLayout(s_theme); }
uint32_t dashboardAccent(const TileDef& d) {
  return d.hasColor ? d.color : modernColour(BAMBU_CARD_BLUE);
}
void styleBambuCard(TileRt& r) {
  if (!dashboardBambu(*r.def)) return;
  lv_obj_t* parent = r.obj;
  const String& kind = r.def->sub;
  const uint32_t accent = dashboardAccent(*r.def);
  const uint32_t foreground = C_TEXT;
  const uint32_t muted = C_DIM;
  const uint32_t border = C_TILE_LINE;
  // Glass has already baked the wallpaper blur/tint and rounded tile edge
  // into the page background. Never paint an opaque surface over that bake:
  // the tile's initial glassStyle() must remain in control of its opacity.
  if (s_theme != THEME_GLASS && !themeUsesModernLayout(s_theme)) {
    lv_obj_set_style_radius(parent, 14, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(C_TILE), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 1, 0);
    lv_obj_set_style_border_color(parent, lv_color_hex(border), 0);
  }
  lv_obj_set_style_pad_all(parent, 9, 0);
  if (cinematicBambu(*r.def)) {
    // Preserve the shared Glass edge instead of replacing it with the opaque palette border.
    if (!themeUsesGlassMode(s_theme))
      lv_obj_set_style_border_color(parent, lv_color_hex(modernColour(0x365471)), 0);
    lv_obj_set_style_text_color(parent, lv_color_hex(0xEDF5FF), 0);
  }
  lv_obj_set_style_text_color(parent, lv_color_hex(foreground), 0);
  if (r.viz) {
    lv_obj_set_style_bg_color(r.viz, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r.viz, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(r.viz, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_radius(r.viz, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(r.viz, 4, LV_PART_INDICATOR);
  }
  if (kind == "hero" && cinematicBambu(*r.def)) return;
  if (kind == "hero") {
    // Status badge, job text, remaining-time and progress are still driven
    // by the original printer data and all error/status state colours.
    lv_obj_set_style_radius(r.name, 9, 0);
    lv_obj_set_style_pad_hor(r.name, 9, 0);
    lv_obj_set_style_pad_ver(r.name, 4, 0);
    lv_obj_set_style_bg_color(r.chip, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(r.chip, LV_OPA_20, 0);
    lv_obj_set_style_text_color(r.sub, lv_color_hex(foreground), 0);
    lv_obj_set_style_text_color(r.bambuParts[4], lv_color_hex(muted), 0);
  } else if (kind == "light") {
    lv_obj_set_style_radius(r.chip, 10, 0);
    lv_obj_set_style_border_width(r.chip, 1, 0);
    lv_obj_set_style_border_color(r.chip, lv_color_hex(border), 0);
    lv_obj_set_style_text_color(r.icon, lv_color_hex(foreground), 0);
    lv_obj_set_style_text_color(r.sub, lv_color_hex(muted), 0);
  } else if (kind == "filament") {
    // Preserve the real filament colour, including transparent/matte trays.
    lv_obj_set_style_radius(r.chip, cinematicBambu(*r.def) ? LV_RADIUS_CIRCLE : 12, 0);
    lv_obj_set_style_border_color(r.chip, lv_color_hex(border), 0);
    lv_obj_set_style_text_color(r.name, lv_color_hex(muted), 0);
  } else {
    if (r.name) lv_obj_set_style_text_color(r.name, lv_color_hex(muted), 0);
    if (r.detail) lv_obj_set_style_text_color(r.detail, lv_color_hex(muted), 0);
  }
  if (kind == "slots" && r.rows) {
    for (int i = 0; i < 8; ++i) {
      lv_obj_t* cell = lv_obj_get_child(r.rows, i);
      if (!cell) continue;
      lv_obj_set_style_radius(cell, 9, 0);
    }
  }
}


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
  if (themeUsesModernLayout(s_theme)) {
    lv_obj_set_style_bg_grad_color(bar, lv_color_mix(lv_color_white(), lv_color_hex(color), 65), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  }
  lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
}

void toggleVisible(lv_obj_t* object, bool shown) {
  if (!object) return;
  if (shown) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

void setText(lv_obj_t* object, const String& value) {
  if (object && strcmp(lv_label_get_text(object), value.c_str())) lv_label_set_text(object, value.c_str());
}

String tempText(float now) {
  if (!isfinite(now)) return "--";
  return String(now, 0) + (themeUsesModernLayout(s_theme) ? "°C" : "°");
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
  if (cinematicBambu(*r.def)) {
    // Absolute geometry is fitted to each tile, with a compact text-only fallback.
    lv_obj_set_layout(parent, LV_LAYOUT_NONE);
    r.chip = lv_obj_create(parent);
    lv_obj_remove_style_all(r.chip);
    lv_obj_remove_flag(r.chip, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_radius(r.chip, 15, 0);
    lv_obj_set_style_bg_color(r.chip, lv_color_hex(modernColour(0x0D192A)), 0);
    lv_obj_set_style_bg_opa(r.chip, LV_OPA_COVER, 0);
    r.icon = label(r.chip, &deck_font_28);
    lv_obj_set_style_text_color(r.icon, lv_color_hex(modernColour(0x88ABC9)), 0);
    const auto glyph = iconCodepoint("mdi:printer-3d");
    const auto* font = glyph ? iconFont(64) : nullptr;
    if (font) {
      char utf[5]; iconUtf8(glyph, utf);
      lv_obj_set_style_text_font(r.icon, font, 0);
      lv_label_set_text(r.icon, utf);
    } else lv_label_set_text(r.icon, "P1S");
    lv_obj_center(r.icon);
    auto* camera = lv_image_create(r.chip);
    lv_obj_remove_flag(camera, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(camera, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(camera);
    // Background image beneath both the progress tint and filament head.
    r.detail = camera;
    r.name = label(parent, &deck_font_20);
    r.sub = label(parent, &deck_font_20);
    r.bambuParts[0] = label(parent, &deck_font_14);
    lv_label_set_text(r.bambuParts[0], "Time remaining");
    r.bambuParts[2] = label(parent, &deck_font_14);
    lv_label_set_text(r.bambuParts[2], "Current layer");
    r.bambuParts[1] = label(parent, &deck_font_14);
    lv_label_set_text(r.bambuParts[1], "Est. finish");
    lv_label_set_long_mode(r.sub, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    r.bambuParts[3] = label(parent, &deck_font_28);
    r.bambuParts[4] = label(parent, &deck_font_14);
    r.bambuParts[5] = label(parent, &lv_font_montserrat_40);
    r.bambuParts[6] = label(parent, &deck_font_20); // current layer, not an animation
    r.bambuParts[7] = label(parent, &deck_font_12);
    lv_label_set_text(r.bambuParts[7], "DOOR OPEN");
    lv_obj_set_style_text_color(r.bambuParts[7], lv_color_hex(PRN_HEAT), 0);
    r.viz = progress(parent, 8);
    // Restore the original moving filament head and progress tint, but
    // constrain both to the illustration area instead of repainting the text.
    auto* fill = lv_obj_create(r.chip);
    lv_obj_remove_style_all(fill);
    lv_obj_remove_flag(fill, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_bg_color(fill, lv_color_hex(modernColour(0x438ACB)), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_30, 0);
    lv_obj_set_style_radius(fill, 12, 0);
    lv_obj_move_to_index(camera, 0);
    lv_obj_move_to_index(fill, 1);
    auto* head = lv_obj_create(r.chip);
    lv_obj_remove_style_all(head);
    lv_obj_remove_flag(head, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_size(head, 10, 10);
    lv_obj_set_style_radius(head, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_set_style_border_color(head, lv_color_white(), 0);
    toggleVisible(fill, false);
    toggleVisible(head, false);
    // Append after the animation head: existing child indices stay unchanged.
    auto* stamp = label(r.chip, &deck_font_12);
    lv_obj_set_style_text_color(stamp, lv_color_white(), 0);
    lv_obj_set_style_bg_color(stamp, lv_color_hex(0x071019), 0);
    lv_obj_set_style_bg_opa(stamp, LV_OPA_80, 0);
    lv_obj_set_style_radius(stamp, 5, 0);
    lv_obj_set_style_pad_hor(stamp, 5, 0);
    lv_obj_set_style_pad_ver(stamp, 3, 0);
    lv_obj_align(stamp, LV_ALIGN_BOTTOM_MID, 0, -5);
    toggleVisible(stamp, false);
    lv_obj_add_event_cb(parent, heroAlertPress, LV_EVENT_SHORT_CLICKED,
                        (void*)(uintptr_t)(s_tiles.size() - 1));
    return;
  }
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
  // A flex row without an explicit content height can retain LVGL's default
  // 50px object height and push the remaining-time row out of the short tile.
  // Match upstream's sizing without changing the approved visual layout.
  lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
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
  lv_obj_set_size(middle, lv_pct(100), LV_SIZE_CONTENT);
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

// Five filament windows in a dark front-facing reel. Native LVGL arcs
// avoid rotating bitmaps or allocating a new canvas on every animation tick.
void reelBuild(lv_obj_t* reel) {
  lv_obj_set_layout(reel, LV_LAYOUT_NONE);
  lv_obj_set_style_pad_all(reel, 0, 0);
  lv_obj_set_style_radius(reel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(reel, lv_color_hex(0x202630), 0);
  lv_obj_set_style_bg_opa(reel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(reel, 1, 0);
  lv_obj_set_style_border_color(reel, lv_color_hex(0x748293), 0);
  for (int i = 0; i < 5; ++i) {
    auto* arc = lv_arc_create(reel);
    lv_obj_remove_style_all(arc);
    lv_obj_remove_flag(arc, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_ADV_HITTEST));
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x536478), LV_PART_INDICATOR);
    // Flat radial ends leave a visible graphite spoke between windows.
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 12, 60);
    lv_arc_set_rotation(arc, i * 72);
    lv_obj_center(arc);
  }
  auto* hub = lv_obj_create(reel);
  lv_obj_remove_style_all(hub);
  lv_obj_remove_flag(hub, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(hub, lv_color_hex(modernColour(0x0B1727)), 0);
  lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(hub, lv_color_hex(0x536070), 0);
  lv_obj_set_style_border_width(hub, 1, 0);
  lv_obj_center(hub);
}
void reelSize(lv_obj_t* reel, int diameter) {
  diameter = LV_MAX(12, diameter);
  lv_obj_set_size(reel, diameter, diameter);
  const int outer = LV_MAX(8, diameter * 80 / 100);
  for (int i = 0; i < 5; ++i) {
    auto* arc = lv_obj_get_child(reel, i);
    lv_obj_set_size(arc, outer, outer);
    lv_obj_set_style_arc_width(arc, LV_MAX(2, diameter * 19 / 100), LV_PART_INDICATOR);
    lv_obj_center(arc);
  }
  auto* hub = lv_obj_get_child(reel, 5);
  lv_obj_set_size(hub, LV_MAX(4, diameter * 30 / 100), LV_MAX(4, diameter * 28 / 100));
  lv_obj_center(hub);
}
void reelState(lv_obj_t* reel, const PrinterTray* tray, bool spinning) {
  const bool loaded = tray && tray->present;
  const uint32_t rgb = loaded ? tray->rgb & 0xFFFFFF : 0x536478;
  for (int i = 0; i < 5; ++i) {
    auto* arc = lv_obj_get_child(reel, i);
    lv_obj_set_style_arc_color(arc, lv_color_hex(rgb), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, loaded ? (tray->alpha < 128 ? LV_OPA_60 : LV_OPA_COVER) : LV_OPA_30, LV_PART_INDICATOR);
  }
  if (loaded && spinning) lv_obj_add_flag(reel, LV_OBJ_FLAG_USER_1);
  else lv_obj_remove_flag(reel, LV_OBJ_FLAG_USER_1);
}
void reelAnimate(lv_obj_t* reel, int angle) {
  if (!reel || !lv_obj_has_flag(reel, LV_OBJ_FLAG_USER_1) || !lv_obj_is_visible(reel)) return;
  for (int i = 0; i < 5; ++i)
    lv_arc_set_rotation(lv_obj_get_child(reel, i), (angle + i * 72) % 360);
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
  if (cinematicBambu(*r.def)) { reelBuild(r.chip); reelSize(r.chip, 42); }
  lv_obj_t* col = box(parent, LV_FLEX_FLOW_COLUMN);
  r.bambuParts[0] = col;
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  r.name = label(col, &deck_font_12, LV_OPA_70);
  lv_obj_set_width(r.name, lv_pct(100));
  r.sub = label(col, &deck_font_20);
  lv_obj_set_width(r.sub, lv_pct(100));
  r.viz = progress(col, 4);
  if (!dashboardBambu(*r.def)) return;
  // Upcoming job filament colours, if present in the Bambu mapping.
  // These small swatches don't alter the existing spool / label area.
  for (int i = 1; i <= 3; ++i) {
    lv_obj_t* swatch = lv_obj_create(parent);
    lv_obj_remove_style_all(swatch);
    lv_obj_set_size(swatch, 9, 34);
    lv_obj_set_style_radius(swatch, 4, 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(swatch, 1, 0);
    lv_obj_set_style_border_color(swatch, lv_color_hex(C_TILE_LINE), 0);
    lv_obj_add_flag(swatch, LV_OBJ_FLAG_HIDDEN);
    r.bambuParts[i] = swatch;
  }
}

void makeSummary(TileRt& r, lv_obj_t* parent) {
  configureTile(parent);
  r.name = label(parent, &deck_font_12, LV_OPA_70);
  r.sub = label(parent, &deck_font_20);
  r.detail = label(parent, &deck_font_12, LV_OPA_70);
  r.viz = progress(parent, 4);
}

void makeLayer(TileRt& r, lv_obj_t* parent) {
  configureTile(parent);
  r.name = label(parent, &deck_font_12, LV_OPA_70);
  lv_label_set_text(r.name, "Layer");
  if (cinematicBambu(*r.def)) {
    auto* reading = box(parent, LV_FLEX_FLOW_ROW);
    r.bambuParts[0] = reading;
    lv_obj_set_width(reading, lv_pct(100));
    lv_obj_set_flex_align(reading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    r.sub = label(reading, &deck_font_20);
    r.detail = label(reading, &deck_font_12, LV_OPA_60);
    r.viz = progress(parent, 7);
  } else {
    r.sub = label(parent, &deck_font_20);
    r.detail = label(parent, &deck_font_12, LV_OPA_60);
    lv_obj_set_flex_grow(r.name, 1);
  }
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

void amsDetailSlot(lv_obj_t* cell, const PrinterTray* tray, bool feeding, int slotNumber, bool printing) {
  if (!cell) return;
  const bool present = tray && tray->present;
  const uint32_t rgb = present ? tray->rgb & 0xFFFFFF : C_TRACK;
  lv_obj_set_style_bg_color(cell, lv_color_hex(rgb), 0);
  lv_obj_set_style_bg_opa(cell, present ? LV_OPA_30 : LV_OPA_20, 0);
  lv_obj_set_style_border_width(cell, feeding ? 2 : 1, 0);
  lv_obj_set_style_border_color(cell, lv_color_hex(feeding ? BAMBU_CARD_BLUE : C_TILE_LINE), 0);
  const bool spoolCard = lv_obj_get_child_count(cell) >= 3;
  lv_obj_t* material = lv_obj_get_child(cell, spoolCard ? 1 : 0);
  lv_obj_t* remaining = lv_obj_get_child(cell, spoolCard ? 2 : 1);
  if (!material || !remaining) return;
  lv_label_set_text(material, present ? (tray->type[0] ? tray->type : "Loaded") : "Empty");
  char amount[12];
  if (!present) amount[0] = '\0';
  else if (tray->remain < 0) snprintf(amount, sizeof(amount), "?");
  else snprintf(amount, sizeof(amount), "%d%%", tray->remain);
  lv_label_set_text(remaining, amount);
  if (lv_obj_get_child_count(cell) >= 3) {
    auto* spool = lv_obj_get_child(cell, 0);
    reelState(spool, tray, feeding && printing);
    lv_obj_set_style_bg_color(cell, lv_color_hex(feeding ? modernColour(0x294C6D) : modernColour(0x14273C)), 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cell, lv_color_hex(feeding ? modernColour(0x76B9FF) : modernColour(0x37536C)), 0);
    setText(material, String("A") + String(slotNumber) + " · " +
      (present ? (tray->type[0] ? String(tray->type) : "Loaded") : "Empty"));
    setText(remaining, feeding ? "In use" : present ? (tray->remain >= 0 ? String(amount) : String("Ready")) : "");
    lv_obj_set_style_text_color(remaining, lv_color_hex(feeding ? modernColour(0x85C4FF) : C_DIM), 0);
    lv_obj_set_style_text_color(material, lv_color_hex(C_TEXT), 0);
    return;
  }
  lv_obj_set_style_text_color(material, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_text_color(remaining,
     lv_color_hex(present && tray->remain >= 0 && tray->remain <= 10
                     ? (s_theme == THEME_DAYLIGHT ? 0xBC303B : PRN_ERROR) : C_DIM), 0);
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
  const bool header = !slots && dashboardBambu(*r.def);
  lv_obj_t* heading = parent;
  if (header) {
    heading = box(parent, LV_FLEX_FLOW_ROW);
    r.bambuParts[0] = heading;
    lv_obj_set_width(heading, lv_pct(100));
    lv_obj_set_height(heading, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(heading, 6, 0);
  }
  r.name = label(heading, &deck_font_14);
  lv_label_set_text(r.name, r.def->label.c_str());
  r.sub = label(heading, &deck_font_20);
  if (header) {
    lv_obj_set_flex_grow(r.name, 0);
    lv_obj_set_width(r.name, LV_SIZE_CONTENT);
    lv_obj_set_width(r.sub, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(r.sub, LV_TEXT_ALIGN_LEFT, 0);
  }
  r.detail = label(parent, &deck_font_12, LV_OPA_70);
  if (!slots) {
    if (!dashboardBambu(*r.def)) return;
    // Single AMS unit: compact filament cards with material and remaining %.
    // Existing summary text remains visible in small tiles.
    r.rows = box(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(r.rows, lv_pct(100));
    lv_obj_set_flex_grow(r.rows, 1);
    lv_obj_set_style_pad_column(r.rows, 5, 0);
    for (int i = 0; i < PRN_AMS_SLOTS; ++i) {
      lv_obj_t* cell = box(r.rows, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_all(cell, 4, 0);
      lv_obj_set_style_pad_row(cell, 2, 0);
      lv_obj_set_style_radius(cell, 9, 0);
      lv_obj_set_style_border_width(cell, 1, 0);
      lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
      lv_obj_t* material = label(cell, &deck_font_12);
      lv_obj_set_width(material, lv_pct(100));
      lv_obj_t* remaining = label(cell, &deck_font_12, LV_OPA_70);
      lv_obj_set_width(remaining, lv_pct(100));
      if (cinematicBambu(*r.def)) {
        auto* spool = lv_obj_create(cell);
        lv_obj_remove_style_all(spool);
        lv_obj_remove_flag(spool, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        reelBuild(spool);
        reelSize(spool, 32);
        lv_obj_move_to_index(spool, 0);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_text_align(material, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(remaining, LV_TEXT_ALIGN_CENTER, 0);
      }
    }
    return;
  }
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
  else if (kind == "summary") makeSummary(r, parent);
  else if (kind == "nozzle_left" || kind == "nozzle_right" ||
           kind == "bed" || kind == "chamber" || kind == "temperatures")
    makeTemperature(r, parent);
  else if (kind == "filament") makeFilament(r, parent);
  else if (kind == "layer") makeLayer(r, parent);
  else if (kind == "light") makeLight(r, parent, index);
  else makeAms(r, parent, kind == "slots");
  styleBambuCard(r);
  if (kind != "light" && !(kind == "hero" && cinematicBambu(*r.def))) lv_obj_remove_flag(parent, LV_OBJ_FLAG_CLICKABLE);
  // New tiles need an initial snapshot even if printer state has not changed.
  // Rebuilding pages otherwise leaves them at their placeholder values.
  s_bambuSeen = UINT32_MAX;
}

void fitBambu(TileRt& r, int cw, int ch) {
  if (cw <= 0 || ch <= 0) return;
  const bool large = roomyWidget(cw, ch);
  const bool tall = large && ch >= 150;
  const String& kind = r.def->sub;
  if (r.name) lv_obj_set_style_text_font(r.name, large ? &deck_font_20 :
      (kind == "light" || kind == "ams" || kind == "slots") ? &deck_font_14 : &deck_font_12, 0);
  const bool temperature = kind == "nozzle_left" || kind == "nozzle_right" ||
                           kind == "bed" || kind == "chamber" || kind == "temperatures";
  if (r.detail && !(kind == "hero" && cinematicBambu(*r.def))) lv_obj_set_style_text_font(r.detail, large ? &deck_font_20 :
                                         temperature ? &deck_font_14 : &deck_font_12, 0);
  if (kind == "hero" && cinematicBambu(*r.def)) {
    const bool art = (cw >= 340 && ch >= 170) || (officeBambuCamera().length() && cw >= 240 && ch >= 120);
    const bool detail = ch >= 135;
    const bool spacious = ch >= 220 && cw >= 300;
    const int artW = art ? cw * 30 / 100 : 0;
    const int x = art ? artW + 14 : 0;
    const int w = LV_MAX(1, cw - x);
    auto place = [&](lv_obj_t* o, int y, int width, const lv_font_t* font, int left) {
      lv_obj_set_style_text_font(o, font, 0);
      lv_obj_set_size(o, LV_MAX(1, width), lv_font_get_line_height(font));
      lv_obj_set_pos(o, left, y);
    };
    toggleVisible(r.chip, art);
    if (art) { lv_obj_set_pos(r.chip, 0, 0); lv_obj_set_size(r.chip, artW, ch); lv_obj_set_style_clip_corner(r.chip, true, 0); }
    const auto* statusFont = spacious ? &deck_font_20 : &deck_font_12;
    const auto* pctFont = detail ? &lv_font_montserrat_40 : &deck_font_20;
    const int statusH = lv_font_get_line_height(statusFont);
    const int headerW = LV_MIN(w, spacious ? 150 : 100);
    const int alertW = w >= 360 ? 90 : 0;
    place(r.name, 0, headerW, statusFont, x);
    place(r.sub, 0, LV_MAX(1, w - headerW - alertW - 4), statusFont, x + headerW + 4);
    toggleVisible(r.sub, w > headerW + alertW + 30);
    lv_obj_set_style_text_color(r.sub, lv_color_hex(C_TEXT), 0);
    const int pctY = statusH + (spacious ? 12 : 3);
    place(r.bambuParts[5], pctY, w, pctFont, x);
    const int barY = detail ? pctY + lv_font_get_line_height(pctFont) + 10 : ch - 5;
    lv_obj_set_pos(r.viz, x, barY);
    lv_obj_set_size(r.viz, w, detail ? 8 : 4);
    const int labelY = barY + (spacious ? 22 : 8);
    const bool three = w >= 345;
    const int columns = three ? 3 : 2;
    const int colW = w / columns;
    const bool captions = detail && labelY + 53 <= ch;
    const auto* valueFont = spacious ? &deck_font_28 : &deck_font_14;
    const int lowerY = labelY + (captions ? lv_font_get_line_height(&deck_font_14) + 4 : 0);
    const bool lower = detail && lowerY + lv_font_get_line_height(valueFont) <= ch;
    place(r.bambuParts[0], labelY, colW - 4, &deck_font_14, x);
    place(r.bambuParts[1], labelY, colW - 4, &deck_font_14, x + colW);
    place(r.bambuParts[2], labelY, colW - 4, &deck_font_14, x + 2 * colW);
    for (int part : {0, 1, 2}) lv_obj_set_style_text_color(r.bambuParts[part], lv_color_hex(C_DIM), 0);
    toggleVisible(r.bambuParts[0], captions && lower);
    toggleVisible(r.bambuParts[1], captions && lower);
    toggleVisible(r.bambuParts[2], captions && lower && three);
    place(r.bambuParts[3], lowerY, colW - 4, valueFont, x);
    place(r.bambuParts[4], lowerY, colW - 4, valueFont, x + colW);
    place(r.bambuParts[6], lowerY, colW - 4, spacious ? &deck_font_20 : &deck_font_12, x + 2 * colW);
    lv_obj_set_style_text_color(r.bambuParts[4], lv_color_hex(modernColour(0x8DC5FF)), 0);
    toggleVisible(r.bambuParts[3], lower);
    toggleVisible(r.bambuParts[4], lower);
    toggleVisible(r.bambuParts[6], lower && three);
    place(r.bambuParts[7], 0, LV_MAX(1, alertW), &deck_font_12, x + w - alertW);
    return;
  }
  if (kind == "hero") {
    for (int part : {1, 4, 7})
      lv_obj_set_style_text_font(r.bambuParts[part], large ? &deck_font_20 : &deck_font_12, 0);
    // The original renderer hides ETA / printer name on narrow layouts.
    toggleVisible(r.bambuParts[1], cw >= 185);
    toggleVisible(r.bambuParts[4], cw >= 265);
    if (cw < 325) toggleVisible(r.bambuParts[7], false);
    lv_obj_set_style_text_font(r.bambuParts[3],
                              tall ? &lv_font_montserrat_40 : ch >= 72 ? &deck_font_28 : &deck_font_20, 0);
    lv_obj_set_style_text_font(r.bambuParts[5],
                              tall ? &deck_font_28 : ch >= 66 ? &deck_font_20 : &deck_font_14, 0);
  } else if (kind == "nozzle_left" || kind == "nozzle_right" ||
             kind == "bed" || kind == "chamber" || kind == "temperatures") {
    const bool compact = ch < 50;
    if (cinematicBambu(*r.def) && ch >= 110 && cw >= 150) {
      lv_obj_set_layout(r.obj, LV_LAYOUT_NONE);
      lv_obj_set_pos(r.name, 0, 0);
      lv_obj_set_width(r.name, cw);
      lv_obj_set_pos(r.bambuParts[0], 0, ch / 2 - 30);
      lv_obj_set_size(r.bambuParts[0], cw, 58);
      lv_obj_set_flex_align(r.bambuParts[0], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_text_font(r.sub, &lv_font_montserrat_40, 0);
      lv_obj_set_flex_grow(r.detail, 1);
      lv_obj_set_style_text_align(r.detail, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_style_text_font(r.detail, cw >= 230 ? &deck_font_20 : &deck_font_12, 0);
      toggleVisible(r.detail, true);
      lv_obj_set_pos(r.viz, 0, ch - 18);
      lv_obj_set_size(r.viz, cw, 8);
      return;
    }
    lv_obj_set_style_text_font(r.sub, tall ? &lv_font_montserrat_40 : ch >= 67 && cw >= 88 ?
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
  } else if (kind == "summary") {
    lv_obj_set_style_text_font(r.sub, tall ? &lv_font_montserrat_40 : cw >= 120 && ch >= 65 ? &deck_font_28 : &deck_font_20, 0);
    toggleVisible(r.detail, ch >= 70);
    toggleVisible(r.viz, ch >= 45);
  } else if (kind == "ams" && r.rows) {
    // In a compact single-row tile keep the original AMS status readable.
    const bool details = cw >= 150 && ch >= 112;
    const bool wideHeader = cw >= 210;
    if (r.bambuParts[0]) {
      lv_obj_set_flex_flow(r.bambuParts[0], wideHeader ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_grow(r.name, 0);
      lv_obj_set_width(r.name, wideHeader ? LV_SIZE_CONTENT : lv_pct(100));
      lv_obj_set_width(r.sub, wideHeader ? LV_SIZE_CONTENT : lv_pct(100));
      lv_obj_set_style_text_align(r.sub, LV_TEXT_ALIGN_LEFT, 0);
      lv_obj_set_style_text_font(r.sub, large ? &deck_font_20 : &deck_font_12, 0);
    }
    const int reserved = wideHeader ? (tall ? 52 : 40) : (tall ? 82 : 62);
    toggleVisible(r.rows, details);
    for (int i = 0; i < PRN_AMS_SLOTS; ++i) {
      lv_obj_t* cell = lv_obj_get_child(r.rows, i);
      if (!cell) continue;
      lv_obj_set_size(cell, LV_MAX(32, (cw - 15) / 4), LV_MAX(48, ch - reserved));
      const bool spoolCard = cinematicBambu(*r.def) && lv_obj_get_child_count(cell) >= 3;
      for (int child = 0; child < 2; ++child)
        lv_obj_set_style_text_font(lv_obj_get_child(cell, child + (spoolCard ? 1 : 0)), tall && cw >= 360 ? &deck_font_20 : &deck_font_12, 0);
      if (spoolCard) {
        const int diameter = LV_MAX(12, LV_MIN((cw - 15) / 4 - 12, (ch - reserved) - (tall && cw >= 360 ? 56 : 35)));
        auto* spool = lv_obj_get_child(cell, 0);
        reelSize(spool, LV_MIN(96, diameter));
      }
    }
    toggleVisible(r.detail, !details);
  } else if (kind == "filament") {
    const int diameter = LV_MIN(tall ? 72 : 44, ch - 7);
    if (cinematicBambu(*r.def)) reelSize(r.chip, diameter);
    else lv_obj_set_size(r.chip, LV_MAX(12, diameter), LV_MAX(12, diameter));
    lv_obj_set_style_text_font(r.sub, tall ? &deck_font_28 : ch >= 62 ? &deck_font_20 : &deck_font_14, 0);
    toggleVisible(r.name, ch >= 48);
    if (cw < 180 || ch < 55)
      for (int i = 1; i <= 3; ++i) toggleVisible(r.bambuParts[i], false);
  } else if (kind == "layer") {
    if (cinematicBambu(*r.def) && r.bambuParts[0]) {
      // Mirror the nozzle and bed cards: heading, one horizontal reading,
      // and a bottom progress bar. Compact cards combine both counts.
      auto* reading = r.bambuParts[0];
      const bool expanded = ch >= 110 && cw >= 150;
      lv_obj_set_layout(r.obj, LV_LAYOUT_NONE);
      lv_obj_set_width(r.name, cw);
      lv_obj_set_pos(r.name, 0, 0);
      lv_obj_set_width(reading, cw);
      lv_obj_set_style_pad_column(reading, 4, 0);
      lv_obj_set_flex_grow(r.detail, expanded ? 1 : 0);
      lv_obj_set_style_text_align(r.detail, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_width(r.sub, expanded ? LV_SIZE_CONTENT : lv_pct(100));
      const auto* countFont = expanded ? &lv_font_montserrat_40 : ch >= 58 ? &deck_font_20 : &deck_font_14;
      lv_obj_set_style_text_font(r.sub, countFont, 0);
      lv_obj_set_style_text_font(r.detail, cw >= 230 ? &deck_font_20 : &deck_font_12, 0);
      const int headingH = lv_font_get_line_height(large ? &deck_font_20 : &deck_font_12);
      const int countH = lv_font_get_line_height(countFont);
      const bool heading = ch >= headingH + countH + 2;
      const int barH = expanded ? 8 : 3;
      const bool bar = ch >= countH + (heading ? headingH + 2 : 0) + barH + 2;
      const int top = heading ? headingH + 2 : 0;
      const int bottom = bar ? ch - barH - (expanded ? 10 : 2) : ch;
      const int readingH = LV_MAX(countH, bottom - top);
      lv_obj_set_size(reading, cw, readingH);
      lv_obj_set_pos(reading, 0, top);
      toggleVisible(r.name, heading);
      toggleVisible(r.detail, expanded);
      lv_obj_set_size(r.viz, cw, barH);
      lv_obj_set_pos(r.viz, 0, expanded ? ch - 18 : ch - barH);
      toggleVisible(r.viz, bar);
    } else {
      lv_obj_set_flex_flow(r.obj, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_grow(r.name, 0);
      lv_obj_set_width(r.name, lv_pct(100));
      lv_obj_set_width(r.sub, lv_pct(100));
      lv_obj_set_style_text_font(r.sub, ch >= 58 ? &deck_font_20 : &deck_font_14, 0);
      toggleVisible(r.name, ch >= 33);
      toggleVisible(r.detail, false);
    }
    s_bambuSeen = UINT32_MAX;
  } else if (kind == "light") {
    if (cinematicBambu(*r.def) && ch >= 110) {
      lv_obj_set_flex_align(r.obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_row(r.obj, 8, 0);
      const int diameter = LV_MIN(84, LV_MIN(cw - 8, ch / 2));
      lv_obj_set_size(r.chip, diameter, diameter);
      const auto* font = iconFont(40);
      if (font && iconCodepoint("mdi:lightbulb")) lv_obj_set_style_text_font(r.icon, font, 0);
      lv_obj_center(r.icon);
      lv_obj_set_style_text_font(r.name, &deck_font_20, 0);
      lv_obj_set_style_text_font(r.sub, &deck_font_20, 0);
      return;
    }
    lv_obj_set_size(r.chip, large ? 48 : 29, large ? 48 : 29);
    lv_obj_center(r.icon);
    lv_obj_set_style_text_font(r.sub, large ? &deck_font_20 : &deck_font_12, 0);
    toggleVisible(r.sub, ch >= 62);
  } else if (kind == "slots" && r.rows) {
    const int columns = cw >= 220 ? 4 : 2;
    const int slotWidth = LV_MAX(1, (cw - (columns - 1) * 5) / columns);
    const int slotHeight = LV_MIN(tall ? 72 : 45, LV_MAX(24, (ch - 42) / (8 / columns) - 4));
    for (int i = 0; i < 8; ++i) {
      lv_obj_t* cell = lv_obj_get_child(r.rows, i);
      lv_obj_set_size(cell, slotWidth, slotHeight);
      lv_obj_set_style_text_font(lv_obj_get_child(cell, 0), tall ? &deck_font_20 : &deck_font_12, 0);
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
      status == "PRINTING" ? (dashboardBambu(*r.def) ? dashboardAccent(*r.def) : PRN_VIOLET) :
      status == "PAUSED" ? PRN_HEAT :
      status == "FAILED" ? PRN_ERROR :
      status == "DONE" ? (dashboardBambu(*r.def) ? dashboardAccent(*r.def) : PRN_BLUE) : PRN_IDLE;
    const bool cinematic = cinematicBambu(*r.def);
    const bool hasJob = online && state->job[0] && status != "READY";
    setText(r.name, (hasError ? String("ERROR") : status) + (cinematic && (hasJob || (hasError && state->err[0])) ? ":" : ""));
    if (cinematicBambu(*r.def)) {
      lv_obj_set_style_text_color(r.name, lv_color_hex(status == "PRINTING" && !hasError ? 0x71DFB6 : statusColor), 0);
    } else lv_obj_set_style_bg_color(r.name, lv_color_hex(statusColor), 0);
    setText(r.sub, cinematic ? (hasJob ? String(state->job) : String("")) : online ? (state->job[0] ? String(state->job) : "Printer ready") : "Waiting for printer");
    toggleVisible(r.bambuParts[7], online && state->door == 1 && r.fitW >= 325);
    if (online && hasError && state->err[0]) setText(r.sub, String(state->err));
    if (!cinematic) setText(r.bambuParts[1], "P1S");
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
        if (cinematic) {
          struct tm today;
          localtime_r(&now, &today);
          const bool otherDay = today.tm_year != local.tm_year || today.tm_yday != local.tm_yday;
          char day[16] = "";
          if (otherDay) strftime(day, sizeof(day), "%a ", &local);
          eta = String(day) + tm;
        } else eta = String("ETA ") + tm;
      }
    }
    setText(r.bambuParts[4], cinematic ? (status == "PAUSED" ? String("Paused") : eta.length() ? eta : String("--")) : eta);
    setText(r.bambuParts[5], active || status == "DONE" ?
      String(status == "DONE" ? 100 : state->progress) + "%" : "");
    setProgress(r.viz, active ? state->progress : status == "DONE" ? 100 : 0, statusColor);
    const int fill = active ? state->progress : status == "DONE" ? 100 : 0;
    if (cinematicBambu(*r.def)) {
      setText(r.bambuParts[5], online ? String(fill) + "%" : "--");
      setText(r.bambuParts[6], online && state->totalLayers ? String(state->layer) + " / " + String(state->totalLayers) : "--");
      lv_obj_set_style_text_color(r.bambuParts[5], lv_color_hex(C_TEXT), 0);
      lv_obj_set_style_text_color(r.bambuParts[6], lv_color_hex(C_DIM), 0);
      setProgress(r.viz, fill, dashboardAccent(*r.def));
      r.frac = (float)fill / 100.0f;
      r.on = online && status == "PRINTING";
      auto* tint = lv_obj_get_child(r.chip, 1);
      auto* head = lv_obj_get_child(r.chip, 3);
      toggleVisible(tint, fill > 0);
      lv_obj_set_size(tint, lv_pct(fill), lv_pct(100));
      toggleVisible(head, r.on);
      const PrinterTray* spool = activeTray(*state);
      lv_obj_set_style_bg_color(head, lv_color_hex(spool ? spool->rgb & 0xFFFFFF : PRN_BLUE), 0);
      delete state;
      markPageStale(r.page);
      return;
    }
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
    const uint32_t colour = heating ? PRN_HEAT :
      dashboardBambu(*r.def) ? C_TEXT : PRN_INK;
    setText(r.name, caption);
    setText(r.sub, online ? tempText(now) : "--");
    setText(r.detail, heating ? (cinematicBambu(*r.def) ? String("Target ") + String(target, 0) + "°" : String("/") + String(target, 0)) : "");
    lv_obj_set_style_text_color(r.sub, lv_color_hex(colour), 0);
    setProgress(r.viz, online && isfinite(now) ?
      (int)(100 * now / (cinematicBambu(*r.def) ? maximum : heating ? target : maximum)) : 0,
      heating ? PRN_HEAT : dashboardBambu(*r.def) ? dashboardAccent(*r.def) : PRN_BLUE);
  } else if (kind == "summary") {
    const bool printing = online && (!strcmp(state->gcodeState, "RUNNING") ||
                                     !strcmp(state->gcodeState, "PAUSE"));
    const bool done = online && !strcmp(state->gcodeState, "FINISH");
    const String status = !online ? "Connecting" : printing ?
        (!strcmp(state->gcodeState, "PAUSE") ? "Paused" : "Printing") :
        done ? "Done" : !strcmp(state->gcodeState, "FAILED") ? "Failed" : "Ready";
    setText(r.name, status);
    setText(r.sub, printing ? String(state->progress) + "%" : done ? "100%" : status);
    setText(r.detail, printing ? duration(state->remainingMin) + " remaining" :
            done ? "Finished" : "");
    setProgress(r.viz, printing ? state->progress : done ? 100 : 0,
                dashboardBambu(*r.def) ? dashboardAccent(*r.def) : PRN_BLUE);
  } else if (kind == "filament") {
    const PrinterTray* tray = online ? activeTray(*state) : nullptr;
    if (cinematicBambu(*r.def)) reelState(r.chip, tray, online && !strcmp(state->gcodeState, "RUNNING"));
    if (tray) {
      const uint32_t rgb = tray->rgb & 0xFFFFFF;
      if (!cinematicBambu(*r.def)) {
        lv_obj_set_style_bg_color(r.chip, lv_color_hex(rgb), 0);
        lv_obj_set_style_bg_opa(r.chip, tray->alpha < 128 ? LV_OPA_50 : LV_OPA_COVER, 0);
      }
      const String place = state->activeUnit == 254 ? "External spool" :
        "AMS " + String(state->activeUnit + 1) + ", slot " + String(state->activeSlot + 1);
      setText(r.name, place);
      setText(r.sub, tray->type[0] ? String(tray->type) : "Filament");
      toggleVisible(r.viz, tray->remain >= 0);
      setProgress(r.viz, tray->remain >= 0 ? tray->remain : 0, rgb);
    } else {
      if (!cinematicBambu(*r.def)) {
        lv_obj_set_style_bg_color(r.chip, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(r.chip, LV_OPA_20, 0);
      }
      setText(r.name, online ? String(state->amsCount) + " AMS unit(s)" : "");
      setText(r.sub, online ? "Not feeding" : "--");
      toggleVisible(r.viz, false);
    }
    // Job mapping is ordered by the slicer. Show subsequent distinct loaded
    // colours only when the current job/AMS slot can be matched to that map.
    for (int i = 1; i <= 3; ++i) toggleVisible(r.bambuParts[i], false);
    if (dashboardBambu(*r.def) && online && tray && state->mapCount &&
        lv_obj_get_content_width(r.obj) >= 180 && lv_obj_get_content_height(r.obj) >= 55) {
      int current = -1;
      for (int k = 0; k < (int)state->mapCount && k < PRN_MAP_MAX; ++k) {
        const uint16_t entry = state->map[k];
        const uint8_t rawUnit = (uint8_t)(entry >> 8), slot = (uint8_t)entry;
        if (state->activeUnit >= state->amsCount || slot != state->activeSlot) continue;
        if (rawUnit == state->ams[state->activeUnit].id) { current = k; break; }
      }
      int swatches = 0;
      for (int k = current + 1; current >= 0 && k < (int)state->mapCount &&
                                      k < PRN_MAP_MAX && swatches < 3; ++k) {
        const uint8_t unitId = (uint8_t)(state->map[k] >> 8);
        const uint8_t slot = (uint8_t)state->map[k];
        if (slot >= PRN_AMS_SLOTS) continue;
        const PrinterTray* future = nullptr;
        for (unsigned u = 0; u < state->amsCount && !future; ++u)
          if (state->ams[u].present && state->ams[u].id == unitId &&
              slot < state->ams[u].slots && state->ams[u].tray[slot].present)
            future = &state->ams[u].tray[slot];
        if (!future) continue;
        if (future->rgb == tray->rgb) continue;
        lv_obj_t* chip = r.bambuParts[++swatches];
        if (!chip) break;
        lv_obj_set_style_bg_color(chip, lv_color_hex(future->rgb & 0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(chip, future->alpha < 128 ? LV_OPA_50 : LV_OPA_COVER, 0);
        toggleVisible(chip, true);
      }
    }
  } else if (kind == "layer") {
    const bool expanded = cinematicBambu(*r.def) && lv_obj_get_content_height(r.obj) >= 110 && lv_obj_get_content_width(r.obj) >= 150;
    const bool known = online && state->totalLayers;
    setText(r.sub, known ? String(state->layer) +
      (expanded ? String("") : String(" / ") + String(state->totalLayers)) : String("--"));
    setText(r.detail, known && expanded ? String("of ") + String(state->totalLayers) : "");
    if (r.viz) setProgress(r.viz, online && state->totalLayers ? (int)(100UL * state->layer / state->totalLayers) : 0, dashboardAccent(*r.def));
  } else if (kind == "light") {
    const bool lit = online && state->light == 1;
    const uint32_t colour = lit ? PRN_HEAT :
      dashboardBambu(*r.def) ? dashboardAccent(*r.def) : PRN_VIOLET;
    lv_obj_set_style_bg_color(r.chip, lv_color_hex(cinematicBambu(*r.def) ? modernColour(0x29445F) : colour), 0);
    if (cinematicBambu(*r.def)) lv_obj_set_style_text_color(r.icon, lv_color_hex(lit ? PRN_HEAT : PRN_IDLE), 0);
    setText(r.sub, !online ? "Offline" : state->light < 0 ? "Unavailable" :
      lit ? "On" : "Off");
  } else if (kind == "ams" || kind == "slots") {
    const int chosen = constrain((int)r.def->bambuAmsUnit - 1, 0, PRN_AMS_UNITS - 1);
    const PrinterAmsUnit* selected = online && chosen < state->amsCount &&
                                    state->ams[chosen].present ? &state->ams[chosen] : nullptr;
    if (kind == "ams" && dashboardBambu(*r.def)) {
      const String heading = r.def->label.length() ? r.def->label :
              "AMS " + String(r.def->bambuAmsUnit);
      setText(r.name, heading + (r.bambuParts[0] ? ":" : ""));
      String info;
      if (selected) {
        if (selected->humidity <= 100) info = String(selected->humidity) + "% RH";
        if (isfinite(selected->temp)) {
          if (info.length()) info += " · ";
          info += String(selected->temp, 0) + "°C";
        }
      }
      setText(r.sub, info.length() ? info : online ? "No AMS data" : "Connecting");
      unsigned loaded = 0;
      if (selected)
        for (unsigned i = 0; i < selected->slots && i < PRN_AMS_SLOTS; ++i)
          if (selected->tray[i].present) ++loaded;
      setText(r.detail, selected ? String(loaded) + " slots loaded" : "");
      if (r.rows)
        for (int i = 0; i < PRN_AMS_SLOTS; ++i) {
          lv_obj_t* cell = lv_obj_get_child(r.rows, i);
          toggleVisible(cell, selected && i < selected->slots);
          if (selected && i < selected->slots)
            amsDetailSlot(cell, &selected->tray[i],
              state->activeUnit == chosen && state->activeSlot == i, i + 1,
               !strcmp(state->gcodeState, "RUNNING"));
        }
    } else {
      setText(r.sub, online ? String(state->amsCount) + " AMS unit(s)" : "Connecting");
      String info;
      if (selected) {
        unsigned loaded = 0;
        for (unsigned i = 0; i < selected->slots && i < PRN_AMS_SLOTS; ++i)
          if (selected->tray[i].present) ++loaded;
        info = String(loaded) + " slots loaded";
        if (selected->humidity <= 100) info += " · " + String(selected->humidity) + "% RH";
      }
      setText(r.detail, info);
    }
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

// UI task: detach all references before releasing the descriptor's pixels.
void cameraClear() {
  for (auto& tile : s_tiles) {
    if (!tile.def || tile.def->type != TILE_BAMBU || tile.def->sub != "hero" ||
        !cinematicBambu(*tile.def) || !tile.detail) continue;
    lv_image_set_src(tile.detail, nullptr);
    toggleVisible(tile.detail, false);
    toggleVisible(tile.icon, true);
    toggleVisible(lv_obj_get_child(tile.chip, 4), false);
  }
  if (s_cameraPixels) {
    lv_image_cache_drop(&s_cameraImage);
    free(s_cameraPixels);
    s_cameraPixels = nullptr;
  }
}

void cameraSync() {
  const String entity = officeBambuCamera();
  if (entity != s_cameraEntity) {
    cameraClear();
    s_cameraEntity = entity;
    s_cameraLast = 0;
  }
  TileRt* target = nullptr;
  for (auto& tile : s_tiles) {
    if (!tile.def || tile.def->type != TILE_BAMBU || tile.def->sub != "hero" ||
        !cinematicBambu(*tile.def) || !tile.detail || !tile.on ||
        lv_obj_has_flag(tile.chip, LV_OBJ_FLAG_HIDDEN) || !lv_obj_is_visible(tile.chip)) continue;
    target = &tile;
    break;
  }
  int w = 0, h = 0;
  if (target) {
    w = lv_obj_get_content_width(target->chip);
    h = lv_obj_get_content_height(target->chip);
    #if defined(BOARD_IS_JC8012P4A1C)
    constexpr int maxSide = 400;
    #else
    constexpr int maxSide = 160;
    #endif
    const int side = std::max(w, h);
    if (side > maxSide) { w = w * maxSide / side; h = h * maxSide / side; }
    w = w & ~1; h = h & ~1;
  }
  if (s_cameraDone.exchange(false, std::memory_order_acquire)) {
    if (s_cameraJob.pixels && entity == s_cameraJob.entity && target &&
        w == s_cameraJob.w && h == s_cameraJob.h) {
      cameraClear();
      s_cameraPixels = s_cameraJob.pixels;
      s_cameraJob.pixels = nullptr;
      const time_t now = time(nullptr);
      s_cameraUpdated = "Updated";
      if (now > 1609459200) {
        struct tm local;
        localtime_r(&now, &local);
        char stamp[32];
        strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
        s_cameraUpdated = stamp;
      }
      s_cameraW = w; s_cameraH = h;
      s_cameraImage = {};
      s_cameraImage.header.magic = LV_IMAGE_HEADER_MAGIC;
      s_cameraImage.header.cf = LV_COLOR_FORMAT_RGB565;
      s_cameraImage.header.w = w; s_cameraImage.header.h = h;
      s_cameraImage.header.stride = w * 2;
      s_cameraImage.data_size = (size_t)w * h * 2;
      s_cameraImage.data = (const uint8_t*)s_cameraPixels;
    } else {
      free(s_cameraJob.pixels);
      s_cameraJob.pixels = nullptr;
      if (entity != s_cameraJob.entity) s_cameraLast = 0;
    }
    s_cameraBusy.store(false, std::memory_order_release);
  }
  if (!target || !entity.length()) return;
  if (s_cameraPixels && s_cameraW == w && s_cameraH == h) {
    if (lv_image_get_src(target->detail) != &s_cameraImage) {
      lv_image_set_src(target->detail, &s_cameraImage);
      const int cw = lv_obj_get_content_width(target->chip);
      const int ch = lv_obj_get_content_height(target->chip);
      lv_image_set_scale(target->detail, std::max(cw * 256 / w, ch * 256 / h));
      lv_obj_center(target->detail);
      toggleVisible(target->detail, true);
      toggleVisible(target->icon, false);
      auto* stamp = lv_obj_get_child(target->chip, 4);
      const bool wide = cw >= 180;
      setText(stamp, s_cameraUpdated == "Updated" ? s_cameraUpdated : (wide ? String("Last updated: ") : String("")) + s_cameraUpdated);
      lv_obj_align(stamp, LV_ALIGN_BOTTOM_MID, 0, -5);
      toggleVisible(stamp, true);
    }
  }
  if (w < 32 || h < 32 || s_cameraBusy.load(std::memory_order_acquire)) return;
  const uint32_t now = millis();
  if (s_cameraLast && now - s_cameraLast < (uint32_t)officeBambuCameraRefresh() * 1000) return;
  s_cameraLast = now ? now : 1;
  s_cameraJob.entity = entity;
  s_cameraJob.w = w; s_cameraJob.h = h; s_cameraJob.pixels = nullptr;
  s_cameraBusy.store(true, std::memory_order_release);
  if (!wallRunWorker(cameraWorker, false, "bambu_camera"))
    s_cameraBusy.store(false, std::memory_order_release);
}

void bambuSync() {
  cameraSync();
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
      if (!page.def || !page.title ||
          (page.def->id != "office_printer" && page.def->id != "office_ams")) continue;
      const String manualName = deck::officeBambuDisplayName();
      const String lanName = deck::officeBambuLiveName();
      const String printerName = sensorName.length() ? sensorName :
          lanName.length() ? lanName :
          manualName.length() ? manualName : "";
      // AMS belongs to the same printer; retain the page distinction while
      // using exactly the same source/priority as its main printer header.
      const String title = printerName.length()
          ? printerName + (page.def->id == "office_ams" ? " AMS" : "")
          : page.def->name.length() ? page.def->name : page.def->id;
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
        if (!page.def || !page.title ||
            (page.def->id != "office_printer" && page.def->id != "office_ams")) continue;
        const String title = lanName +
            (page.def->id == "office_ams" ? " AMS" : "");
        if (strcmp(lv_label_get_text(page.title), title.c_str())) {
          lv_label_set_text(page.title, title.c_str());
          page.stale = true;
        }
      }
    }
  }
  // Reels advance at ~6 fps; only visible, actively feeding reels move.
  static uint32_t reelFrame = 0;
  static int reelAngle = 0;
  if (millis() - reelFrame >= 165) {
    reelFrame = millis();
    reelAngle = (reelAngle + 6) % 360;
    for (auto& tile : s_tiles) {
      if (!tile.def || tile.def->type != TILE_BAMBU || !cinematicBambu(*tile.def)) continue;
      if (tile.def->sub == "filament") reelAnimate(tile.chip, reelAngle);
      else if (tile.def->sub == "ams" && tile.rows) {
        for (int i = 0; i < PRN_AMS_SLOTS; ++i) {
          auto* cell = lv_obj_get_child(tile.rows, i);
          if (cell) reelAnimate(lv_obj_get_child(cell, 0), reelAngle);
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
    if (cinematicBambu(*tile.def)) {
      if (lv_obj_has_flag(tile.chip, LV_OBJ_FLAG_HIDDEN) || !lv_obj_is_visible(tile.chip)) continue;
      auto* head = lv_obj_get_child(tile.chip, 3);
      const int width = lv_obj_get_content_width(tile.chip);
      const int height = lv_obj_get_content_height(tile.chip);
      const int travel = LV_MAX(1, height - 14);
      const uint32_t phase = (millis() / 12) % (uint32_t)(travel * 2);
      lv_obj_set_pos(head, constrain((int)(width * tile.frac) - 5, 0, LV_MAX(0, width - 10)),
                     (int)(phase < (uint32_t)travel ? phase : travel * 2 - phase));
      continue;
    }
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
