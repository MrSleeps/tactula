// Configurable Home Assistant media-player remote. No default entity:
// Pages & Tiles requires the user to choose the media_player entity.
#include "deck_ui_int.h"

namespace deckui {
namespace {
constexpr int HA_MEDIA_BUTTONS = 6;
const char* const kMediaServices[HA_MEDIA_BUTTONS] = {
    "media_previous_track", "media_play_pause", "media_next_track",
    "volume_down", "media_stop", "volume_up"};
const char* const kMediaIcons[HA_MEDIA_BUTTONS] = {
    "skip-previous", "play", "skip-next", "volume-minus", "stop", "volume-plus"};
const char* const kMediaFallback[HA_MEDIA_BUTTONS] = {"|<", ">", ">|", "-", "[]", "+"};

// Prefer the configured MDI font, but keep usable symbols without it.
void mediaGlyph(lv_obj_t* label, const char* name, const char* fallback, int px) {
  const uint32_t cp = iconCodepoint(String(name));
  const lv_font_t* font = cp ? iconFont(px, cp) : nullptr;
  if (cp && font) {
    char utf8[5];
    iconUtf8(cp, utf8);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, utf8);
  } else {
    lv_obj_set_style_text_font(label, &deck_font_20, 0);
    lv_label_set_text(label, fallback);
  }
}

void haMediaPress(lv_event_t* ev) {
  const size_t key = (size_t)(uintptr_t)lv_event_get_user_data(ev);
  const size_t tile = key / HA_MEDIA_BUTTONS;
  const size_t button = key % HA_MEDIA_BUTTONS;
  if (tile >= s_tiles.size() || button >= HA_MEDIA_BUTTONS) return;
  TileRt& r = s_tiles[tile];
  if (!r.def || r.def->type != TILE_HA_MEDIA) return;
  if (haLink() != HA_CONNECTED) {
    deckToastShow("Home Assistant is offline", 1800);
    return;
  }
  HaEntity state;
  if (!haGetEntity(r.def->entity, state) || !state.known ||
      state.state == "unavailable" || state.state == "unknown") {
    deckToastShow("Media player unavailable", 1800);
    return;
  }
  hapticTap();
  audioClick();
  if (!haCallService("media_player", kMediaServices[button], r.def->entity, ""))
    deckToastShow("Home Assistant is busy, try again", 1800);
}
}  // namespace

void makeHaMedia(TileRt& r, lv_obj_t* parent, size_t index) {
  r.name = lv_label_create(parent);
  lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_font(r.name, &deck_font_20, 0);
  lv_label_set_text(r.name, r.def->label.length() ? r.def->label.c_str() : "Media player");

  r.sub = lv_label_create(parent);
  lv_label_set_long_mode(r.sub, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_font(r.sub, &deck_font_14, 0);
  lv_obj_set_style_text_color(r.sub, lv_color_hex(C_DIM), 0);
  lv_label_set_text(r.sub, "Connecting...");

  for (int i = 0; i < HA_MEDIA_BUTTONS; ++i) {
    lv_obj_t* b = r.haMediaButtons[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(b, 9, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_TILE_PRESSED), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(r.def->hasColor ? r.def->color : 0x48A5FF), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 2);
    lv_obj_t* label = r.haMediaIcons[i] = lv_label_create(b);
    mediaGlyph(label, kMediaIcons[i], kMediaFallback[i], 24);
    lv_obj_center(label);
    lv_obj_add_event_cb(b, haMediaPress, LV_EVENT_SHORT_CLICKED,
                        (void*)(uintptr_t)(index * HA_MEDIA_BUTTONS + i));
  }
}

void fitHaMedia(TileRt& r, int cw, int ch) {
  if (cw <= 0 || ch <= 0) return;
  const int labelH = 20;
  lv_obj_set_pos(r.name, 0, 0);
  lv_obj_set_size(r.name, cw, labelH);
  lv_obj_set_pos(r.sub, 0, labelH + 2);
  lv_obj_set_size(r.sub, cw, 18);
  const int y0 = labelH + 24;
  const int gap = 5;
  const int btnW = std::max(25, (cw - gap * 2) / 3);
  const int btnH = std::max(24, (ch - y0 - gap) / 2);
  for (int i = 0; i < HA_MEDIA_BUTTONS; ++i) {
    lv_obj_t* b = r.haMediaButtons[i];
    if (!b) continue;
    lv_obj_set_pos(b, (i % 3) * (btnW + gap), y0 + (i / 3) * (btnH + gap));
    lv_obj_set_size(b, btnW, btnH);
    if (r.haMediaIcons[i]) lv_obj_center(r.haMediaIcons[i]);
  }
}

void applyHaMedia(TileRt& r, const HaEntity* entity) {
  const bool available = entity && entity->known && entity->state != "unavailable" &&
                         entity->state != "unknown";
  // A customised label always wins; otherwise use the current track when
  // available and fall back to the media player's friendly name.
  if (!r.def->label.length()) {
    const String name = available && entity->mediaTitle.length() ? entity->mediaTitle :
                        available && entity->name.length() ? entity->name : "Media player";
    lv_label_set_text(r.name, name.c_str());
  }
  String status = available ? entity->state : "Unavailable";
  if (available && entity->mediaArtist.length()) {
    status = entity->mediaArtist;
  }
  if (available && isfinite(entity->mediaPosition) &&
      isfinite(entity->mediaDuration) && entity->mediaDuration > 0) {
    const unsigned pos = (unsigned)max(0.0f, entity->mediaPosition);
    const unsigned duration = (unsigned)entity->mediaDuration;
    char time[40];
    snprintf(time, sizeof(time), "%u:%02u / %u:%02u",
             pos / 60, pos % 60, duration / 60, duration % 60);
    status += "  ";
    status += time;
  } else if (available && entity->level >= 0) {
    status += "  |  ";
    status += String(entity->level);
    status += "%";
  }
  lv_label_set_text(r.sub, status.c_str());
  if (r.haMediaIcons[1])
    mediaGlyph(r.haMediaIcons[1], available && entity->state == "playing" ? "pause" : "play",
               available && entity->state == "playing" ? "||" : ">", 24);
  for (lv_obj_t* b : r.haMediaButtons) {
    if (!b) continue;
    if (available && haLink() == HA_CONNECTED) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else lv_obj_add_state(b, LV_STATE_DISABLED);
  }
  markPageStale(r.page);
}

}  // namespace deckui
