// Configurable Home Assistant media-player remote. No default entity:
// Pages & Tiles requires the user to choose the media_player entity.
#include "deck_ui_int.h"
#include <cctype>
#include <atomic>

namespace deckui {
namespace {
// Media metadata may contain typographic punctuation not included in the
// compact bitmap fonts. Keep the original UTF-8 and use a same-size glyph
// alias only if the native font has no glyph. No extra bitmap or heap cost.
uint32_t punctuationAlias(uint32_t cp) {
  switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x201B:
    case 0x2032: case 0x02BC: return 0x27; // apostrophe
    case 0x201C: case 0x201D: case 0x201E: case 0x201F:
    case 0x2033: return 0x22; // quotation mark
    case 0x2010: case 0x2011: case 0x2012: case 0x2013:
    case 0x2014: case 0x2015: case 0x2212: return 0x2D; // dash/minus
    case 0x202F: case 0x2009: case 0x2007: return 0x20; // spaces
    default: return cp;
  }
}

bool mediaGlyphDescriptor(const lv_font_t* font, lv_font_glyph_dsc_t* out,
                          uint32_t cp, uint32_t next) {
  const auto* native = static_cast<const lv_font_t*>(font->user_data);
  const bool found = native->get_glyph_dsc(native, out, cp, next);
  if (found && !out->is_placeholder) return true;
  const uint32_t alias = punctuationAlias(cp);
  if (alias == cp) return found;
  *out = {};
  return native->get_glyph_dsc(native, out, alias, punctuationAlias(next));
}

lv_font_t makeMediaTextFont(const lv_font_t& native) {
  lv_font_t font = native;
  font.user_data = const_cast<lv_font_t*>(&native);
  font.get_glyph_dsc = mediaGlyphDescriptor;
  // Keep native bitmap callbacks and descriptor; LVGL resolves the copy
  // using the same glyph indices and bitmap data.
  return font;
}

const lv_font_t* mediaTextFont(const lv_font_t* font) {
  static const lv_font_t f14 = makeMediaTextFont(deck_font_14);
  static const lv_font_t f20 = makeMediaTextFont(deck_font_20);
  static const lv_font_t f28 = makeMediaTextFont(deck_font_28);
  static const lv_font_t f40 = makeMediaTextFont(lv_font_montserrat_40);
  if (font == &deck_font_14) return &f14;
  if (font == &deck_font_20) return &f20;
  if (font == &deck_font_28) return &f28;
  if (font == &lv_font_montserrat_40) return &f40;
  return font;
}
constexpr int HA_MEDIA_BUTTONS = 6;
constexpr int PLAY_PAUSE = 1;
constexpr int VOLUME_DOWN = 3;
constexpr int VOLUME_UP = 5;
// Stable Home Assistant MediaPlayerEntityFeature bit values.
constexpr int MEDIA_PAUSE = 1;
constexpr int MEDIA_SEEK = 2;
constexpr int MEDIA_MUTE = 8;
constexpr int MEDIA_SHUFFLE = 32768;
constexpr int MEDIA_REPEAT = 262144;
constexpr int MEDIA_SELECT_SOURCE = 2048;
constexpr int MEDIA_VOLUME_SET = 4;
constexpr int MEDIA_PREVIOUS = 16;
constexpr int MEDIA_NEXT = 32;
constexpr int MEDIA_STOP = 4096;
constexpr int MEDIA_PLAY = 16384;
constexpr int MEDIA_VOLUME_STEP = 1024;
constexpr uint8_t ALL_CONTROLS = (1u << HA_MEDIA_BUTTONS) - 1;
const char* const kMediaServices[HA_MEDIA_BUTTONS] = {
    "media_previous_track", "media_play_pause", "media_next_track",
    "volume_down", "media_stop", "volume_up"};
const char* const kMediaIcons[HA_MEDIA_BUTTONS] = {
    "skip-previous", "play", "skip-next", "volume-minus", "stop", "volume-plus"};
const char* const kMediaFallback[HA_MEDIA_BUTTONS] = {"|<", ">", ">|", "-", "[]", "+"};
struct TvKey { const char* data; const char* icon; const char* fallback; };
const TvKey kTvKeys[HA_MEDIA_BUTTONS] = {
    {"{\"button\":\"LEFT\"}", "chevron-left", "<"},
    {"{\"button\":\"ENTER\"}", "", "OK"},
    {"{\"button\":\"RIGHT\"}", "chevron-right", ">"},
    {"{\"button\":\"VOLUMEDOWN\"}", "volume-minus", "-"},
    {"{\"button\":\"BACK\"}", "arrow-left", "<"},
    {"{\"button\":\"VOLUMEUP\"}", "volume-plus", "+"}};

struct MediaSheet {
  lv_obj_t* title;
  lv_obj_t* detail;
  lv_obj_t* volume;
  lv_obj_t* volumeSlider;
  lv_obj_t* volumeIcon;
  lv_obj_t* buttons[HA_MEDIA_BUTTONS];
  lv_obj_t* play;
  lv_obj_t* badge;
  lv_obj_t* artwork;
  lv_obj_t* album;
  lv_obj_t* progress;
  lv_obj_t* time;
  lv_obj_t* duration;
  lv_obj_t* state;
  lv_obj_t* extra[3];
  lv_obj_t* extraGlyph[3];
  lv_obj_t* sources;
  lv_obj_t* sourceLabel;
  lv_obj_t* volumePanel;
  lv_obj_t* idle;
  String sourceOptions;
  int width, height, header, tile;
  bool large;
  int iconPx;
  uint32_t accent;
};
MediaSheet s_mediaSheet = {};
// Bounded shared URL cache, one worker job. No network or JPEG decode on LVGL task.
#if defined(BOARD_IS_JC8012P4A1C)
constexpr int ART_SIDE = 320;
#else
constexpr int ART_SIDE = 128;
#endif
struct ArtJob { String url; uint16_t* pixels = nullptr; };
ArtJob artJob;
std::atomic<bool> artBusy{false}, artDone{false};
#if defined(BOARD_IS_JC8012P4A1C)
constexpr int ART_SLOTS = 3;
#else
constexpr int ART_SLOTS = 2;
#endif
struct ArtSlot {
  String url;
  uint16_t* pixels = nullptr;
  lv_image_dsc_t image = {};
  uint32_t used = 0;
};
ArtSlot artCache[ART_SLOTS];
struct ArtRetry { String url; uint32_t at = 0; };
ArtRetry artRetry[8];
unsigned artRetryNext = 0;
ArtSlot* findArt(const String& url) {
  if (!url.length()) return nullptr;
  for (auto& slot : artCache) if (slot.pixels && slot.url == url) return &slot;
  return nullptr;
}
bool retryAllowed(const String& url, uint32_t now) {
  for (auto& retry : artRetry)
    if (retry.url == url && now - retry.at < 30000) return false;
  return true;
}
void recordAttempt(const String& url, uint32_t now) {
  for (auto& retry : artRetry) if (retry.url == url) { retry.at = now; return; }
  auto& retry = artRetry[artRetryNext++ % 8];
  retry.url = url; retry.at = now;
}
void artWorker() {
  uint8_t* jpeg = nullptr; size_t bytes = 0;
  if (haMediaArtwork(artJob.url, jpeg, bytes)) {
    auto* pixels = (uint16_t*)heap_caps_malloc(ART_SIDE * ART_SIDE * 2, MALLOC_CAP_SPIRAM);
    if (pixels && wallDecodeArtwork(jpeg, bytes, ART_SIDE, ART_SIDE, pixels)) artJob.pixels = pixels;
    else free(pixels);
    free(jpeg);
  }
  artDone.store(true, std::memory_order_release);
}
void hideArtwork(lv_obj_t* image) {
  if (!image) return;
  showObj(image, false);
  lv_image_set_src(image, nullptr);
}
void evictArt(ArtSlot& slot) {
  if (!slot.pixels) return;
  for (auto& r : s_tiles) if (r.haMediaArtwork && lv_image_get_src(r.haMediaArtwork) == &slot.image) {
    hideArtwork(r.haMediaArtwork);
    if (r.icon) showObj(r.icon, true);
    markPageStale(r.page);
  }
  if (s_mediaSheet.artwork && lv_image_get_src(s_mediaSheet.artwork) == &slot.image) {
    hideArtwork(s_mediaSheet.artwork);
    if (s_mediaSheet.badge) showObj(s_mediaSheet.badge, true);
  }
  lv_image_cache_drop(&slot.image);
  free(slot.pixels); slot.pixels = nullptr; slot.url = "";
}
void setArtwork(lv_obj_t* image, lv_obj_t* fallback, const String& url, int side) {
  if (!image || !fallback) return;
  auto* slot = side >= 32 ? findArt(url) : nullptr;
  if (!slot) {
    if (lv_image_get_src(image)) hideArtwork(image);
    showObj(fallback, true);
    return;
  }
  slot->used = millis();
  if (lv_image_get_src(image) != &slot->image) lv_image_set_src(image, &slot->image);
  lv_image_set_scale(image, std::max(1, side * 256 / ART_SIDE));
  lv_obj_center(image);
  showObj(image, true); showObj(fallback, false);
}


bool mediaAvailable(const HaEntity* e) {
  return e && e->known && e->state != "unavailable" && e->state != "unknown";
}

String mediaName(const TileRt& r, const HaEntity* e) {
  if (r.def->label.length()) return r.def->label;
  return e && e->name.length() ? e->name : String("Media player");
}

// The app/input is useful on TVs that report no title for the current programme.
const String* mediaAppOrSource(const HaEntity* e) {
  if (!mediaAvailable(e) || e->state == "off" || e->state == "standby") return nullptr;
  if (e->mediaApp.length()) return &e->mediaApp;
  return e->mediaSource.length() ? &e->mediaSource : nullptr;
}

const String* assumedPlaybackApp(const HaEntity* e) {
  return mediaAvailable(e) && e->state == "on" ? mediaAppOrSource(e) : nullptr;
}

bool mediaUsesWebOs(const TileRt& r, const HaEntity* e) {
  // Registry identification takes precedence; old configurations remain a
  // fallback if HA cannot provide registry metadata.
  return e && e->mediaWebOsPlatform >= 0 ? e->mediaWebOsPlatform == 1 : r.def->mediaWebOs;
}

bool mediaPlaying(const TileRt& r, const HaEntity* e) {
  if (!mediaAvailable(e)) return false;
  if (e->state == "playing") return true;
  // Only infer playback for explicitly configured webOS remotes.
  const String* app = mediaUsesWebOs(r,e) ? assumedPlaybackApp(e) : nullptr;
  return app && *app != r.haMediaPausedApp;
}

bool canSetMediaVolume(const HaEntity* e) {
  return mediaAvailable(e) && e->level >= 0 &&
         (e->mediaFeatures < 0 || (e->mediaFeatures & MEDIA_VOLUME_SET));
}

bool mediaSupports(const HaEntity* e, int bits) {
  return mediaAvailable(e) && (e->mediaFeatures < 0 || (e->mediaFeatures & bits));
}

uint8_t mediaControls(const TileRt& r, const HaEntity* e) {
  // Keep the layout stable while disconnected; explicit remote keys are
  // independent of media_player feature flags.
  if (!mediaAvailable(e) || mediaUsesWebOs(r,e) || e->mediaFeatures < 0)
    return ALL_CONTROLS;
  uint8_t mask = 0;
  const int flags[] = {MEDIA_PREVIOUS, MEDIA_PLAY | MEDIA_PAUSE, MEDIA_NEXT,
                       MEDIA_VOLUME_STEP, MEDIA_STOP, MEDIA_VOLUME_STEP};
  for (int i = 0; i < HA_MEDIA_BUTTONS; ++i)
    if (mediaSupports(e, flags[i])) mask |= 1u << i;
  return mask;
}

bool mediaControlEnabled(const TileRt& r, const HaEntity* e, int control) {
  if (!mediaAvailable(e) || haLink() != HA_CONNECTED ||
      !(mediaControls(r, e) & (1u << control))) return false;
  if (mediaUsesWebOs(r,e) || e->mediaFeatures < 0) return true;
  if (control == PLAY_PAUSE)
    return mediaSupports(e, mediaPlaying(r, e) ? MEDIA_PAUSE : MEDIA_PLAY);
  return true;
}

String mediaHeadline(const HaEntity* e) {
  if (mediaAvailable(e) && e->state != "off" && e->state != "standby" && e->mediaTitle.length())
    return e->mediaTitle;
  const String* app = mediaAppOrSource(e);
  return app ? *app : String();
}

String mediaStatus(const HaEntity* e) {
  if (haLink() != HA_CONNECTED) return "Home Assistant offline";
  if (!mediaAvailable(e)) return "Unavailable";
  String status = e->state;
  if (status.length()) status.setCharAt(0, toupper(static_cast<unsigned char>(status[0])));
  const String* app = mediaAppOrSource(e);
  if (e->state != "off" && e->state != "standby" && e->mediaTitle.length()) {
    if (e->mediaArtist.length()) status += String("  ·  ") + e->mediaArtist;
    else if (app && *app != e->mediaTitle) status += String("  ·  ") + *app;
  }
  return status;
}

// Prefer the configured MDI font, but keep usable symbols without it.
void mediaGlyph(lv_obj_t* label, const char* name, const char* fallback, int px) {
  const uint32_t cp = *name ? iconCodepoint(String(name)) : 0;
  const lv_font_t* font = cp ? iconFont(px, cp) : nullptr;
  // MDI fonts have a finite number of size slots. Reuse a nearby existing
  // size before falling back to text when the cache is full.
  if (cp && !font) {
    const int alternatives[] = {px >= 32 ? 40 : 24, 28, 24, 20, 16, 40};
    for (int size : alternatives) {
      if (size == px) continue;
      font = iconFont(size, cp);
      if (font) break;
    }
  }
  if (cp && font) {
    char utf8[5];
    iconUtf8(cp, utf8);
    if (lv_obj_get_style_text_font(label, LV_PART_MAIN) != font)
      lv_obj_set_style_text_font(label, font, 0);
    setText(label, utf8);
  } else {
    const auto* fallbackFont = px >= 36 ? &deck_font_28 : px < 20 ? &deck_font_14 : &deck_font_20;
    if (lv_obj_get_style_text_font(label, LV_PART_MAIN) != fallbackFont)
      lv_obj_set_style_text_font(label, fallbackFont, 0);
    setText(label, fallback);
  }
}

void haMediaPress(lv_event_t* ev) {
  const size_t key = (size_t)(uintptr_t)lv_event_get_user_data(ev);
  const size_t tile = key / HA_MEDIA_BUTTONS;
  const size_t button = key % HA_MEDIA_BUTTONS;
  if (tile >= s_tiles.size() || button >= HA_MEDIA_BUTTONS) return;
  TileRt& r = s_tiles[tile];
  if (!r.def || r.def->type != TILE_HA_MEDIA || !tilePinAccess(r)) return;
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
  if (!mediaControlEnabled(r, &state, button)) {
    deckToastShow("Control not supported", 1800);
    return;
  }
  hapticTap();
  audioClick();
  const String* app = mediaUsesWebOs(r,&state) ? assumedPlaybackApp(&state) : nullptr;
  const bool pause = mediaPlaying(r, &state);
  bool sent;
  if (mediaUsesWebOs(r,&state)) {
    const char* data = kTvKeys[button].data;
    if (button == PLAY_PAUSE && (app || state.state == "playing" || state.state == "paused"))
      data = pause ? "{\"button\":\"PAUSE\"}" : "{\"button\":\"PLAY\"}";
    sent = haCallService("webostv", "button", r.def->entity, data);
  } else {
    const char* service = button == PLAY_PAUSE && state.mediaFeatures >= 0
        ? (pause ? "media_pause" : "media_play") : kMediaServices[button];
    sent = haCallService("media_player", service, r.def->entity, "");
  }
  if (!sent) {
    deckToastShow("Home Assistant is busy, try again", 1800);
  } else if (button == PLAY_PAUSE && app) {
    r.haMediaPausedApp = pause ? *app : String();
    applyHaMedia(r, &state);
    refreshModal();
  }
}

void mediaControlGlyph(lv_obj_t* label, const TileRt& r, int control, int px,
                       const HaEntity* e = nullptr) {
  if (control == PLAY_PAUSE && mediaPlaying(r, e)) {
    mediaGlyph(label, "pause", "||", px);
  } else if (mediaUsesWebOs(r,e) &&
             (control != PLAY_PAUSE || (!assumedPlaybackApp(e) && (!e || e->state != "paused")))) {
    const auto& key = kTvKeys[control];
    mediaGlyph(label, key.icon, key.fallback, px);
  } else {
    mediaGlyph(label, kMediaIcons[control], kMediaFallback[control], px);
  }
}

void onMediaVolume(lv_event_t* event) {
  const size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
  if (index >= s_tiles.size() || s_tiles[index].def->type != TILE_HA_MEDIA) return;
  if (!tilePinAccess(s_tiles[index], false)) return;
  const auto& id = s_tiles[index].def->entity;
  const auto code = lv_event_get_code(event);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    releaseLevel(id);
    return;
  }
  HaEntity e;
  if (haLink() != HA_CONNECTED || !haGetEntity(id, e) || !canSetMediaVolume(&e)) return;
  const float volume = lv_slider_get_value(lv_event_get_target_obj(event)) / 100.0f;
  if (code == LV_EVENT_PRESSED) holdLevel(id, volume);
  else if (code == LV_EVENT_VALUE_CHANGED) setWish(id, true, volume, true);
}

lv_obj_t* makeMediaVolume(lv_obj_t* parent, size_t index, uint32_t accent) {
  auto* slider = lv_slider_create(parent);
  lv_slider_set_range(slider, 0, 100);
  lv_obj_set_ext_click_area(slider, 9);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(C_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(accent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(C_TEXT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);
  for (auto code : {LV_EVENT_PRESSED, LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(slider, onMediaVolume, code, (void*)(uintptr_t)index);
  showObj(slider, false);
  return slider;
}

void syncMediaVolume(lv_obj_t* slider, lv_obj_t* label, const TileRt& r, const HaEntity* e) {
  char text[24] = "Volume";
  if (canSetMediaVolume(e)) {
    const int level = constrain((int)lroundf(wishFrac(r.def->entity, *e) * 100), 0, 100);
    if (!lv_obj_has_state(slider, LV_STATE_PRESSED))
      lv_slider_set_value(slider, level, LV_ANIM_OFF);
    snprintf(text, sizeof(text), "%d%%", level);
  }
  if (haLink() == HA_CONNECTED && canSetMediaVolume(e)) lv_obj_remove_state(slider, LV_STATE_DISABLED);
  else lv_obj_add_state(slider, LV_STATE_DISABLED);
  setText(label, text);
}

void mediaAppBadge(lv_obj_t* label, const HaEntity* entity) {
  const String* app = mediaAppOrSource(entity);
  if (!app && entity && entity->name.equalsIgnoreCase("Spotify")) {
    mediaGlyph(label, "spotify", "S", 24);
    return;
  }
  if (!app || *app == "Music Assistant Queue") {
    mediaGlyph(label, entity && entity->deviceClass == "tv" ? "television" : "music", "♪", 24);
    return;
  }
  const char* brand = "";
  const char* fallback = "TV";
  if (app->equalsIgnoreCase("Netflix")) { brand = "netflix"; fallback = "N"; }
  else if (app->equalsIgnoreCase("Plex")) { brand = "plex"; fallback = "P"; }
  else if (app->equalsIgnoreCase("YouTube")) { brand = "youtube"; fallback = "YT"; }
  else if (app->equalsIgnoreCase("Disney+")) fallback = "D+";
  else if (app->equalsIgnoreCase("Prime Video") || app->equalsIgnoreCase("Amazon Prime Video")) fallback = "PV";
  else if (app->equalsIgnoreCase("BBC iPlayer")) fallback = "iP";
  else if (app->equalsIgnoreCase("ITVX") || app->equalsIgnoreCase("ITV")) fallback = "ITV";
  else if (app->equalsIgnoreCase("Spotify")) { brand = "spotify"; fallback = "S"; }
  else {
    const unsigned char first = (*app)[0];
    if (first < 128 && isalnum(first)) {
      const char initial[] = {static_cast<char>(toupper(first)), 0};
      mediaGlyph(label, "", initial, 24);
      return;
    }
  }
  mediaGlyph(label, brand, fallback, 24);
}

void mediaOpen(lv_event_t* event) {
  const size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
  if (index < s_tiles.size() && s_tiles[index].def->type == TILE_HA_MEDIA) openModal(index);
}

lv_obj_t* mediaLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t colour) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, mediaTextFont(font), 0);
  lv_obj_set_style_text_color(label, lv_color_hex(colour), 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
  return label;
}

lv_obj_t* cinematicMediaButton(lv_obj_t* parent, size_t index, int control, uint32_t accent) {
  const bool primary = control == PLAY_PAUSE;
  lv_obj_t* button = lv_obj_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_remove_flag(button, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE));
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(button, primary ? LV_RADIUS_CIRCLE : 14, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(primary ? accent : C_TILE_PRESSED), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(primary ? C_TILE_PRESSED : C_BUTTON), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, primary ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  if (primary) {
    cinematicMediaButtonGradient(button, accent);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_40, 0);
  }
  lv_obj_set_style_text_color(button, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_text_color(button, lv_color_hex(C_TEXT), LV_STATE_PRESSED);
  lv_obj_set_style_opa(button, LV_OPA_50, LV_STATE_DISABLED);
  lv_obj_add_event_cb(button, haMediaPress, LV_EVENT_SHORT_CLICKED,
                      (void*)(uintptr_t)(index * HA_MEDIA_BUTTONS + control));
  return button;
}

void mediaLayoutLabel(lv_obj_t* label, const lv_font_t* font, int x, int y, int width) {
  lv_obj_set_style_text_font(label, mediaTextFont(font), 0);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_size(label, width, lineH(font));
}
bool mediaHasProgress(const HaEntity* e);
void onMediaExtra(lv_event_t* ev);
uint8_t mediaExtras(const TileRt& r, const HaEntity* e) {
  if (!mediaAvailable(e)) return 0;
  return (mediaSupports(e, MEDIA_SHUFFLE) && e->mediaShuffle >= 0 ? 1 : 0) |
         (mediaSupports(e, MEDIA_REPEAT) && e->mediaRepeat.length() ? 2 : 0) |
         (mediaSupports(e, MEDIA_MUTE) && e->mediaMute >= 0 ? 4 : 0);
}
void syncTileExtras(TileRt& r, const HaEntity* e) {
  if (!r.haMediaExtra[0]) return;
  const uint8_t mask = mediaExtras(r,e);
  const char* icons[] = {"shuffle", (mask & 2) && e->mediaRepeat == "one" ? "repeat-once" : "repeat", (mask & 4) && e->mediaMute ? "volume-off" : "volume-high"};
  const char* fallback[] = {"S", "R", "M"};
  const bool active[] = {bool((mask & 1) && e->mediaShuffle), bool((mask & 2) && e->mediaRepeat != "off"), bool((mask & 4) && e->mediaMute)};
  for (int j=0;j<3;++j) {
    mediaGlyph(r.haMediaExtraGlyph[j], icons[j], fallback[j], j==2 ? 24 : r.iconPx ? r.iconPx : 24);
    lv_obj_set_style_text_color(r.haMediaExtraGlyph[j], lv_color_hex(active[j] ? tileAccent(*r.def) : C_TEXT), 0);
    lv_obj_center(r.haMediaExtraGlyph[j]);
    if ((mask & (1u<<j)) && haLink()==HA_CONNECTED) lv_obj_remove_state(r.haMediaExtra[j], LV_STATE_DISABLED);
    else lv_obj_add_state(r.haMediaExtra[j], LV_STATE_DISABLED);
  }
}
float mediaPositionNow(const HaEntity* e) {
  float pos = e->mediaPosition;
  if (e->state == "playing" && e->mediaPositionAt)
    pos += (uint32_t)(millis() - e->mediaPositionAt) / 1000.0f;
  return std::clamp(pos, 0.0f, e->mediaDuration);
}
void updateTileProgress(TileRt& r, const HaEntity* e) {
  if (!r.haMediaProgress || !mediaHasProgress(e) ||
      lv_obj_has_flag(r.haMediaProgress, LV_OBJ_FLAG_HIDDEN)) return;
  const unsigned p = (unsigned)mediaPositionNow(e), d = (unsigned)e->mediaDuration;
  const int value = (int)(1000.0f * p / e->mediaDuration);
  if (lv_bar_get_value(r.haMediaProgress) != value)
    lv_bar_set_value(r.haMediaProgress, value, LV_ANIM_OFF);
  char buf[48];
  snprintf(buf, sizeof(buf), "%u:%02u / %u:%02u", p/60,p%60,d/60,d%60);
  setText(r.haMediaTime, buf);
}
}  // namespace

void makeHaMedia(TileRt& r, lv_obj_t* parent, size_t index) {
  r.name = lv_label_create(parent);
  lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_font(r.name, mediaTextFont(&deck_font_20), 0);
  lv_label_set_text(r.name, r.def->label.length() ? r.def->label.c_str() : "Media player");

  r.sub = lv_label_create(parent);
  lv_label_set_long_mode(r.sub, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_font(r.sub, mediaTextFont(&deck_font_14), 0);
  lv_obj_set_style_text_color(r.sub, lv_color_hex(C_DIM), 0);
  lv_label_set_text(r.sub, "Connecting...");

  if (themeUsesModernLayout(s_theme)) {
    lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(parent, mediaOpen, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)index);
    const uint32_t accent = tileAccent(*r.def);
    r.chip = lv_obj_create(parent);
    lv_obj_remove_style_all(r.chip);
    lv_obj_remove_flag(r.chip, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_radius(r.chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(r.chip, LV_OPA_COVER, 0);
    styleIconChip(r.chip, accent);
    r.icon = lv_label_create(r.chip);
    lv_obj_center(r.icon);
    r.haMediaArtwork = lv_image_create(r.chip);
    lv_obj_remove_flag(r.haMediaArtwork, LV_OBJ_FLAG_CLICKABLE);
    showObj(r.haMediaArtwork, false);
    for (int i = 0; i < HA_MEDIA_BUTTONS; ++i) {
      auto* b = r.haMediaButtons[i] = cinematicMediaButton(parent, index, i, accent);
      r.haMediaIcons[i] = lv_label_create(b);
      lv_obj_center(r.haMediaIcons[i]);
    }
    r.haMediaAlbum = mediaLabel(parent, &deck_font_14, C_DIM);
    showObj(r.haMediaAlbum, false);
    for (int j=0;j<3;++j) {
      auto* b = r.haMediaExtra[j] = lv_obj_create(parent);
      lv_obj_remove_style_all(b);
      lv_obj_remove_flag(b, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE));
      lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_radius(b, 10, 0);
      lv_obj_set_style_bg_color(b, lv_color_hex(C_BUTTON), LV_STATE_PRESSED);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
      r.haMediaExtraGlyph[j] = lv_label_create(b);
      lv_obj_center(r.haMediaExtraGlyph[j]);
      lv_obj_add_event_cb(b, onMediaExtra, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)(index*3+j));
      showObj(b,false);
    }
    r.haMediaProgress = lv_bar_create(parent);
    lv_obj_remove_flag(r.haMediaProgress, LV_OBJ_FLAG_CLICKABLE);
    lv_bar_set_range(r.haMediaProgress, 0, 1000);
    lv_obj_set_style_bg_color(r.haMediaProgress, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r.haMediaProgress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(r.haMediaProgress, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_radius(r.haMediaProgress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(r.haMediaProgress, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    r.haMediaTime = mediaLabel(parent, &deck_font_14, C_DIM);
    showObj(r.haMediaProgress, false);
    showObj(r.haMediaTime, false);
    r.haMediaVolumePanel = lv_obj_create(parent);
    lv_obj_remove_style_all(r.haMediaVolumePanel);
    lv_obj_remove_flag(r.haMediaVolumePanel, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_radius(r.haMediaVolumePanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(r.haMediaVolumePanel, lv_color_hex(C_BUTTON), 0);
    lv_obj_set_style_bg_opa(r.haMediaVolumePanel, LV_OPA_TRANSP, 0);
    r.haMediaVolume = makeMediaVolume(r.haMediaVolumePanel, index, accent);
    r.haMediaVolumeText = mediaLabel(r.haMediaVolumePanel, &deck_font_14, C_TEXT);
    r.haMediaVolumeIcon = lv_label_create(r.haMediaVolumePanel);
    lv_obj_remove_flag(r.haMediaVolumeIcon, LV_OBJ_FLAG_CLICKABLE);
    showObj(r.haMediaVolumePanel, false);
    showObj(r.haMediaVolumeText, false);
    return;
  }

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
    mediaControlGlyph(label, r, i, 24);
    lv_obj_center(label);
    lv_obj_add_event_cb(b, haMediaPress, LV_EVENT_SHORT_CLICKED,
                        (void*)(uintptr_t)(index * HA_MEDIA_BUTTONS + i));
  }
}

void fitHaMedia(TileRt& r, int cw, int ch) {
  if (cw <= 0 || ch <= 0) return;
  if (themeUsesModernLayout(s_theme)) {
    // Use the actual allocated grid span (half-cell units) and pixel budget.
    // A dense P4 grid and a small S3 grid must not force unreadable controls.
    const auto& p = r.def->pos[cw > 0 && logicalW() > logicalH() ? 0 : 1];
    const int gw = r.def->parent.length() ? r.def->groupW : p.w;
    const int gh = r.def->parent.length() ? r.def->groupH : p.h;
    const bool compact = (gh <= 2 && gw <= 4) || cw < 145 || ch < 65;
    const bool expanded = !compact && gw >= 6 && gh >= 4 && cw >= 240 && ch >= 155;
    const bool tall = !compact && ch > cw * 5 / 4 && ch >= 190;
    const bool square = !compact && !tall && cw < ch * 3 / 2;
    const bool shortRow = !compact && ch < 135;
    const int gap = std::clamp(std::min(cw,ch)/18, 3, 12);
    const int margin = std::min(gap, cw >= 400 && ch >= 160 ? 12 : 8);
    const int W = std::max(1,cw-2*margin), H = std::max(1,ch-2*margin);
    const auto* tf = W >= 420 && H >= 160 ? &lv_font_montserrat_40 : H >= 100 && W >= 180 ? &deck_font_20 : &deck_font_14;
    const auto* sf = W >= 420 && H >= 160 ? &deck_font_20 : &deck_font_14;
    const int th = lineH(tf), sh = lineH(sf);
    const auto* timeFont = W >= 360 && H >= 160 ? &deck_font_20 : &deck_font_14;
    lv_obj_set_style_text_color(r.sub, lv_color_hex(tileAccent(*r.def)), 0);
    const uint8_t mask = r.haMediaUnavailable ? 0 : r.haMediaControlMask;
    const bool play = mask & (1u<<PLAY_PAUSE);
    HaEntity e;
    const HaEntity* current = haGetEntity(r.def->entity,e) ? &e : nullptr;
    for (auto* button : r.haMediaButtons) showObj(button,false);
    for (auto* button : r.haMediaExtra) showObj(button,false);
    showObj(r.name,false); showObj(r.sub,false); showObj(r.haMediaAlbum,false);
    showObj(r.haMediaProgress,false); showObj(r.haMediaTime,false);
    showObj(r.haMediaVolumePanel,false);
    showObj(r.haMediaVolume,false); showObj(r.haMediaVolumeText,false);
    auto cover = [&](int x,int y,int side) {
      showObj(r.chip,side>=16);
      lv_obj_set_pos(r.chip,x,y);
      lv_obj_set_size(r.chip,std::max(1,side),std::max(1,side));
      lv_obj_set_style_radius(r.chip,std::min(16,std::max(5,side/10)),0);
      lv_obj_set_style_clip_corner(r.chip,true,0);
      lv_obj_center(r.icon);
      setArtwork(r.haMediaArtwork,r.icon,current ? current->mediaPicture : String(),side);
    };
    auto label = [&](lv_obj_t* obj,const lv_font_t* font,int x,int y,int width,bool centre=false) {
      showObj(obj,width>=24);
      lv_obj_set_style_text_align(obj,centre ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT,0);
      mediaLayoutLabel(obj,font,x,y,std::max(1,width));
    };
    auto button = [&](int j,int x,int y,int side) {
      if (!(mask & (1u<<j)) || side<16) return;
      auto* b = r.haMediaButtons[j];
      showObj(b,true); lv_obj_set_pos(b,x,y); lv_obj_set_size(b,side,side);
      mediaControlGlyph(r.haMediaIcons[j],r,j,side>=60?40:side>=40?28:side>=28?24:16,current);
      lv_obj_center(r.haMediaIcons[j]);
    };
    auto bar = [&](int x,int y,int width,bool time) {
      if (!r.haMediaHasProgress || width<32) return;
      if (time) { label(r.haMediaTime,timeFont,x,y,width); y+=lineH(timeFont)+3; }
      showObj(r.haMediaProgress,true);
      lv_obj_set_pos(r.haMediaProgress,x,y);
      lv_obj_set_size(r.haMediaProgress,width, H>=140?6:4);
    };
    if (r.haMediaUnavailable) {
      const int art = std::max(16,std::min({H-4,W/3,100}));
      if (W >= 100 && W >= H) {
        cover(margin,margin+(H-art)/2,art);
        mediaGlyph(r.icon,current && current->deviceClass == "tv" ? "television" : "speaker", "--", std::min(48,art/2));
        lv_obj_center(r.icon);
        label(r.name,tf,margin+art+gap,margin+std::max(0,(H-th-sh)/2),W-art-gap);
        label(r.sub,sf,margin+art+gap,margin+std::max(0,(H-th-sh)/2)+th,W-art-gap);
      } else {
        cover(margin+(W-art)/2,margin+(H-art)/2,art);
        mediaGlyph(r.icon,"power-standby","--",std::min(40,art/2));
        lv_obj_center(r.icon);
      }
      lv_obj_set_style_text_color(r.sub,lv_color_hex(C_DIM),0);
      invalidateSwipeZones();
      return;
    }
    if (compact) {
      const bool horizontal = W>=H*3/2;
      if (horizontal) {
        const int bs=play?std::min(48,H-4):0;
        const int art=std::min(H-4,std::max(24,W/4));
        cover(margin,margin+(H-art)/2,art);
        const int x=margin+art+gap, width=W-art-gap-(play?bs+gap:0);
        const int y=margin+std::max(0,(H-th-sh-8)/2);
        label(r.name,tf,x,y,width);
        if (H>=th+sh+10) label(r.sub,sf,x,y+th,width);
        if (H>=th+sh+15) bar(x,y+th+sh+3,width,false);
        button(PLAY_PAUSE,margin+W-bs,margin+(H-bs)/2,bs);
      } else {
        const int bs=play?std::min(42,std::max(16,H/3)):0;
        const int art=std::min(W-4,H-bs-(play?gap:0));
        cover(margin+(W-art)/2,margin,art);
        button(PLAY_PAUSE,margin+(W-bs)/2,margin+H-bs,bs);
      }
      invalidateSwipeZones();
      return;
    }
    const bool vol = r.haMediaCanSetVolume && W>=120 && H>=95;
    const bool sideBySide = !tall && W >= 300 && W >= H * 17 / 10;
    const uint8_t em = expanded && !shortRow ? r.haMediaExtraMask : 0;
    int controls[6], count=0;
    if (em&1) controls[count++]=6;
    if (mask&1) controls[count++]=0;
    if (play) controls[count++]=PLAY_PAUSE;
    if (mask&4) controls[count++]=2;
    if (em&2) controls[count++]=7;
    if (!play && !count && (mask&(1u<<4))) controls[count++]=4;
    const int volumeH=vol? std::min(42,std::max(24,H/7)):0;
    const int art = sideBySide ? std::min({H-4,W*39/100,ART_SIDE}) :
        tall ? std::min(W-2*gap,std::max(16,H/3)) :
        std::min({std::max(16,H/2),std::max(16,W*40/100),ART_SIDE});
    const int tx=sideBySide?margin+art+gap*2:tall?margin:margin+art+gap;
    const int tw=tall?W:std::max(1,W-(tx-margin));
    const int controlX=sideBySide?tx:margin;
    const int controlW=sideBySide?tw:W;
    const int bs=count?std::max(1,std::min({expanded?68:52,std::max(16,H/4),(controlW-gap*(count-1))/count})):0;
    // Reserve a real separation between metadata/progress and transport.
    // On wide tiles the volume row stays aligned with the bottom of the art.
    const bool denseWide = sideBySide && logicalW() < 800;
    const int transportGap = count ? (denseWide ? 4 : sideBySide ? std::max(10,gap) : gap) : 0;
    const int volumeGap = vol && count ? (denseWide ? 4 : sideBySide ? std::max(10,gap) : gap) : 0;
    const int bottom = (count ? bs + transportGap : 0) + (vol ? volumeH + (count ? volumeGap : gap) : 0);
    const int metaH=std::max(0,H-bottom);
    const int ax=tall?margin+(W-art)/2:margin;
    cover(ax,margin+(sideBySide?(H-art)/2:tall?0:std::max(0,(metaH-art)/2)),art);
    const bool showAlbum=expanded && current && current->mediaAlbum.length() && metaH>=th+sh+lineH(&deck_font_14)+gap+(r.haMediaHasProgress?lineH(timeFont)+9:8);
    const bool showProgress=r.haMediaHasProgress;
    const int timeH=lineH(timeFont)+(denseWide?6:9);
    const int metadataH=th+sh+(showAlbum?lineH(&deck_font_14):0)+(showProgress?timeH:0)+gap;
    int y=tall?margin+art+gap:margin+std::max(0,(metaH-metadataH)/2);
    int metadataEnd = y;
    if (y+th<=margin+metaH) {
      label(r.name,tf,tx,y,tw,tall); y+=th;
      if (y+sh<=margin+metaH) { label(r.sub,sf,tx,y,tw,tall); y+=sh; }
      if (showAlbum && y+lineH(&deck_font_14)<=margin+metaH) {
        label(r.haMediaAlbum,&deck_font_14,tx,y,tw,tall); y+=lineH(&deck_font_14);
      }
      y+=gap/2;
      metadataEnd = y;
      if (showProgress && y+6<=margin+metaH) {
        const bool timestamps = y+timeH<=margin+metaH;
        bar(tx,y,tw,timestamps);
        metadataEnd = y + (timestamps ? timeH : 6);
      }
    }
    // Distribute leftover space evenly above and below the transport row,
    // instead of placing the play button flush against the progress track.
    const int volumeTop = margin+H-(vol?volumeH:0);
    // Centre between the actual progress track's lower edge and the
    // volume track's upper edge, not the top of its transparent row.
    const int lowerEdge = vol ? volumeTop+(volumeH-6)/2 : margin+H;
    // Keep a small clearance from the volume row, even on dense layouts.
    const int latest = std::max(margin,volumeTop-bs-(vol?4:0));
    const int earliest = std::min(latest,std::max(margin,metadataEnd+transportGap));
    const int balanced = (metadataEnd+lowerEdge-bs)/2;
    const int by = count ? std::clamp(balanced,earliest,latest) : earliest;
    // Control row lives beside art on wide tiles, below on square/portrait.
    int bx=controlX+(controlW-count*bs-std::max(0,count-1)*gap)/2;
    for(int j=0;j<count;++j) {
      int k=controls[j];
      if(k<6) button(k,bx,by,bs);
      else { auto* b=r.haMediaExtra[k-6]; showObj(b,true); lv_obj_set_pos(b,bx,by); lv_obj_set_size(b,bs,bs); }
      bx+=bs+gap;
    }
    if (vol) {
      const int vy=margin+H-volumeH;
      const bool mute=(r.haMediaExtraMask&4)!=0;
      const int speakerW=volumeH;
      const int panelW=std::max(1,controlW);
      const int labelW=panelW>=220?60:42;
      const auto* volumeFont = panelW>=250 && volumeH>=34 ? &deck_font_20 : &deck_font_14;
      showObj(r.haMediaVolumePanel,true);
      lv_obj_set_pos(r.haMediaVolumePanel,controlX,vy);
      lv_obj_set_size(r.haMediaVolumePanel,panelW,volumeH);
      label(r.haMediaVolumeText,volumeFont,panelW-labelW-4,(volumeH-lineH(volumeFont))/2,labelW);
      lv_obj_set_style_text_align(r.haMediaVolumeText,LV_TEXT_ALIGN_RIGHT,0);
      showObj(r.haMediaVolumeIcon,!mute);
      if (!mute) {
        mediaGlyph(r.haMediaVolumeIcon,"volume-high","V",volumeH>=32?24:16);
        lv_obj_align(r.haMediaVolumeIcon,LV_ALIGN_LEFT_MID,8,0);
      }
      showObj(r.haMediaVolume,true);
      lv_obj_set_pos(r.haMediaVolume,speakerW+gap,(volumeH-6)/2);
      lv_obj_set_size(r.haMediaVolume,std::max(1,panelW-speakerW-labelW-2*gap-4),6);
      if (mute) {
        auto* b=r.haMediaExtra[2];
        showObj(b,true);
        lv_obj_set_style_bg_color(b,lv_color_hex(C_BUTTON),0);
        lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);
        lv_obj_set_pos(b,controlX,vy);
        lv_obj_set_size(b,speakerW,volumeH);
      }
    }
    r.iconPx=bs>=60?40:bs>=40?28:bs>=28?24:16;
    syncTileExtras(r,current);
    updateTileProgress(r,current);
    invalidateSwipeZones();
    return;
  }
  const bool large = roomyWidget(cw, ch);
  const lv_font_t* title = large ? &deck_font_28 : &deck_font_20;
  const lv_font_t* detail = large ? &deck_font_20 : &deck_font_14;
  const int labelH = lv_font_get_line_height(title);
  const int detailH = lv_font_get_line_height(detail);
  const bool showTitle = ch >= labelH + 40;
  const bool showDetail = ch >= labelH + detailH + 50;
  showObj(r.name, showTitle);
  showObj(r.sub, showDetail);
  lv_obj_set_style_text_font(r.name, mediaTextFont(title), 0);
  lv_obj_set_style_text_font(r.sub, mediaTextFont(detail), 0);
  lv_obj_set_pos(r.name, 0, 0);
  lv_obj_set_size(r.name, cw, labelH);
  lv_obj_set_pos(r.sub, 0, labelH + 2);
  lv_obj_set_size(r.sub, cw, detailH);
  const int y0 = (showTitle ? labelH + 2 : 0) + (showDetail ? detailH + 4 : 0);
  const int gap = std::min(large ? 8 : 5, std::min(cw / 8, (ch - y0) / 4));
  const int btnW = std::max(1, (cw - gap * 2) / 3);
  const int btnH = std::max(1, (ch - y0 - gap) / 2);
  for (int i = 0; i < HA_MEDIA_BUTTONS; ++i) {
    lv_obj_t* b = r.haMediaButtons[i];
    if (!b) continue;
    lv_obj_set_pos(b, (i % 3) * (btnW + gap), y0 + (i / 3) * (btnH + gap));
    lv_obj_set_size(b, btnW, btnH);
    if (r.haMediaIcons[i]) {
      const int side = std::min(btnW, btnH);
      mediaControlGlyph(r.haMediaIcons[i], r, i, side >= 68 ? 40 : side >= 30 ? 24 : 16);
      lv_obj_center(r.haMediaIcons[i]);
    }
  }
}

void applyHaMedia(TileRt& r, const HaEntity* entity) {
  const bool available = mediaAvailable(entity);
  const String* app = assumedPlaybackApp(entity);
  if (!app || *app != r.haMediaPausedApp) r.haMediaPausedApp = "";
  if (themeUsesModernLayout(s_theme)) {
    String title = mediaHeadline(entity);
    if (!title.length()) title = mediaName(r, entity);
    const String status = mediaAvailable(entity) && entity->mediaArtist.length() && entity->mediaTitle.length() ? entity->mediaArtist : mediaStatus(entity);
    auto* icon = r.haMediaIcons[PLAY_PAUSE];
    const String previousIcon = lv_label_get_text(icon);
    const String previousBadge = lv_label_get_text(r.icon);
    const bool enabled = available && haLink() == HA_CONNECTED;
    const bool changed = title != lv_label_get_text(r.name) || status != lv_label_get_text(r.sub) ||
                         enabled == lv_obj_has_state(r.haMediaButtons[PLAY_PAUSE], LV_STATE_DISABLED);
    setText(r.name, title.c_str());
    setText(r.sub, status.c_str());
    mediaControlGlyph(icon, r, PLAY_PAUSE, r.iconPx ? r.iconPx : 28, entity);
    if (available) mediaAppBadge(r.icon, entity);
    setArtwork(r.haMediaArtwork, r.icon, entity ? entity->mediaPicture : String(), lv_obj_get_width(r.chip));
    const bool volume = canSetMediaVolume(entity);
    const uint8_t controls = mediaControls(r, entity);
    const bool progress = mediaHasProgress(entity);
    const uint8_t extras = mediaExtras(r,entity);
    setText(r.haMediaAlbum, available ? entity->mediaAlbum.c_str() : "");
    const bool unavailable = !available;
    const bool extraChanged = r.haMediaExtraMask != extras || changed || r.haMediaUnavailable != unavailable;
    r.haMediaUnavailable = unavailable;
    r.haMediaExtraMask = extras;
    if (extraChanged || r.haMediaCanSetVolume != volume || r.haMediaControlMask != controls || r.haMediaHasProgress != progress) {
      r.haMediaHasProgress = progress;
      r.haMediaCanSetVolume = volume;
      r.haMediaControlMask = controls;
      r.fitW = 0;
      fitTile(r);
    }
    syncTileExtras(r,entity);
    updateTileProgress(r, entity);
    syncMediaVolume(r.haMediaVolume, r.haMediaVolumeText, r, entity);
    for (int i = 0; i < HA_MEDIA_BUTTONS; ++i) {
      mediaControlGlyph(r.haMediaIcons[i], r, i, r.iconPx ? r.iconPx : 28, entity);
      auto* b = r.haMediaButtons[i];
      if (mediaControlEnabled(r, entity, i)) lv_obj_remove_state(b, LV_STATE_DISABLED);
      else lv_obj_add_state(b, LV_STATE_DISABLED);
    }
    if (changed || previousIcon != lv_label_get_text(icon) || previousBadge != lv_label_get_text(r.icon))
      markPageStale(r.page);
    return;
  }
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
    mediaControlGlyph(r.haMediaIcons[PLAY_PAUSE], r, PLAY_PAUSE, 24, entity);
  for (int i = 0; i < HA_MEDIA_BUTTONS; ++i) {
    auto* b = r.haMediaButtons[i];
    if (!b) continue;
    if (mediaControlEnabled(r, entity, i)) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else lv_obj_add_state(b, LV_STATE_DISABLED);
  }
  markPageStale(r.page);
}

namespace {
void onMediaExtra(lv_event_t* ev) {
  const size_t key = (size_t)(uintptr_t)lv_event_get_user_data(ev);
  const size_t index = key / 3, control = key % 3;
  if (index >= s_tiles.size() || haLink() != HA_CONNECTED) return;
  const auto& r = s_tiles[index];
  if (!tilePinAccess(r)) return;
  HaEntity e;
  if (!haGetEntity(r.def->entity, e) || !mediaAvailable(&e)) return;
  JsonDocument doc;
  const char* service = nullptr;
  if (control == 0 && mediaSupports(&e, MEDIA_SHUFFLE) && e.mediaShuffle >= 0) {
    service = "shuffle_set"; doc["shuffle"] = !e.mediaShuffle;
  } else if (control == 1 && mediaSupports(&e, MEDIA_REPEAT) && e.mediaRepeat.length()) {
    service = "repeat_set"; doc["repeat"] = e.mediaRepeat == "off" ? "all" : e.mediaRepeat == "all" ? "one" : "off";
  } else if (control == 2 && mediaSupports(&e, MEDIA_MUTE) && e.mediaMute >= 0) {
    service = "volume_mute"; doc["is_volume_muted"] = !e.mediaMute;
  }
  if (!service) return;
  String data; serializeJson(doc, data);
  if (!haCallService("media_player", service, r.def->entity, data))
    deckToastShow("Home Assistant is busy", 1800);
}
void onMediaSeek(lv_event_t* ev) {
  const size_t index = (size_t)(uintptr_t)lv_event_get_user_data(ev);
  if (index >= s_tiles.size()) return;
  const auto& r = s_tiles[index];
  if (!tilePinAccess(r)) return;
  HaEntity e;
  if (!haGetEntity(r.def->entity, e) || !mediaSupports(&e, MEDIA_SEEK) ||
      !isfinite(e.mediaDuration) || e.mediaDuration <= 0 || haLink() != HA_CONNECTED) return;
  const float pos = e.mediaDuration * lv_slider_get_value(lv_event_get_target_obj(ev)) / 1000.0f;
  char data[64];
  snprintf(data, sizeof(data), "{\"seek_position\":%.1f}", pos);
  if (!haCallService("media_player", "media_seek", r.def->entity, data))
    deckToastShow("Could not seek", 1800);
}
void onMediaSource(lv_event_t* ev) {
  const size_t index = (size_t)(uintptr_t)lv_event_get_user_data(ev);
  if (index >= s_tiles.size()) return;
  const auto& r = s_tiles[index];
  if (!tilePinAccess(r)) return;
  HaEntity e;
  if (!haGetEntity(r.def->entity, e) || !mediaSupports(&e, MEDIA_SELECT_SOURCE) ||
      haLink() != HA_CONNECTED) return;
  char name[256];
  lv_dropdown_get_selected_str(lv_event_get_target_obj(ev), name, sizeof(name));
  if (!*name || String(name) == e.mediaSource) return;
  JsonDocument doc;
  doc["source"] = name;
  String data;
  serializeJson(doc, data);
  if (!haCallService("media_player", "select_source", r.def->entity, data))
    deckToastShow("Could not select source", 1800);
}
bool mediaHasProgress(const HaEntity* e) {
  return mediaAvailable(e) && (e->state == "playing" || e->state == "paused" || (e->state == "idle" && e->mediaTitle.length())) &&
      isfinite(e->mediaPosition) && isfinite(e->mediaDuration) && e->mediaDuration > 0;
}
void updateMediaProgress(const HaEntity* e) {
  auto& m = s_mediaSheet;
  if (!m.progress || !mediaHasProgress(e)) return;
  float pos = e->mediaPosition;
  if (e->state == "playing" && e->mediaPositionAt)
    pos += (uint32_t)(millis() - e->mediaPositionAt) / 1000.0f;
  pos = std::clamp(pos, 0.0f, e->mediaDuration);
  if (lv_obj_has_state(m.progress, LV_STATE_PRESSED))
    pos = e->mediaDuration * lv_slider_get_value(m.progress) / 1000.0f;
  else {
    const int value = (int)lroundf(pos * 1000 / e->mediaDuration);
    if (lv_slider_get_value(m.progress) != value) lv_slider_set_value(m.progress, value, LV_ANIM_OFF);
  }
  const unsigned p = (unsigned)pos, d = (unsigned)e->mediaDuration;
  char time[48];
  snprintf(time, sizeof(time), "%u:%02u", p/60, p%60);
  setText(m.time, time);
  snprintf(time, sizeof(time), "%u:%02u", d/60, d%60);
  setText(m.duration, time);
}
}
void buildHaMediaSheet(lv_obj_t* card, int width, int height, int headerHeight,
                       int tile, uint32_t accent, bool large) {
  auto& m = s_mediaSheet;
  m.accent = accent;
  m.width = width; m.height = height; m.header = headerHeight; m.tile = tile; m.large = large;
  const auto& r = s_tiles[tile];
  m.title = mediaLabel(card, large ? &lv_font_montserrat_40 : &deck_font_28, C_TEXT);
  m.detail = mediaLabel(card, &deck_font_20, accent);
  m.state = mediaLabel(card, &deck_font_14, C_DIM);
  m.album = mediaLabel(card, &deck_font_14, C_DIM);
  auto* cover = lv_obj_create(card);
  lv_obj_remove_style_all(cover);
  lv_obj_remove_flag(cover, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_style_bg_color(cover, lv_color_hex(C_TILE), 0);
  lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(cover, 1, 0);
  lv_obj_set_style_border_color(cover, lv_color_hex(C_TILE_LINE), 0);
  lv_obj_set_style_radius(cover, 22, 0);
  lv_obj_set_style_clip_corner(cover, true, 0);
  m.badge = mediaLabel(cover, &deck_font_28, C_TEXT);
  lv_obj_center(m.badge);
  m.artwork = lv_image_create(cover);
  showObj(m.artwork, false);
  m.idle = mediaLabel(card, &deck_font_14, C_DIM);
  m.time = mediaLabel(card, &deck_font_14, C_DIM);
  m.duration = mediaLabel(card, &deck_font_14, C_DIM);
  lv_obj_set_style_text_align(m.duration, LV_TEXT_ALIGN_RIGHT, 0);
  m.progress = lv_slider_create(card);
  lv_slider_set_range(m.progress, 0, 1000);
  lv_obj_set_style_radius(m.progress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(m.progress, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_radius(m.progress, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_pad_all(m.progress, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(m.progress, lv_color_hex(C_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(m.progress, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(m.progress, lv_color_hex(accent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(m.progress, lv_color_hex(C_TEXT), LV_PART_KNOB);
  lv_obj_set_ext_click_area(m.progress, 8);
  lv_obj_add_event_cb(m.progress, onMediaSeek, LV_EVENT_RELEASED, (void*)(uintptr_t)tile);
  m.volumePanel = lv_obj_create(card);
  lv_obj_remove_style_all(m.volumePanel);
  lv_obj_remove_flag(m.volumePanel, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_style_bg_opa(m.volumePanel, LV_OPA_TRANSP, 0);
  m.volume = mediaLabel(m.volumePanel, &deck_font_14, C_TEXT);
  m.volumeIcon = lv_label_create(m.volumePanel);
  lv_obj_remove_flag(m.volumeIcon, LV_OBJ_FLAG_CLICKABLE);
  m.volumeSlider = makeMediaVolume(m.volumePanel, tile, accent);
  for (int j = 0; j < 3; ++j) {
    auto* b = m.extra[j] = lv_obj_create(card);
    lv_obj_remove_style_all(b);
    lv_obj_remove_flag(b, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE));
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_BUTTON), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    m.extraGlyph[j] = lv_label_create(b);
    lv_obj_center(m.extraGlyph[j]);
    lv_obj_add_event_cb(b, onMediaExtra, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)(tile*3+j));
  }
  m.sources = lv_dropdown_create(card);
  lv_obj_set_style_text_font(m.sources, mediaTextFont(&deck_font_14), 0);
  lv_obj_add_event_cb(m.sources, onMediaSource, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)tile);
  m.sourceLabel = mediaLabel(card, &deck_font_14, C_DIM);
  setText(m.sourceLabel, "Output / source");
  m.iconPx = large ? 40 : 24;
  for (int j = 0; j < HA_MEDIA_BUTTONS; ++j) {
    auto* button = m.buttons[j] = cinematicMediaButton(card, tile, j, accent);
    auto* glyph = lv_label_create(button);
    mediaControlGlyph(glyph, r, j, m.iconPx);
    lv_obj_center(glyph);
    if (j == PLAY_PAUSE) m.play = glyph;
  }
}
void refreshHaMediaSheet(const TileRt& r, const HaEntity* entity) {
  auto& m = s_mediaSheet;
  if (!m.title) return;
  const int pad = m.large ? 40 : 12;
  const int gap = m.large ? 16 : 5;
  const bool compactWide = !m.large && m.width >= 400;
  const bool wide = (m.large && m.width >= 800) || compactWide;
  const int coverSide = compactWide ? 100 : wide ? std::min(ART_SIDE, m.height - m.header - pad - 24) : 44;
  auto* cover = lv_obj_get_parent(m.badge);
  lv_obj_set_size(cover, coverSide, coverSide);
  lv_obj_set_pos(cover, pad, m.header + gap);
  const int left = wide ? pad + coverSide + 2 * gap : pad;
  const int w = m.width - left - pad;
  const int top = m.header + gap;
  setArtwork(m.artwork, m.badge, entity ? entity->mediaPicture : String(), coverSide);
  const bool progress = mediaHasProgress(entity);
  const bool volume = canSetMediaVolume(entity);
  const bool sources = mediaAvailable(entity) && entity->mediaSources.length() != 0 &&
      mediaSupports(entity, MEDIA_SELECT_SOURCE);
  uint8_t controls = mediaAvailable(entity) ? mediaControls(r, entity) : 0;
  if (volume) controls &= ~((1u << VOLUME_DOWN) | (1u << VOLUME_UP));
  int count = 0;
  for (int j = 0; j < HA_MEDIA_BUTTONS; ++j) if (controls & (1u << j)) ++count;
  String headline = mediaHeadline(entity);
  if (!headline.length()) headline = mediaAvailable(entity) ? "Nothing playing" : "Player unavailable";
  setText(m.title, headline.c_str());
  setText(m.detail, mediaAvailable(entity) && entity->mediaArtist.length() ? entity->mediaArtist.c_str() : "");
  String state = mediaAvailable(entity) ? entity->state : String("Unavailable");
  if (state.length()) state.setCharAt(0,toupper(static_cast<unsigned char>(state[0])));
  setText(m.state, mediaPlaying(r, entity) ? "NOW PLAYING" : state.c_str());
  lv_obj_set_style_text_color(m.state, lv_color_hex(mediaPlaying(r,entity)?m.accent:C_DIM),0);
  const bool album = mediaAvailable(entity) && entity->mediaAlbum.length() && headline != "Nothing playing";
  setText(m.album, album ? entity->mediaAlbum.c_str() : "");
  mediaAppBadge(m.badge, entity);
  const auto* tf = m.large ? &lv_font_montserrat_40 : &deck_font_20;
  const auto* df = m.large ? &deck_font_28 : &deck_font_14;
  const auto* af = m.large ? &deck_font_20 : &deck_font_14;
  const int row = m.large ? 86 : 34;
  const bool shuffle = mediaSupports(entity, MEDIA_SHUFFLE) && entity->mediaShuffle >= 0;
  const bool repeat = mediaSupports(entity, MEDIA_REPEAT) && entity->mediaRepeat.length();
  const bool mute = mediaSupports(entity, MEDIA_MUTE) && entity->mediaMute >= 0;
  if ((shuffle || repeat) && (controls & (1u << PLAY_PAUSE))) { controls &= ~(1u << 4); }
  count = shuffle + repeat;
  for (int j=0;j<HA_MEDIA_BUTTONS;++j) if (controls & (1u<<j)) ++count;
  const auto* timeFont = m.large ? &deck_font_20 : &deck_font_14;
  const int progressH = progress ? 22 + lineH(timeFont) + gap : 0;
  const int sourceH = sources ? row + gap : 0;
  const int volumeH = (volume || mute) ? (m.large ? 50 : 32) + gap : 0;
  const int controlsH = count ? row + gap : 0;
  const int detailGap = m.large ? 4 : 0;
  const int metaH = lineH(&deck_font_14) + lineH(tf) + lineH(df) + (album ? lineH(af) : 0) + gap + 2*detailGap;
  const int total = metaH + progressH + sourceH + volumeH + controlsH;
  int y = top + std::max(0, (m.height - pad - top - total) / 2);
  if (wide) lv_obj_set_pos(cover,pad,top+std::max(0,(m.height-pad-top-coverSide)/2));
  const int badge = wide ? 0 : coverSide + gap;
  if (!wide) lv_obj_set_pos(cover, pad, y);
  lv_obj_center(m.badge);
  const int tx = left + badge;
  mediaLayoutLabel(m.state, &deck_font_14, tx, y, std::max(1, w - badge));
  y += lineH(&deck_font_14);
  mediaLayoutLabel(m.title, tf, tx, y, std::max(1, w - badge));
  y += lineH(tf) + detailGap;
  mediaLayoutLabel(m.detail, df, tx, y, std::max(1, w - badge));
  y += lineH(df) + detailGap;
  showObj(m.album, album);
  if (album) { mediaLayoutLabel(m.album, af, tx, y, w - badge); y += lineH(af); }
  y += gap;
  showObj(m.progress, progress); showObj(m.time, progress); showObj(m.duration, progress);
  if (progress) {
    lv_obj_set_pos(m.progress, left + 8, y + 6);
    lv_obj_set_size(m.progress, w - 16, m.large ? 12 : 7);
    if (mediaSupports(entity, MEDIA_SEEK) && haLink() == HA_CONNECTED) lv_obj_remove_state(m.progress, LV_STATE_DISABLED);
    else lv_obj_add_state(m.progress, LV_STATE_DISABLED);
    mediaLayoutLabel(m.time, timeFont, left, y + 20, w/2);
    mediaLayoutLabel(m.duration, timeFont, left + w/2, y + 20, w-w/2);
    updateMediaProgress(entity);
    y += progressH;
  }
  const int cell = count ? std::min(row + (m.large ? 18 : 4), (w - (count - 1) * gap) / count) : 1;
  int x = left + (w - count * cell - std::max(0, count - 1) * gap) / 2;
  auto placeExtra = [&](int j, bool visible, const char* glyph, const char* fallback, bool active) {
    auto* b = m.extra[j];
    showObj(b, visible);
    if (!visible) return;
    lv_obj_set_size(b, cell, row);
    lv_obj_set_pos(b, x, y);
    mediaGlyph(m.extraGlyph[j], glyph, fallback, m.large ? 40 : 24);
    lv_obj_set_style_text_color(m.extraGlyph[j], lv_color_hex(active ? m.accent : C_TEXT), 0);
    lv_obj_center(m.extraGlyph[j]);
    x += cell + gap;
  };
  placeExtra(0, shuffle, "shuffle", "S", shuffle && entity->mediaShuffle);
  for (int j = 0; j < HA_MEDIA_BUTTONS; ++j) {
    const bool visible = controls & (1u << j);
    auto* b = m.buttons[j];
    showObj(b, visible);
    if (!visible) continue;
    const int bw = j == PLAY_PAUSE ? std::min(cell,row) : cell;
    lv_obj_set_pos(b, x + (cell - bw)/2, y);
    lv_obj_set_size(b, bw, row);
    if (mediaControlEnabled(r, entity, j)) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else lv_obj_add_state(b, LV_STATE_DISABLED);
    x += cell + gap;
  }
  placeExtra(1, repeat, repeat && entity->mediaRepeat == "one" ? "repeat-once" : "repeat", "R", repeat && entity->mediaRepeat != "off");
  for (int i=0;i<HA_MEDIA_BUTTONS;++i) {
    auto* glyph = lv_obj_get_child(m.buttons[i],0);
    if (glyph) mediaControlGlyph(glyph,r,i,m.iconPx,entity);
  }
  y += controlsH;
  showObj(m.volumePanel, volume);
  showObj(m.volume, volume); showObj(m.volumeSlider, volume);
  showObj(m.extra[2], mute);
  const int vh = m.large ? 50 : 32;
  const int volumeWidth = wide ? std::min(w, 620) : w;
  const int vl = left + (w-volumeWidth)/2;
  const int vw = volumeWidth;
  if (volume) {
    lv_obj_set_pos(m.volumePanel, vl, y);
    lv_obj_set_size(m.volumePanel, vw, vh);
    const int labelW=m.large?64:44;
    mediaLayoutLabel(m.volume, timeFont, vw-labelW-4, (vh-lineH(timeFont))/2, labelW);
    lv_obj_set_style_text_align(m.volume,LV_TEXT_ALIGN_RIGHT,0);
    showObj(m.volumeIcon,!mute);
    if (!mute) {
      mediaGlyph(m.volumeIcon,"volume-high","V",m.large?28:20);
      lv_obj_align(m.volumeIcon,LV_ALIGN_LEFT_MID,8,0);
    }
    lv_obj_set_pos(m.volumeSlider, vh+gap, (vh-6)/2);
    lv_obj_set_size(m.volumeSlider, std::max(1,vw-vh-labelW-2*gap-4), 6);
    syncMediaVolume(m.volumeSlider, m.volume, r, entity);
  }
  if (mute) {
    lv_obj_set_pos(m.extra[2], volume?vl:left+(w-vh)/2, y);
    lv_obj_set_size(m.extra[2], vh, vh);
    lv_obj_set_style_bg_color(m.extra[2], lv_color_hex(C_BUTTON), 0);
    lv_obj_set_style_bg_opa(m.extra[2], LV_OPA_COVER, 0);
    mediaGlyph(m.extraGlyph[2], entity->mediaMute?"volume-off":"volume-high", "M", m.large ? 28 : 20);
    lv_obj_set_style_text_color(m.extraGlyph[2], lv_color_hex(entity->mediaMute ? m.accent : C_TEXT), 0);
    lv_obj_center(m.extraGlyph[2]);
    if (haLink()==HA_CONNECTED) lv_obj_remove_state(m.extra[2],LV_STATE_DISABLED);
    else lv_obj_add_state(m.extra[2],LV_STATE_DISABLED);
  }
  y += volumeH;
  showObj(m.sources, sources); showObj(m.sourceLabel, sources);
  if (sources) {
    if (m.sourceOptions != entity->mediaSources) {
      m.sourceOptions = entity->mediaSources;
      lv_dropdown_set_options(m.sources, m.sourceOptions.c_str());
    }
    int selected = 0, from = 0;
    while (from < (int)m.sourceOptions.length()) {
      int end = m.sourceOptions.indexOf('\n', from);
      if (end < 0) end = m.sourceOptions.length();
      if (m.sourceOptions.substring(from,end) == entity->mediaSource) {
        if (lv_dropdown_get_selected(m.sources) != selected) lv_dropdown_set_selected(m.sources, selected);
        break;
      }
      from = end + 1; ++selected;
    }
    const int sourceLabelW = m.large ? 118 : 66;
    mediaLayoutLabel(m.sourceLabel, &deck_font_14, left, y + 5, sourceLabelW - 3);
    lv_obj_set_pos(m.sources, left + sourceLabelW, y);
    lv_obj_set_size(m.sources, std::max(1,w - sourceLabelW), row);
    if (haLink() == HA_CONNECTED) lv_obj_remove_state(m.sources, LV_STATE_DISABLED);
    else lv_obj_add_state(m.sources, LV_STATE_DISABLED);
  }
  const bool idle = !count && !volume && !sources && !progress;
  showObj(m.idle, idle);
  if (idle) {
    mediaLayoutLabel(m.idle, &deck_font_14, left, y + gap, w);
    setText(m.idle, "No playback controls available");
  }
}
void tickHaMediaSheet() {
  static uint32_t lastPoll = 0, lastProgress = 0;
  static size_t cursor = 0;
  const uint32_t now = millis();
  if (now - lastPoll < 200 || touchIsDown()) return;
  lastPoll = now;
  const bool progressDue = now - lastProgress >= 1000;
  if (progressDue) lastProgress = now;
  const int modal = s_mediaSheet.artwork ? s_mediaSheet.tile : -1;
  // Gather the visible requests, prioritising the expanded player; skip
  // players without pictures and back off individual failed URLs.
  struct Request { size_t index; String url; };
  std::vector<Request> requests;
  auto inspect = [&](size_t n, bool sheet) {
    auto& r = s_tiles[n];
    if (!r.def || !r.haMediaArtwork || !r.chip ||
        (!sheet && !lv_obj_is_visible(r.chip))) return;
    HaEntity e;
    if (!haGetEntity(r.def->entity, e) || !mediaAvailable(&e)) return;
    if (progressDue) {
      updateTileProgress(r, &e);
      if (sheet) updateMediaProgress(&e);
    }
    if (e.state == "off" || e.state == "standby" || !e.mediaPicture.length()) return;
    requests.push_back({n, e.mediaPicture});
  };
  if (modal >= 0 && modal < (int)s_tiles.size()) inspect(modal, true);
  for (size_t step = 0; step < s_tiles.size(); ++step) {
    const size_t n = (cursor + step) % s_tiles.size();
    if ((int)n != modal) inspect(n, false);
  }
  auto wanted = [&](const String& url) {
    for (const auto& req : requests) if (req.url == url) return true;
    return false;
  };
  if (artDone.exchange(false, std::memory_order_acquire)) {
    if (artJob.pixels && wanted(artJob.url) && !findArt(artJob.url)) {
      ArtSlot* slot = nullptr;
      for (auto& entry : artCache) if (!entry.pixels) { slot = &entry; break; }
      if (!slot) for (auto& entry : artCache) {
        if (wanted(entry.url)) continue;
        if (!slot || now - entry.used > now - slot->used) slot = &entry;
      }
      if (slot) {
        evictArt(*slot);
        slot->pixels = artJob.pixels; artJob.pixels = nullptr;
        slot->url = artJob.url; slot->used = now;
        slot->image = {};
        slot->image.header.magic = LV_IMAGE_HEADER_MAGIC;
        slot->image.header.cf = LV_COLOR_FORMAT_RGB565;
        slot->image.header.w = ART_SIDE; slot->image.header.h = ART_SIDE;
        slot->image.header.stride = ART_SIDE * 2;
        slot->image.data_size = ART_SIDE * ART_SIDE * 2;
        slot->image.data = (const uint8_t*)slot->pixels;
        for (const auto& req : requests) if (req.url == slot->url) {
          auto& r = s_tiles[req.index];
          HaEntity e;
          if (!haGetEntity(r.def->entity, e) || e.mediaPicture != slot->url) continue;
          applyHaMedia(r, &e); markPageStale(r.page);
          if ((int)req.index == modal) refreshHaMediaSheet(r, &e);
        }
      }
    }
    free(artJob.pixels); artJob.pixels = nullptr;
    artBusy.store(false, std::memory_order_release);
  }
  if (artBusy.load(std::memory_order_acquire)) return;
  bool capacity = false;
  for (const auto& slot : artCache) if (!slot.pixels || !wanted(slot.url)) { capacity = true; break; }
  if (!capacity) return; // Never churn covers when all cache entries are visible.
  for (const auto& req : requests) {
    if (findArt(req.url) || !retryAllowed(req.url, now)) continue;
    recordAttempt(req.url, now);
    cursor = req.index + 1;
    artJob.url = req.url;
    artBusy.store(true, std::memory_order_release);
    if (!wallRunWorker(artWorker, false, "media_art")) artBusy.store(false, std::memory_order_release);
    break;
  }
}

void resetHaMediaSheet() {
  s_mediaSheet = {};
}

}  // namespace deckui
