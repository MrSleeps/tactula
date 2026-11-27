#include "deck_sys.h"
#include "tactula_setup_view.h"
#include "deck_panel.h"
#include "config.h"
#include "tactula_settings.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <lvgl.h>

namespace deck {
namespace {

constexpr uint32_t C_BG = tactula_setup::BG;
constexpr uint32_t C_CHIP = tactula_setup::TRACK;
constexpr uint32_t C_TITLE = tactula_setup::TITLE;
constexpr uint32_t C_TEXT = tactula_setup::TEXT;
constexpr uint32_t C_DIM = tactula_setup::DIM;
constexpr uint32_t C_TEAL = tactula_setup::ACCENT;  // Tactula blue success accent
constexpr uint32_t C_VIOLET = 0xB8ADFF;
constexpr uint32_t C_BLUE = tactula_setup::ACCENT;
constexpr uint32_t C_RED = 0xFF8A7A;

lv_obj_t* s_root = nullptr;
ScreenState s_shown = SCREEN_DECK;
bool s_rebuild = false;

lv_obj_t* s_splashStatus = nullptr;
lv_obj_t* s_splashDots[3] = {};
lv_timer_t* s_splashTimer = nullptr;
unsigned s_splashStep = 0;

void pulseSplash(lv_timer_t*) {
  lv_obj_set_style_bg_opa(s_splashDots[s_splashStep], LV_OPA_30, 0);
  s_splashStep = (s_splashStep + 1) % 3;
  lv_obj_set_style_bg_opa(s_splashDots[s_splashStep], LV_OPA_COVER, 0);
}

// Firmware update widgets, valid while that screen is up.
lv_obj_t* s_otaTitle = nullptr;
lv_obj_t* s_otaPct = nullptr;
lv_obj_t* s_otaBar = nullptr;
lv_obj_t* s_otaInfo = nullptr;
lv_obj_t* s_otaNote = nullptr;
int s_otaLastPct = -1;

lv_obj_t* box(lv_obj_t* parent, lv_flex_flow_t flow, int gap) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(o, flow);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(o, gap, 0);
  lv_obj_set_style_pad_column(o, gap, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}

lv_obj_t* text(lv_obj_t* parent, const char* t, const lv_font_t* f, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

lv_obj_t* gap(lv_obj_t* parent, int h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, 1, h);
  return o;
}

using tactula_setup::logoMark;

// Round chip with a symbol, for the state screens.
void badge(lv_obj_t* parent, const char* symbol, uint32_t bg, uint32_t fg, bool largeScreen) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  const int size = largeScreen ? 76 : 48;
  lv_obj_set_size(c, size, size);
  lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(c, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_t* l = text(c, symbol, largeScreen ? &deck_font_28 : &deck_font_20, fg);
  lv_obj_center(l);
}

void buildSplash(lv_obj_t* col, bool largeScreen) {
  logoMark(col, largeScreen ? 42 : 30);
  gap(col, largeScreen ? 16 : 6);
  text(col, PRODUCT_NAME, &lv_font_montserrat_40, C_TITLE);
  text(col, "v" FW_VERSION, largeScreen ? &deck_font_28 : &deck_font_14, C_DIM);
  gap(col, largeScreen ? 16 : 8);
  s_splashStatus = text(col, "Starting...", largeScreen ? &deck_font_28 : &deck_font_20, C_TEXT);
  const int dotSize = largeScreen ? 16 : 8;
  lv_obj_t* dots = box(col, LV_FLEX_FLOW_ROW, dotSize);
  s_splashStep = 0;
  for (unsigned i = 0; i < 3; ++i) {
    lv_obj_t* dot = lv_obj_create(dots);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, dotSize, dotSize);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(C_BLUE), 0);
    lv_obj_set_style_bg_opa(dot, i == 0 ? LV_OPA_COVER : LV_OPA_30, 0);
    s_splashDots[i] = dot;
  }
  s_splashTimer = lv_timer_create(pulseSplash, largeScreen ? 140 : 220, nullptr);
}

void buildConnected(lv_obj_t* col, bool largeScreen) {
  if (largeScreen) {
    logoMark(col, 30);
    gap(col, 12);
  }
  badge(col, LV_SYMBOL_OK, 0x192E50, 0x48A5FF, largeScreen);
  gap(col, largeScreen ? 16 : 6);
  text(col, "Connected to Wi-Fi", largeScreen ? &lv_font_montserrat_40 : &deck_font_20, C_TITLE);
  auto* ssid = text(col, WiFi.SSID().c_str(), largeScreen ? &deck_font_28 : &deck_font_14, C_BLUE);
  lv_obj_set_width(ssid, logicalW() - 32);
  lv_label_set_long_mode(ssid, LV_LABEL_LONG_MODE_DOTS);
  text(col, WiFi.localIP().toString().c_str(), largeScreen ? &deck_font_28 : &deck_font_20, C_TEXT);
  if (netSettings.mdnsEnabled && netSettings.hostname[0]) {
    String host = String("http://") + netSettings.hostname + ".local";
    auto* label = text(col, host.c_str(), largeScreen ? &deck_font_20 : &deck_font_14, C_DIM);
    lv_obj_set_width(label, logicalW() - 32);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
  }
  gap(col, largeScreen ? 10 : 4);
  text(col, "Loading dashboard...", largeScreen ? &deck_font_20 : &deck_font_14, C_DIM);
}

// Same setup view is used by the 10.1-inch P4 firmware. The original
// JC3248 layout, fonts, colour palette and QR encoding are preserved.
void buildApSetup(lv_obj_t* root, bool landscape) {
  tactula_setup::buildApSetup(root, landscape, getAPSSID(),
                              WIFI_AP_PASSWORD, WiFi.softAPIP().toString(),
                              logicalW() >= 800 && logicalH() >= 800);
}

void buildOta(lv_obj_t* col, int w, int h) {
  const bool large = w >= 800 && h >= 800;
  const int shortSide = w < h ? w : h;
  const int diameter = large ? (shortSide * 60 / 100) : shortSide * 85 / 100;
  const int stroke = large ? 20 : 9;
  auto* ring = lv_obj_create(col);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, diameter, diameter);
  lv_obj_remove_flag(ring, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  s_otaBar = lv_arc_create(ring);
  lv_obj_remove_style_all(s_otaBar);
  lv_obj_set_size(s_otaBar, diameter, diameter);
  lv_obj_center(s_otaBar);
  lv_arc_set_rotation(s_otaBar, 270);
  lv_arc_set_bg_angles(s_otaBar, 0, 360);
  lv_arc_set_range(s_otaBar, 0, 1000);
  lv_arc_set_value(s_otaBar, 0);
  lv_obj_set_style_arc_width(s_otaBar, stroke, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_otaBar, stroke, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_otaBar, lv_color_hex(C_CHIP), LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_otaBar, lv_color_hex(C_BLUE), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(s_otaBar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(s_otaBar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_otaBar, true, LV_PART_INDICATOR);
  lv_obj_remove_flag(s_otaBar, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

  auto* content = box(ring, LV_FLEX_FLOW_COLUMN, large ? 12 : 4);
  lv_obj_set_width(content, diameter * 79 / 100);
  lv_obj_center(content);
  logoMark(content, large ? 26 : 23);
  s_otaTitle = text(content, "Updating firmware", large ? &deck_font_28 : &deck_font_20, C_TITLE);
  s_otaPct = text(content, "0%", large ? &lv_font_montserrat_40 : &deck_font_28, C_BLUE);
  s_otaInfo = text(content, "Receiving", large ? &deck_font_20 : &deck_font_14, C_TEXT);
  s_otaNote = text(content, "Keep the power on.", large ? &deck_font_20 : &deck_font_14, C_DIM);
  for (auto* label : {s_otaTitle, s_otaInfo, s_otaNote}) {
    lv_obj_set_width(label, diameter * 76 / 100);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
  }
  s_otaLastPct = -1;
}

void destroy() {
  if (s_splashTimer) lv_timer_delete(s_splashTimer);
  s_splashTimer = nullptr;
  s_splashStatus = nullptr;
  if (s_root) lv_obj_delete(s_root);
  s_root = nullptr;
  s_otaTitle = s_otaPct = s_otaBar = s_otaInfo = s_otaNote = nullptr;
}

void build(ScreenState st) {
  destroy();
  if (st == SCREEN_DECK) return;
  const int w = logicalW(), h = logicalH();
  const bool largeScreen = w >= 800 && h >= 800;
  // Opaque and clickable: covers the pages and swallows their taps.
  s_root = lv_obj_create(lv_layer_sys());
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, w, h);
  lv_obj_set_style_bg_color(s_root, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s_root, lv_color_hex(C_TEXT), 0);
  lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_root, largeScreen ? 6 : 4, 0);
  lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);

  switch (st) {
    case SCREEN_SPLASH: buildSplash(s_root, largeScreen); break;
    case SCREEN_CONNECTING_WIFI: tactula_setup::buildConnecting(s_root, wifiSSID, largeScreen); break;
    case SCREEN_WIFI_CONNECTED: buildConnected(s_root, largeScreen); break;
    case SCREEN_AP_MODE: buildApSetup(s_root, w > h); break;
    case SCREEN_OTA_UPDATE: buildOta(s_root, w, h); break;
    default: break;
  }
}

}  // namespace

void sysScreenSync(ScreenState state) {
  if (state == s_shown && !s_rebuild) return;
  s_shown = state;
  s_rebuild = false;
  build(state);
}

#if defined(BOARD_IS_JC8012P4A1C)
void sysSplashStatus(const char* status) {
  if (s_splashStatus) lv_label_set_text(s_splashStatus, status);
}
#endif

void sysScreenRelayout() { s_rebuild = s_shown != SCREEN_DECK; }

void sysOtaProgress(uint32_t done, uint32_t total) {
  if (!s_otaBar) return;
  char buf[40];
  if (total) {
    const int permille = (int)((uint64_t)done * 1000 / total);
    lv_arc_set_value(s_otaBar, permille > 1000 ? 1000 : permille);
    const int pct = permille / 10;
    if (pct != s_otaLastPct) {
      s_otaLastPct = pct;
      lv_label_set_text_fmt(s_otaPct, "%d%%", pct > 100 ? 100 : pct);
    }
    snprintf(buf, sizeof(buf), "%.1f of %.1f MB", done / 1048576.0f, total / 1048576.0f);
  } else {
    snprintf(buf, sizeof(buf), "%.1f MB received", done / 1048576.0f);
  }
  lv_label_set_text(s_otaInfo, buf);
}

void sysOtaResult(bool ok, const char* message) {
  if (!s_otaBar) return;
  const uint32_t color = ok ? C_TEAL : C_RED;
  lv_label_set_text(s_otaTitle, ok ? "Update complete" : "Update failed");
  lv_obj_set_style_text_color(s_otaPct, lv_color_hex(color), 0);
  lv_obj_set_style_arc_color(s_otaBar, lv_color_hex(color), LV_PART_INDICATOR);
  if (ok) {
    lv_label_set_text(s_otaPct, "100%");
    lv_arc_set_value(s_otaBar, 1000);
    lv_label_set_text(s_otaInfo, "Restarting");
    lv_label_set_text(s_otaNote, LV_SYMBOL_OK "  Keep the power on.");
  } else {
    lv_label_set_text(s_otaPct, LV_SYMBOL_CLOSE);
    lv_label_set_text(s_otaInfo, message && message[0] ? message : "The upload did not complete");
    lv_label_set_text(s_otaNote, "The current firmware keeps running.");
  }
}

}  // namespace deck
