#pragma once
// The one Tactula QR onboarding layout shared by JC3248W535 and JC8012P4A1C.
// Keep this identical between boards: the only board-specific difference
// is largeScreen scaling for the 10.1-inch 1280x800 display.
#include <Arduino.h>
#include <lvgl.h>

namespace tactula_setup {
constexpr uint32_t BG = 0x0C1019;
constexpr uint32_t TITLE = 0xEDF0F8;
constexpr uint32_t TEXT = 0xD3DAEA;
constexpr uint32_t DIM = 0x8795AC;
constexpr uint32_t ACCENT = 0x48A5FF;
constexpr uint32_t TRACK = 0x252E3D;

inline lv_obj_t* box(lv_obj_t* parent, lv_flex_flow_t flow, int spacing) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(o, spacing, 0);
    lv_obj_set_style_pad_column(o, spacing, 0);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return o;
}
inline lv_obj_t* text(lv_obj_t* parent, const char* value, const lv_font_t* font, uint32_t color) {
    lv_obj_t* o = lv_label_create(parent);
    lv_label_set_text(o, value);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    return o;
}
inline void gap(lv_obj_t* parent, int height) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 1, height);
}

// Four rounded tiles in the deck's accent colours: the product mark.
inline void logoMark(lv_obj_t* parent, int cell) {
  // Approximation of the tactula vector mark, built with four LVGL tiles.
  // Drawn directly, so boot does not depend on a filesystem or PNG decoder.
  const int width = cell * 2 + cell / 3;
  lv_obj_t* m = lv_obj_create(parent);
  lv_obj_remove_style_all(m);
  lv_obj_set_size(m, width, width);
  // Scale the approved 30px splash mark proportionally at compact sizes.
  // Keep the original large-screen geometry and the splash itself intact.
  const auto scaled = [cell](int n) { return (n * cell + 15) / 30; };
  const int x[] = {0, cell <= 30 ? scaled(34) : cell + 4, 0, cell <= 30 ? scaled(29) : cell - 1};
  const int y[] = {0, cell / 5, cell <= 30 ? scaled(34) : cell + 4, cell <= 30 ? scaled(31) : cell + 1};
  const int w[] = {cell <= 30 ? scaled(33) : cell + 3, cell <= 30 ? scaled(24) : cell - 6,
                   cell <= 30 ? scaled(26) : cell - 4, cell <= 30 ? scaled(35) : cell + 5};
  const int h[] = {cell <= 30 ? scaled(32) : cell + 2, cell <= 30 ? scaled(22) : cell - 8,
                   cell <= 30 ? scaled(23) : cell - 7, cell <= 30 ? scaled(29) : cell - 1};
  const uint32_t colors[] = {0x7160FB, 0x444963, 0x444963, 0x444963};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t* tile = lv_obj_create(m);
    lv_obj_remove_style_all(tile);
    lv_obj_set_pos(tile, x[i], y[i]);
    lv_obj_set_size(tile, w[i], h[i]);
    lv_obj_set_style_radius(tile, cell / 3, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(colors[i]), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    if (i == 0) {
      lv_obj_set_style_bg_grad_color(tile, lv_color_hex(0x1FA4F3), 0);
      lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_HOR, 0);
    }
  }
}

// Original Tactula connecting indicator from deck_sys.cpp::buildConnecting.
// Both devices use this exact LVGL spinner, timing, arc widths and colours;
// only physical dimensions and font sizes scale for the larger display.
inline void buildConnecting(lv_obj_t* root, const String& ssid,
                            bool largeScreen = false) {
    const int scale = largeScreen ? 2 : 1;
    // Keep boot and normal connecting screens in the same Tactula palette.
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0C1019), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    if (largeScreen) {
      logoMark(root, 30);
      text(root, "tactula", &lv_font_montserrat_40, TITLE);
      gap(root, 6 * scale);
    } else {
      logoMark(root, 30);
      text(root, "tactula", &deck_font_28, TITLE);
    }
    lv_obj_t* spinner = lv_spinner_create(root);
    lv_obj_set_size(spinner, largeScreen ? 56 * scale : 48, largeScreen ? 56 * scale : 48);
    lv_spinner_set_anim_params(spinner, 1100, 240);
    lv_obj_set_style_arc_width(spinner, 5 * scale, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 5 * scale, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(ACCENT), LV_PART_INDICATOR);
    gap(root, 8 * scale);
    text(root, "Connecting to Wi-Fi",
         largeScreen ? &lv_font_montserrat_40 : &deck_font_20, TITLE);
    auto* network = text(root, ssid.c_str(),
         largeScreen ? &deck_font_28 : &deck_font_14, 0x48A5FF);
    lv_obj_set_width(network, lv_pct(90));
    lv_label_set_long_mode(network, LV_LABEL_LONG_MODE_DOTS);
    text(root, "Establishing connection...", largeScreen ? &deck_font_20 : &deck_font_14, DIM);
}

// Design faithfully extracted from deck_sys.cpp::buildApSetup. Large-screen
// adaptation uses the same elements, order, colours and wording. No divergent
// platform-specific QR setup screen should be introduced.
inline void buildApSetup(lv_obj_t* root, bool landscape,
                         const String& ssid, const String& password,
                         const String& portalIp, bool largeScreen = false) {
    const int scale = largeScreen ? 2 : 1;
    const lv_font_t* titleFont = largeScreen ? &lv_font_montserrat_40 : &deck_font_28;
    const lv_font_t* stepFont = largeScreen ? &deck_font_28 : &deck_font_14;
    const lv_font_t* infoFont = largeScreen ? &deck_font_28 : &deck_font_20;
    const lv_font_t* noteFont = largeScreen ? &deck_font_20 : &deck_font_14;

    lv_obj_t* wrap = box(root, landscape ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN,
                         (landscape ? 28 : 16) * scale);
    lv_obj_t* qrBox = lv_obj_create(wrap);
    lv_obj_remove_style_all(qrBox);
    lv_obj_set_size(qrBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(qrBox, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qrBox, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qrBox, 14 * scale, 0);
    lv_obj_set_style_pad_all(qrBox, 10 * scale, 0);
    lv_obj_t* qr = lv_qrcode_create(qrBox);
    lv_qrcode_set_size(qr, (landscape ? 150 : 132) * scale);
    lv_qrcode_set_dark_color(qr, lv_color_hex(BG));
    lv_qrcode_set_light_color(qr, lv_color_white());
    const String payload = "WIFI:T:WPA;S:" + ssid + ";P:" + password + ";;";
    lv_qrcode_update(qr, payload.c_str(), payload.length());

    lv_obj_t* col = box(wrap, LV_FLEX_FLOW_COLUMN, 2 * scale);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER,
                          landscape ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    text(col, "WiFi setup", titleFont, TITLE);
    gap(col, 8 * scale);
    text(col, "1. Join this network", stepFont, DIM);
    text(col, ssid.c_str(), infoFont, ACCENT);
    const String passwordLine = "Password  " + password;
    text(col, passwordLine.c_str(), noteFont, TEXT);
    gap(col, 8 * scale);
    text(col, "2. Open in a browser", stepFont, DIM);
    text(col, portalIp.c_str(), infoFont, ACCENT);
    gap(col, 8 * scale);
    text(col, "Or scan the code with a phone.", noteFont, DIM);
}
} // namespace tactula_setup
