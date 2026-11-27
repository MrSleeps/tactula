#include "optional_preferences.h"
// Guition JC3248W535 deck UI. See deck_ui.h for the threading contract.
#include "debug_log.h"
#include "p4_trace.h"
#include "deck_ui_int.h"
#include "deck_auth.h"
#if defined(BOARD_IS_JC8012P4A1C)
#include "p4_board.h"
#endif
#include <utility>
#include "deck_mqtt.h"
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
#include "deck_office_bambu.h"
#endif
#include "psram_json.h"

namespace deckui {

// Page images are refreshed one per pass, at most this often (each costs
// ~40 ms of loop time at 480x320).
constexpr uint32_t SNAP_SPACING_MS = 40;
#if defined(BOARD_IS_JC8012P4A1C)
constexpr uint32_t CINEMATIC_PAGE_SNAP_MS = 1500;
constexpr uint32_t CINEMATIC_SNAP_SPACING_MS = 450;
uint32_t s_snapshotUnderruns = 0;
// All fields below are protected by s_mux (the gesture ownership lock).
static uint32_t s_directEpoch = 0;
static int s_directOrigin = -1, s_directTarget = -1;
#endif
constexpr uint32_t PANEL_SNAP_MS = 5000;  // a stale panel image waits at most this long behind busy pages
constexpr uint32_t STATS_MS = 10000;
constexpr uint32_t SLIDE_MS = 280;

struct GlassCacheEntry {
  String id;
  std::vector<uint32_t> key;
  lv_draw_buf_t* buffer = nullptr;
};
GlassCacheEntry s_glassCache[MAX_PAGES];
bool s_frameSync = false;  // frameCatchUp: rebuilds and page changes keep the frame

void clearGlassCache() {
  for (GlassCacheEntry& c : s_glassCache) {
    if (c.buffer) lv_draw_buf_destroy(c.buffer);
    c = {};
  }
}

// Theme colours are set before the pages are built.
uint8_t s_theme = 0;
uint32_t C_BG, C_TILE, C_TILE_PRESSED, C_TILE_LINE, C_CHIP, C_TEXT, C_TEXT_ON, C_TITLE,
    C_CLOCK, C_OK, C_DIM, C_PANEL, C_TRACK, C_BUTTON;

void setPalette(uint8_t theme) {
  clearGlassCache();
  s_theme = normalizeDisplayTheme(theme);
  const bool day = s_theme == THEME_DAYLIGHT, glass = themeUsesWallpaper(s_theme);
  C_BG = day ? 0xF5F6FB : 0x0C1019;
  C_TILE = day ? 0xFFFFFF : glass ? 0x101622 : 0x171E2C;  // glass: the tint
  C_TILE_PRESSED = day ? 0xE9EDF6 : glass ? 0x151C2B : 0x252E43;
  C_TILE_LINE = day ? 0xDFE4EF : glass ? 0xFFFFFF : 0x2B3445;
  C_CHIP = day ? 0xE9EDF6 : 0x252E3D;
  C_TEXT = day ? 0x242940 : 0xEDF0F8;
  C_TEXT_ON = 0xFFFFFF;
  C_TITLE = day ? 0x242940 : 0xEDF0F8;
  C_CLOCK = day ? 0x66738B : 0xD3DAEA;
  C_OK = day ? 0x7664E4 : 0x927DFF;  // tactula violet, not legacy green
  C_DIM = day ? 0x78859D : 0x8795AC;
  C_PANEL = day ? 0xFFFFFF : 0x171E2C;
  C_TRACK = day ? 0xDFE4EF : 0x2B3445;
  C_BUTTON = day ? 0xE9EDF6 : 0x252E3D;
  if (s_theme == THEME_TACTULA) {
    C_TILE = 0x242344;
    C_TILE_PRESSED = 0x312A59;
    C_TILE_LINE = 0x7765C7;
    C_CHIP = 0x312A59;
    C_DIM = 0xA6B1C5;
    C_PANEL = 0x171E2C;
    C_TRACK = 0x393460;
    C_BUTTON = 0x312A59;
  } else if (themeUsesModernLayout(s_theme)) {
    C_BG = 0x060E20;
    C_TILE = 0x111D36;
    C_TILE_PRESSED = 0x203457;
    C_TILE_LINE = 0x2B456C;
    C_CHIP = 0x192E50;
    C_TEXT = 0xF1F5FF;
    C_TITLE = C_TEXT;
    C_CLOCK = C_TEXT;
    C_OK = 0x5197FF;
    C_DIM = 0xB3C3DF;
    C_PANEL = 0x0F1B32;
    C_TRACK = 0x243959;
    C_BUTTON = 0x1B3054;
    if (themeUsesEmber(s_theme)) {
      C_BG = 0x151617; C_TILE = 0x292B2E; C_TILE_PRESSED = 0x403B36;
      C_TILE_LINE = 0x645249; C_CHIP = 0x373330; C_TEXT = 0xF6F2ED;
      C_TITLE = C_CLOCK = C_TEXT; C_OK = 0xE99449; C_DIM = 0xC8BBB0;
      C_PANEL = 0x202123; C_TRACK = 0x48413C; C_BUTTON = 0x393532;
    } else if (themeUsesAmethyst(s_theme)) {
      C_BG = 0x1B142B; C_TILE = 0x30213F; C_TILE_PRESSED = 0x49335F;
      C_TILE_LINE = 0x654A80; C_CHIP = 0x362647; C_TEXT = 0xF5EEFF;
      C_TITLE = C_CLOCK = C_TEXT; C_OK = 0xAC91F4; C_DIM = 0xD8C5F5;
      C_PANEL = 0x241A35; C_TRACK = 0x48365D; C_BUTTON = 0x3D2B52;
    } else if (themeUsesForest(s_theme)) {
      C_BG = 0x101F1C; C_TILE = 0x1C342D; C_TILE_PRESSED = 0x2C4D40;
      C_TILE_LINE = 0x416B58; C_CHIP = 0x203B32; C_TEXT = 0xEDF9F3;
      C_TITLE = C_CLOCK = C_TEXT; C_OK = 0x65C5A0; C_DIM = 0xB5DFCE;
      C_PANEL = 0x172C25; C_TRACK = 0x345445; C_BUTTON = 0x284638;
    } else if (themeUsesGraphite(s_theme)) {
      C_BG = 0x15171A; C_TILE = 0x292D32; C_TILE_PRESSED = 0x3D434B;
      C_TILE_LINE = 0x555D68; C_CHIP = 0x30343A; C_TEXT = 0xF2F4F7;
      C_TITLE = C_CLOCK = C_TEXT; C_OK = 0xB4C0CE; C_DIM = 0xD3D9E1;
      C_PANEL = 0x202329; C_TRACK = 0x424851; C_BUTTON = 0x363C44;
    }
  }
}
 
// Central mapping for decorative modern blue colours. Keep semantic
// heating/cooling, connection status, and user/device colours unchanged.
uint32_t modernColour(uint32_t ocean) {
  if (!themeUsesModernLayout(s_theme)) return ocean;
  const int palette = themeUsesEmber(s_theme) ? 0 : themeUsesAmethyst(s_theme) ? 1 :
      themeUsesForest(s_theme) ? 2 : themeUsesGraphite(s_theme) ? 3 : -1;
  if (palette < 0) return ocean;
  // Each decorative token has one colour per non-Ocean palette.
  struct ColourRow { uint32_t ocean; uint32_t colours[4]; };
  static constexpr ColourRow rows[] = {
    {0x25435F, {0x49413B, 0x503B65, 0x395648, 0x444A53}},
    {0x203C60, {0x413A35, 0x463459, 0x304B3F, 0x3A4048}},
    {0x15283E, {0x292725, 0x2B203B, 0x23372F, 0x2B3036}},
    {0x0A1527, {0x171819, 0x20162F, 0x14251E, 0x1A1D22}},
    {0x45678D, {0x76604D, 0x775D94, 0x547B68, 0x666F7C}},
    {0x29496A, {0x61462F, 0x584071, 0x355847, 0x4A525D}},
    {0xA5BCD5, {0xC8BBB0, 0xD8C5F5, 0xB5DFCE, 0xD3D9E1}},
    {0x85BCFF, {0xF6C28C, 0xD8C5F5, 0xA8E5CC, 0xE1E5EB}},
    {0x89BFFF, {0xF6C28C, 0xD8C5F5, 0xA8E5CC, 0xE1E5EB}},
    {0x8DC5FF, {0xF6C28C, 0xD8C5F5, 0xA8E5CC, 0xE1E5EB}},
    {0x85C4FF, {0xF6C28C, 0xD8C5F5, 0xA8E5CC, 0xE1E5EB}},
    {0x5197FF, {0xE99449, 0xAC91F4, 0x65C5A0, 0xB4C0CE}},
    {0x5286C3, {0xE99449, 0xAC91F4, 0x65C5A0, 0xB4C0CE}},
    {0x438ACB, {0xE99449, 0xAC91F4, 0x65C5A0, 0xB4C0CE}},
    {0x4F8CFF, {0xE99449, 0xAC91F4, 0x65C5A0, 0xB4C0CE}},
    {0x28588D, {0x92582F, 0x7055A0, 0x377F65, 0x677381}},
    {0x192E50, {0x373330, 0x362647, 0x203B32, 0x30343A}},
    {0x364B64, {0x52483F, 0x59456F, 0x3A594A, 0x49515C}},
    {0x304B6B, {0x52483F, 0x59456F, 0x3A594A, 0x49515C}},
    {0x304B65, {0x52483F, 0x59456F, 0x3A594A, 0x49515C}},
    {0x0D192A, {0x252324, 0x251B35, 0x1B2E25, 0x25292F}},
    {0x14273C, {0x252324, 0x251B35, 0x1B2E25, 0x25292F}},
    {0x0B1727, {0x252324, 0x251B35, 0x1B2E25, 0x25292F}},
    {0x496681, {0x685748, 0x6B5385, 0x4B6E5B, 0x5E6773}},
    {0x365471, {0x685748, 0x6B5385, 0x4B6E5B, 0x5E6773}},
    {0x37536C, {0x685748, 0x6B5385, 0x4B6E5B, 0x5E6773}},
    {0x82BCF9, {0xF3BC83, 0xCDB6FA, 0x99DEC2, 0xD5DDE6}},
    {0x76B9FF, {0xF3BC83, 0xCDB6FA, 0x99DEC2, 0xD5DDE6}},
    {0x294C6D, {0x5B4635, 0x53406A, 0x365846, 0x48515C}},
    {0x29445F, {0x5B4635, 0x53406A, 0x365846, 0x48515C}},
    {0x88ABC9, {0xC6AC95, 0xBEA9DF, 0x9DCDB8, 0xB9C2CE}},
    {0x59D8F4, {0xF5B36F, 0xC8ACFF, 0x80DDB8, 0xD1D9E3}},
  };
  for (const auto& row : rows) if (row.ocean == ocean) return row.colours[palette];
  return ocean;
}

// An icon in its round chip: on Daylight a light tint of the accent behind a
// darker accent, since pale accents (amber, yellow) vanish on white.
void styleIconChip(lv_obj_t* chip, uint32_t accent) {
  const lv_color_t a = lv_color_hex(accent);
  const bool day = s_theme == THEME_DAYLIGHT;
  lv_obj_set_style_bg_color(chip, day ? lv_color_mix(a, lv_color_white(), 60) : lv_color_hex(C_CHIP), 0);
  lv_obj_set_style_text_color(chip, day ? lv_color_mix(a, lv_color_black(), 165) : a, 0);
}

lv_display_t* s_disp = nullptr;
lv_indev_t* s_indev = nullptr;
DeckConfig s_cfg;
Page s_pages[MAX_PAGES];
std::vector<TileRt> s_tiles;
// A web upload/delete can happen while a gesture is active. Refresh when idle.
static std::vector<String> s_changedTilePngs;
int s_count = 0;
uint32_t s_gen = 1;
uint32_t s_lastSnapMs = 0;
uint32_t s_panelSnapMs = 0;
uint32_t s_lastClockCheck = 0;
int s_lastMinute = -1;
int s_lastWifi = -1;
uint32_t s_lastStats = 0;
uint32_t s_waitTimeouts = 0;
uint32_t s_snapUsMax = 0;
lv_style_transition_dsc_t s_pressTrans;

// Shared with the touch task, guarded by s_mux. `active` is only written by
// the loop task.
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

Shared sh;
static int8_t s_livePanelRequest = 0; // protected by s_mux
int s_zonesPage = -1;     // page the zones were taken from
bool s_zonesDirty = true;  // tiles rebuilt or moved: take them again

// --- LVGL glue ---------------------------------------------------------------------
uint32_t tickCb() { return millis(); }

// Posted LVGL frames whose framebuffer the flush task has not released yet.
int s_lvOut = 0;
#if defined(BOARD_IS_JC8012P4A1C)
constexpr uint32_t SCANOUT_HOLD_MS = 60000;
constexpr uint32_t SNAPSHOT_TEST_MS = 120000;
constexpr uint32_t CPU_SCANOUT_TEST_MS = 120000;
DeckDisplayTest s_displayTest = DeckDisplayTest::None;
uint32_t s_displayTestStarted = 0;
uint32_t s_displayTestFrames = 0;
uint32_t s_displayTestUnderruns = 0;
uint32_t s_displayTestSubmitted = 0;
PanelDamage s_lvDamage = {};
#endif

void flushCb(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
#if defined(BOARD_IS_JC8012P4A1C)
  if (deckScanoutHoldActive()) {
    s_lvDamage.full = true;
    lv_display_flush_ready(d);
    return;
  }
  if (!s_lvDamage.full) {
    if (s_lvDamage.count == PanelDamage::MaxRects) s_lvDamage.full = true;
    else s_lvDamage.rects[s_lvDamage.count++] = {area->x1, area->y1, area->x2, area->y2};
  }
#else
  (void)area;
#endif
  if (!lv_display_flush_is_last(d)) {
    lv_display_flush_ready(d);
    return;
  }
  const uint8_t* fb1 = (const uint8_t*)panelLvBuffer(1);
  const int idx = (px >= fb1 && px < fb1 + FRAME_BYTES) ? 1 : 0;
#if defined(BOARD_IS_JC8012P4A1C)
  panelPostLv(idx, s_gen, s_lvDamage);
  s_lvDamage = {};
  if (s_displayTest == DeckDisplayTest::WithoutSnapshots ||
      s_displayTest == DeckDisplayTest::PartialScanout ||
      s_displayTest == DeckDisplayTest::CpuScanout) ++s_displayTestSubmitted;
#else
  panelPostLv(idx, s_gen);
#endif
  s_lvOut++;
}

// Blocking wait instead of LVGL's default busy spin on `flushing`. The flush
// task gives exactly one token per posted frame, and never calls
// lv_display_flush_ready() itself. LVGL draws next into the other buffer, so
// only the older frame must be released: the panel writes frame N while
// LVGL renders N+1 (waiting for N itself halved LVGL animations to 30 fps).
void flushWaitCb(lv_display_t*) {
  while (s_lvOut > 1) {
    if (!panelWaitLvReady(250)) {
      s_waitTimeouts++;
      s_lvOut = 1;
      break;
    }
    s_lvOut--;
  }
}

// Double-tap the non-interactive dashboard background or page title.
// Never count touches on controls: both taps must be harmless.
static bool s_homeTapPending = false;
static int s_homeTapPage = -1;
static bool homeTapArea(int x, int y) {
  if (getScreenState() != SCREEN_DECK || frameActive() || s_qp.open ||
      s_modalCard || s_ed.root || compMode() != COMP_NONE ||
      sh.active < 0 || sh.active >= s_count) return false;
  const Page& page = s_pages[sh.active];
  auto contains = [x,y](lv_obj_t* obj) {
    if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return false;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2;
  };
  if (contains(page.updateBadge)) return false;
  if (contains(page.title)) return true;
  if (!contains(page.grid)) return false;
  for (const auto& tile : s_tiles)
    if (tile.page == sh.active && !tile.off && contains(tile.obj)) return false;
  return true;
}

static void trackHomeTap(const TouchPoint& p) {
  static bool down = false, eligible = false;
  static uint32_t started = 0, last = 0;
  static int x = 0, y = 0, firstX = 0, firstY = 0, page = -1, count = 0;
  const uint32_t now = millis();
  const int radius = logicalW() >= 800 ? 40 : 24;
  if (p.cancel || frameActive() || getScreenState() != SCREEN_DECK) {
    down = eligible = false;
    count = 0;
    return;
  }
  if (p.pressed && !down) {
    down = true;
    started = now;
    x = p.x; y = p.y;
    eligible = homeTapArea(x, y);
    if (!eligible || page != sh.active || now - last > 450 ||
        abs(x - firstX) > radius || abs(y - firstY) > radius) count = 0;
    if (!count) { firstX = x; firstY = y; page = sh.active; }
  } else if (p.pressed && down) {
    if (abs(p.x - x) > 12 || abs(p.y - y) > 12) eligible = false;
  } else if (!p.pressed && down) {
    down = false;
    if (!eligible || now - started > 300 || page != sh.active ||
        !homeTapArea(p.x, p.y)) { count = 0; return; }
    last = now;
    if (++count == 2) {
      s_homeTapPending = true;
      s_homeTapPage = page;
      count = 0;
    }
  }
}

void indevRead(lv_indev_t* indev, lv_indev_data_t* data) {
#if defined(BOARD_IS_JC8012P4A1C)
  if (s_displayTest == DeckDisplayTest::RenderOnly) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
#endif
  const TouchPoint p = touchForLvgl();
  trackHomeTap(p);
  // Drop the press before LVGL handles the release from a swipe.
  if (p.cancel) lv_indev_reset(indev, nullptr);
  data->point.x = p.x;
  data->point.y = p.y;
  data->state = p.pressed && !frameActive() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// --- Navigation -------------------------------------------------------------------------
void switchTo(int page);

// Slide one column (or, vertical, one page of the column) toward dir through
// the compositor, as a fling would.
bool slideBy(int dir, bool vertical) {
  Src a, b;
  int so, idxA;
  if (!uiTryBeginSwipe(dir, vertical, a, b, so, idxA)) return false;
  if (!compBeginDrag(a, b, so, idxA, vertical)) {
    releaseSwipe();
    return false;
  }
  const int full = vertical ? logicalH() : logicalW();
  compRelease(dir > 0 ? full : 0, SLIDE_MS, (int8_t)dir);
  return true;
}

int pageIndexOf(const String& id) {
  for (int i = 0; i < s_count; i++)
    if (s_pages[i].def && s_pages[i].def->id == id) return i;
  return -1;
}

String activePageId() {
  const int a = sh.active;
  return a >= 0 && a < s_count && s_pages[a].def ? s_pages[a].def->id : String();
}

void goToPage(const String& id) {
  if (frameActive() && !s_frameSync) stopGestures();
  for (int i = 0; i < s_count; i++) {
    if (!s_pages[i].def || s_pages[i].def->id != id) continue;
    const int a = sh.active;
    if (i == a) return;
    if (s_frameSync) {  // under the frame picture: no slide
      switchTo(i);
      return;
    }
    const int dc = sh.col[i] - sh.col[a];
    if (dc == 0 && (i - a == 1 || i - a == -1) && slideBy(i - a, true)) return;
    if ((dc == 1 || dc == -1) && sh.colCur[sh.col[i]] == i && slideBy(dc, false)) return;
    switchTo(i);  // not a neighbour (or images not ready): jump
    return;
  }
}


static lv_obj_t* s_firmwareConfirm = nullptr;
static lv_obj_t* s_pinMask = nullptr;
static lv_obj_t* s_pinStatus = nullptr;
static char s_updatePin[9] = {};
static char s_confirmVersion[40] = {};
static uint8_t s_pinLength = 0;
static uint32_t s_pinOpened = 0;
// -1 = firmware, -2 = editor, >=0 = index of one protected widget.
static int s_pinTarget = -1, s_pinPage = -1;
static int s_widgetUnlocked = -1, s_editorGrant = -1;
static uint32_t s_widgetAt = 0, s_widgetRevision = 0, s_editorRevision = 0;
static constexpr uint32_t WIDGET_UNLOCK_MS = 30000;
static void openUpdatePin(int target = -1, int page = -1);
static void lockWidgets();
static void refreshPinShields();
static bool widgetPinGranted(const TileRt& r) {
  return !r.def->requirePin || (deckAuth::widgetPinEnabled() &&
      s_widgetUnlocked >= 0 && size_t(s_widgetUnlocked) < s_tiles.size() &&
      &r == &s_tiles[s_widgetUnlocked] && !r.off && r.page == sh.active &&
      s_widgetRevision == deckAuth::widgetPinRevision() &&
      millis() - s_widgetAt < WIDGET_UNLOCK_MS);
}
bool tilePinAccess(const TileRt& r, bool prompt) {
  if (widgetPinGranted(r)) return true;
  if (!prompt || r.off || r.page != sh.active || s_firmwareConfirm) return false;
  if (!deckAuth::widgetPinEnabled()) {
    deckToastShow("Set a widget PIN in Security", 2200);
    return false;
  }
  const int idx = int(&r - s_tiles.data());
  lockWidgets();
  openUpdatePin(idx, r.page);
  return false; // never replay a tap/drag after PIN entry
}
static bool editorNeedsPin() {
  if (deckAuth::widgetPinEnabled()) return true;
  for (const auto& page : s_cfg.pages)
    for (const auto& tile : page.tiles) if (tile.requirePin) return true;
  return false;
}
bool editorPinAccess(int page) {
  if (!editorNeedsPin()) return true;
  if (s_editorGrant == page && s_editorRevision == deckAuth::widgetPinRevision()) {
    s_editorGrant = -1;
    return true;
  }
  if (!deckAuth::widgetPinEnabled()) {
    deckToastShow("Set a widget PIN in Security", 2200);
    return false;
  }
  if (!s_firmwareConfirm && page == sh.active) {
    lockWidgets();
    openUpdatePin(-2, page);
  }
  return false;
}
static void clearUpdatePin() {
  memset(s_updatePin, 0, sizeof(s_updatePin));
  s_pinLength = 0;
}
// Track a freshly staged image independently of top-bar badge preferences.
// Declining the popup must not make it appear once per second.
static bool s_firmwarePrompted = false;
static void dismissFirmwareConfirm() {
  clearUpdatePin();
  s_pinMask = s_pinStatus = nullptr;
  s_pinOpened = 0;
  s_pinTarget = s_pinPage = -1;
  if (s_firmwareConfirm) {
    lv_obj_delete_async(s_firmwareConfirm);
    s_firmwareConfirm = nullptr;
    setModalOpen(s_modalCard != nullptr);
  }
}
static void closeFirmwareConfirm(lv_event_t*) {
  if (s_pinTarget == -1) firmwareUpdate::cancelTouchConfirmation();
  dismissFirmwareConfirm();
}
static void updatePinMask() {
  if (!s_pinMask) return;
  char mask[9] = {};
  memset(mask, '*', s_pinLength);
  lv_label_set_text(s_pinMask, s_pinLength ? mask : "Enter PIN");
}

static void submitUpdatePin() {
  if (s_pinTarget != -1) {
    const int target = s_pinTarget, page = s_pinPage;
    const auto result = deckAuth::verifyWidgetPin(s_updatePin);
    clearUpdatePin();
    if (result == deckAuth::PinResult::Accepted && page == sh.active &&
        deckAuth::widgetPinEnabled()) {
      dismissFirmwareConfirm();
      s_widgetRevision = deckAuth::widgetPinRevision();
      s_widgetAt = millis();
      if (target == -2) {
        s_editorRevision = s_widgetRevision;
        s_editorGrant = page;
        edOpen(page);
        s_editorGrant = -1;
      } else if (target >= 0 && size_t(target) < s_tiles.size() &&
                 !s_tiles[target].off && s_tiles[target].page == page &&
                 s_tiles[target].def->requirePin) {
        s_widgetUnlocked = target;
        refreshPinShields();
        deckToastShow("Widget unlocked for 30 seconds", 1800);
      }
      return;
    }
    updatePinMask();
    if (s_pinStatus) lv_label_set_text(s_pinStatus, result == deckAuth::PinResult::Locked ?
        "Too many attempts. Wait 30s." : "Incorrect widget PIN.");
    return;
  }
  using R = firmwareUpdate::TouchInstallResult;
  const auto result = firmwareUpdate::installStagedFromTouch(s_updatePin, s_confirmVersion);
  clearUpdatePin();
  if (result == R::Accepted || result == R::NotReady) {
    closeFirmwareConfirm(nullptr);
    if (result == R::NotReady) debugSerial.println("Update: staged image no longer ready");
    return;
  }
  updatePinMask();
  if (s_pinStatus) lv_label_set_text(s_pinStatus, result == R::PinLocked ?
      "Too many attempts. Wait 30s." : "Incorrect PIN. Try again.");
}

static void updatePinKey(lv_event_t* event) {
  if (!s_pinMask) return;
  const int key = (int)(intptr_t)lv_event_get_user_data(event);
  if (key == 11) {
    if (s_pinLength >= 4) submitUpdatePin();
    else lv_label_set_text(s_pinStatus, "Enter at least 4 digits.");
    return;
  }
  if (key == 10) {
    if (s_pinLength) s_updatePin[--s_pinLength] = 0;
  } else if (key >= 0 && key <= 9 && s_pinLength < 8) {
    s_updatePin[s_pinLength++] = char('0' + key);
    s_updatePin[s_pinLength] = 0;
  }
  updatePinMask();
}

static void openUpdatePin(int target, int page) {
  dismissFirmwareConfirm(); // keep expected version and any web permission
  s_pinTarget = target;
  s_pinPage = page;
  s_pinOpened = millis();
  setModalOpen(true);
  const int w = logicalW(), h = logicalH();
  const bool large = std::min(w,h) >= 800;
  const int cw = std::min(w - 16, large ? 500 : 304);
  const int pad = large ? 22 : 10, gap = large ? 12 : 6;
  const int headH = large ? 40 : 28, fieldH = large ? 58 : 34;
  const int statusH = large ? 30 : 20;
  const int keyH = std::min(large ? 76 : 42, (h - 16 - 2*pad - headH - fieldH - statusH - 6*gap)/4);
  const int ch = 2*pad + headH + fieldH + statusH + 4*keyH + 6*gap;
  const int innerW = cw - 2*pad;
  const lv_font_t* labelFont = large ? &deck_font_20 : &deck_font_14;
  const lv_font_t* keyFont = large ? &deck_font_28 : &deck_font_20;
  // A full-screen scrim consumes taps; dashboard controls cannot receive them.
  s_firmwareConfirm = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_firmwareConfirm);
  lv_obj_set_size(s_firmwareConfirm, w, h);
  lv_obj_center(s_firmwareConfirm);
  lv_obj_set_style_bg_color(s_firmwareConfirm, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_firmwareConfirm, LV_OPA_60, 0);
  lv_obj_add_flag(s_firmwareConfirm, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_firmwareConfirm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* card = lv_obj_create(s_firmwareConfirm);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, cw, ch);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, large ? 22 : 14, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* title = lv_label_create(card);
  lv_obj_set_style_text_font(title, labelFont, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(C_TEXT), 0);
  lv_label_set_text(title, target == -1 ? "Firmware update PIN" : target == -2 ? "Editor PIN" : "Widget PIN");
  lv_obj_set_pos(title, pad, pad + (headH - lv_font_get_line_height(labelFont))/2);
  auto button = [&](int x, int y, int bw, int bh, const char* label, const lv_font_t* font,
                    bool primary, lv_event_cb_t cb, void* data) {
    lv_obj_t* b = lv_button_create(card);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b,x,y);
    lv_obj_set_size(b,bw,bh);
    lv_obj_set_style_radius(b, large ? 12 : 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? C_OK : C_BUTTON), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l,font,0);
    lv_obj_set_style_text_color(l,lv_color_hex(primary ? C_BG : C_TEXT),0);
    lv_label_set_text(l,label);
    lv_obj_center(l);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,data);
  };
  const int cancelW = large ? 90 : 58;
  button(cw-pad-cancelW,pad,cancelW,headH,"Cancel",labelFont,false,closeFirmwareConfirm,nullptr);
  int y = pad + headH + gap;
  lv_obj_t* field = lv_obj_create(card);
  lv_obj_remove_style_all(field);
  lv_obj_set_pos(field,pad,y);
  lv_obj_set_size(field,innerW,fieldH);
  lv_obj_set_style_bg_color(field,lv_color_hex(C_BG),0);
  lv_obj_set_style_bg_opa(field,LV_OPA_COVER,0);
  lv_obj_set_style_radius(field,8,0);
  lv_obj_remove_flag(field, LV_OBJ_FLAG_SCROLLABLE);
  s_pinMask = lv_label_create(field);
  lv_obj_set_style_text_font(s_pinMask,keyFont,0);
  lv_obj_set_style_text_color(s_pinMask,lv_color_hex(C_TEXT),0);
  updatePinMask();
  lv_obj_center(s_pinMask);
  y += fieldH + gap;
  const char* labels[] = {"1","2","3","4","5","6","7","8","9","Del","0","OK"};
  const int keys[] = {1,2,3,4,5,6,7,8,9,10,0,11};
  const int kw = (innerW - 2*gap)/3;
  for (int i=0;i<12;++i)
    button(pad+(i%3)*(kw+gap),y+(i/3)*(keyH+gap),kw,keyH,labels[i],
        keyFont,keys[i]==11,updatePinKey,(void*)(intptr_t)keys[i]);
  y += 4*keyH + 4*gap;
  s_pinStatus = lv_label_create(card);
  lv_obj_set_width(s_pinStatus,innerW);
  lv_obj_set_pos(s_pinStatus,pad,y);
  lv_obj_set_style_text_font(s_pinStatus,labelFont,0);
  lv_obj_set_style_text_color(s_pinStatus,lv_color_hex(C_DIM),0);
  lv_label_set_text(s_pinStatus,target == -1 ? "Enter firmware PIN to install." : "Enter widget PIN to unlock.");
}

static void refreshPinShields() {
  for (auto& r : s_tiles) {
    if (!r.pinShield) continue;
    const bool show = !widgetPinGranted(r);
    const bool hidden = lv_obj_has_flag(r.pinShield, LV_OBJ_FLAG_HIDDEN);
    if (show == hidden) {
      if (show) lv_obj_remove_flag(r.pinShield, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(r.pinShield, LV_OBJ_FLAG_HIDDEN);
      markPageStale(r.page);
    }
  }
}
static void lockWidgets() {
  if (s_widgetUnlocked >= 0 && s_modalTile == s_widgetUnlocked) closeModal();
  s_widgetUnlocked = -1;
  refreshPinShields();
}
static void sizePinShield(lv_event_t* ev) {
  auto* tile = lv_event_get_target_obj(ev);
  auto* shield = static_cast<lv_obj_t*>(lv_event_get_user_data(ev));
  lv_obj_set_size(shield, lv_obj_get_width(tile), lv_obj_get_height(tile));
  lv_obj_center(shield);
}
static void addPinShield(TileRt& r, size_t index) {
  if (!r.def->requirePin) return;
  auto* shield = lv_obj_create(r.obj);
  r.pinShield = shield;
  lv_obj_remove_style_all(shield);
  lv_obj_add_flag(shield, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT));
  lv_obj_remove_flag(shield, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE));
  lv_obj_set_size(shield, lv_obj_get_width(r.obj), lv_obj_get_height(r.obj));
  lv_obj_center(shield);
  auto* label = lv_label_create(shield);
  lv_label_set_text(label, "PIN");
  lv_obj_set_style_text_font(label, &deck_font_12, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_bg_color(label, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(label, 3, 0);
  lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -3, 3);
  lv_obj_add_event_cb(r.obj, sizePinShield, LV_EVENT_SIZE_CHANGED, shield);
  lv_obj_add_event_cb(shield, [](lv_event_t* ev) {
    const size_t i = (size_t)(uintptr_t)lv_event_get_user_data(ev);
    if (i < s_tiles.size()) tilePinAccess(s_tiles[i]);
  }, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)index);
}
static void approveFirmwareFromTouch(lv_event_t*) {
  if (firmwareUpdate::touchPinRequired()) openUpdatePin();
  else submitUpdatePin();
}
static void downloadFirmwareFromTouch(lv_event_t*) {
  // Queue a verified OTA download; the firmware is never installed without
  // a second physical confirmation after staging and validation.
  firmwareUpdate::requestDownload(true);
  closeFirmwareConfirm(nullptr);
}
static void openFirmwareConfirmation(lv_event_t*) {
  if (s_firmwareConfirm) return;
  const auto st = firmwareUpdate::snapshot();
  snprintf(s_confirmVersion,sizeof(s_confirmVersion),"%s",st.version);
  if (!st.allowTouchUpdates && !(st.staged && st.webConfirmationAuthorized)) return;
  if (!st.available && !st.staged && !st.downloading) return;
  const int w = logicalW(), h = logicalH();
  const bool large = std::min(w, h) >= 800;
  const int cw = std::min(w - (large ? 64 : 20), large ? 640 : 360);
  const int pad = large ? 28 : 14;
  const int gap = large ? 16 : 10;
  const int buttonH = large ? 60 : 40;
  const int innerW = cw - 2 * pad;
  const lv_font_t* titleFont = large ? &deck_font_28 : &deck_font_20;
  const lv_font_t* bodyFont = large ? &deck_font_20 : &deck_font_14;
  const lv_font_t* buttonFont = large ? &deck_font_20 : &deck_font_14;
  const char* heading = st.staged ? "Install verified update?" :
                        st.downloading ? "Firmware downloading" :
                        "Firmware update available";
  const char* actionName = st.staged ? "Install and restart" : "Download update";
  String message(st.version);
  if (st.staged) message += " is verified and ready. The screen will restart.";
  else if (st.downloading) message += " is downloading. Return here when ready.";
  else if (st.error[0]) {
    message += " is available, but not downloaded. Last error: ";
    message += st.error;
  } else message += " is available. Download and verify it before installing.";

  // Explicit fonts and measured wrapping: the P4 theme's inherited font is
  // much larger than the S3 default. Keep buttons outside the scrollable body.
  lv_point_t titleSize{}, bodySize{};
  lv_text_get_size(&titleSize, heading, titleFont, 0, 0, innerW, LV_TEXT_FLAG_NONE);
  lv_text_get_size(&bodySize, message.c_str(), bodyFont, 0, 2, innerW, LV_TEXT_FLAG_NONE);
  const int halfW = (innerW - gap) / 2;
  const bool sideBySide = !st.downloading &&
      halfW >= std::max(textW("Not now", buttonFont), textW(actionName, buttonFont)) + 20;
  const int buttonsH = st.downloading || sideBySide ? buttonH : 2 * buttonH + gap;
  const int maxBodyH = std::max(1, h - (large ? 64 : 20) -
      (2 * pad + int(titleSize.y) + 2 * gap + buttonsH));
  const int bodyH = std::min(int(bodySize.y), maxBodyH);
  const int ch = 2 * pad + int(titleSize.y) + 2 * gap + bodyH + buttonsH;

  s_firmwareConfirm = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_firmwareConfirm);
  lv_obj_set_size(s_firmwareConfirm, cw, ch);
  lv_obj_center(s_firmwareConfirm);
  lv_obj_set_style_bg_color(s_firmwareConfirm, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_bg_opa(s_firmwareConfirm, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_firmwareConfirm, large ? 22 : 14, 0);
  lv_obj_set_style_border_color(s_firmwareConfirm, lv_color_hex(C_OK), 0);
  lv_obj_set_style_border_width(s_firmwareConfirm, 1, 0);
  lv_obj_add_flag(s_firmwareConfirm, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_firmwareConfirm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(s_firmwareConfirm, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t* title = lv_label_create(s_firmwareConfirm);
  lv_obj_set_style_text_font(title, titleFont, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(C_TEXT), 0);
  lv_obj_set_width(title, innerW);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(title, pad, pad);
  lv_label_set_text(title, heading);

  // Only an unusually long diagnostic can scroll, never the action row.
  lv_obj_t* body = lv_obj_create(s_firmwareConfirm);
  lv_obj_remove_style_all(body);
  lv_obj_set_pos(body, pad, pad + int(titleSize.y) + gap);
  lv_obj_set_size(body, innerW, bodyH);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  if (bodySize.y <= bodyH) lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  else lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t* version = lv_label_create(body);
  lv_obj_set_style_text_font(version, bodyFont, 0);
  lv_obj_set_style_text_color(version, lv_color_hex(C_DIM), 0);
  lv_obj_set_style_text_line_space(version, 2, 0);
  lv_obj_set_width(version, innerW);
  lv_label_set_long_mode(version, LV_LABEL_LONG_WRAP);
  lv_label_set_text(version, message.c_str());

  const int by = ch - pad - buttonsH;
  auto addButton = [&](int x, int y, int bw, const char* name, bool primary, lv_event_cb_t cb) {
    lv_obj_t* b = lv_button_create(s_firmwareConfirm);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, bw, buttonH);
    lv_obj_set_style_radius(b, large ? 12 : 9, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? C_OK : C_BUTTON), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_t* label = lv_label_create(b);
    lv_obj_set_style_text_font(label, buttonFont, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(primary ? C_BG : C_TEXT), 0);
    lv_label_set_text(label, name);
    lv_obj_center(label);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  };
  addButton(pad, by, sideBySide ? halfW : innerW, "Not now", false, closeFirmwareConfirm);
  if (!st.downloading) {
    addButton(sideBySide ? pad + halfW + gap : pad,
              sideBySide ? by : by + buttonH + gap,
              sideBySide ? innerW - halfW - gap : innerW,
              actionName, true,
              st.staged ? approveFirmwareFromTouch : downloadFirmwareFromTouch);
  }
}

// Shows or hides a tiles page's top bar; the grid takes the freed height.
void layoutHeader(Page& p) {
  if (!p.grid) return;
  s_zonesDirty = true;
  const int w = logicalW(), h = logicalH();
  for (lv_obj_t* o : {p.title, p.wifi}) {
    if (s_header) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  showObj(p.headerBand, s_header);
  if (p.updateBadge) {
    const auto st = firmwareUpdate::snapshot();
    if (s_header && st.allowTouchUpdates && st.checksEnabled && (st.showBadge || (st.staged && !st.promptOnStage)) && (st.available || st.staged))
      lv_obj_remove_flag(p.updateBadge, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(p.updateBadge, LV_OBJ_FLAG_HIDDEN);
  }
  // A clock tile already displays time on this page. Keep the header strip,
  // title and Wi-Fi, but hide only its duplicate clock.
  if (s_header && !p.hasClockTile) lv_obj_remove_flag(p.clock, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(p.clock, LV_OBJ_FLAG_HIDDEN);
  // Reclaim the clock's space for the Wi-Fi indicator on clock-tile pages.
  lv_obj_align(p.wifi, LV_ALIGN_TOP_RIGHT, p.hasClockTile ? -12 : -62, 8);
  if (p.updateBadge) lv_obj_align(p.updateBadge, LV_ALIGN_TOP_RIGHT, p.hasClockTile ? -31 : -81, 0);
  const int top = s_header ? HEADER_H : 0;
  lv_obj_set_pos(p.grid, 0, top);
  lv_obj_set_size(p.grid, w, h - top);
  lv_obj_set_style_pad_top(p.grid, s_header ? 2 : 8, 0);
  p.stale = true;
  if (p.glass) bakeGlass(p);  // the tiles moved
}

void placeTiles(const PageDef& def, int orient, std::vector<Cell>& out) {
  const int cols = def.cols[orient] * 2, rows = def.rows[orient] * 2;
  // P4 permits 8x6 whole cells (16x12 half cells); S3 uses a subset.
  bool used[12][16] = {};
  auto fits = [&](int x, int y, int tw, int th) {
    if (x + tw > cols || y + th > rows) return false;
    for (int yy = y; yy < y + th; yy++)
      for (int xx = x; xx < x + tw; xx++)
        if (used[yy][xx]) return false;
    return true;
  };
  auto take = [&](Cell& c, int x, int y) {
    for (int yy = y; yy < y + c.h; yy++)
      for (int xx = x; xx < x + c.w; xx++) used[yy][xx] = true;
    c.placed = true;
    c.x = x;
    c.y = y;
  };
  out.assign(def.tiles.size(), Cell{false, 0, 0, 2, 2});
  for (size_t i = 0; i < def.tiles.size(); i++) {
    const TilePos& ps = def.tiles[i].pos[orient];
    out[i].w = ps.w;
    out[i].h = ps.h;
    out[i].off = !def.tiles[i].shown || def.tiles[i].parent.length();
    if (!out[i].off && ps.set && fits(ps.x, ps.y, ps.w, ps.h)) take(out[i], ps.x, ps.y);
  }
  for (Cell& c : out) {
    if (c.off) continue;
    for (int y = 0; y < rows && !c.placed; y++)
      for (int x = 0; x < cols && !c.placed; x++)
        if (fits(x, y, c.w, c.h)) take(c, x, y);
  }
}

// Every group uses equal tracks. LVGL retains descriptor pointers, so use
// a shared immutable tail of 24 fractional tracks plus its end marker.
void layoutGroups(int page) {
  static const int32_t tracks[] = {
    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST
  };
  for (TileRt& group : s_tiles) {
    if (group.page != page || !group.groupBody) continue;
    const bool custom = group.def->groupLayout == 3;
    int count = 0;
    for (const TileRt& child : s_tiles)
      if (child.page == page && child.def->parent == group.def->id && child.def->shown) ++count;
    const int cols = custom ? 6 : group.def->groupLayout == 0 ? std::max(1, count) :
                     group.def->groupLayout == 1 ? 1 : std::min<int>(group.def->groupColumns, std::max(1, count));
    const int rows = custom ? 6 : std::max(1, (count + cols - 1) / cols);
    lv_obj_set_grid_dsc_array(group.groupBody, tracks + MAX_PAGE_TILES - cols,
                             tracks + MAX_PAGE_TILES - rows);
    int slot = 0;
    for (TileRt& child : s_tiles) {
      if (child.page != page || child.def->parent != group.def->id) continue;
      child.off = group.off || !child.def->shown;
      if (child.off) {
        lv_obj_add_flag(child.obj, LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      if (custom) {
        // Old children without a slot get deterministic, non-overlapping defaults.
        const int x = child.def->groupSlotSet ? child.def->groupX : (slot % 2) * 3;
        const int y = child.def->groupSlotSet ? child.def->groupY : ((slot / 2) % 3) * 2;
        const int w = child.def->groupSlotSet ? child.def->groupW : 3;
        const int h = child.def->groupSlotSet ? child.def->groupH : 2;
        lv_obj_set_grid_cell(child.obj, LV_GRID_ALIGN_STRETCH, x, w,
                            LV_GRID_ALIGN_STRETCH, y, h);
      } else {
        lv_obj_set_grid_cell(child.obj, LV_GRID_ALIGN_STRETCH, slot % cols, 1,
                            LV_GRID_ALIGN_STRETCH, slot / cols, 1);
      }
      lv_obj_remove_flag(child.obj, LV_OBJ_FLAG_HIDDEN);
      ++slot;
    }
  }
}

void onGridHold(lv_event_t* e) {
  const int page = (int)(uintptr_t)lv_event_get_user_data(e);
  if (page == sh.active) edOpen(page);
}

void addTilesPage(const PageDef& def) {
  const int w = logicalW(), h = logicalH();
  const int orient = w > h ? ORIENT_LANDSCAPE : ORIENT_PORTRAIT;
  const int page = s_count;
  Page& p = s_pages[s_count];
  p = {};
  p.def = &def;
  p.root = makeRoot(w, h);
  if (s_theme == THEME_TACTULA) {
    p.headerBand = lv_obj_create(p.root);
    lv_obj_remove_style_all(p.headerBand);
    lv_obj_remove_flag(p.headerBand, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(p.headerBand, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(p.headerBand, w, HEADER_H);
    lv_obj_set_style_bg_color(p.headerBand, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_bg_opa(p.headerBand, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(p.headerBand, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(p.headerBand, 2, 0);
    lv_obj_set_style_border_color(p.headerBand, lv_color_hex(C_OK), 0);
  }

  lv_obj_t* t = lv_label_create(p.root);
  p.title = t;
  String pageTitle = def.name.length() ? def.name : def.id;
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  if (def.id == "office_printer") {
    const String friendlyName = deck::officeBambuDisplayName();
    if (friendlyName.length()) pageTitle = friendlyName;
  }
#endif
  lv_label_set_text(t, pageTitle.c_str());
  lv_obj_set_style_text_font(t, &deck_font_20, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(C_TITLE), 0);
  lv_obj_set_pos(t, 12, 5);
  p.clock = lv_label_create(p.root);
  lv_label_set_text(p.clock, "");
  lv_obj_set_style_text_color(p.clock, lv_color_hex(C_CLOCK), 0);
  lv_obj_align(p.clock, LV_ALIGN_TOP_RIGHT, -12, 8);
  p.wifi = lv_label_create(p.root);
  lv_label_set_text(p.wifi, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(p.wifi, lv_color_hex(C_DIM), 0);
  lv_obj_align(p.wifi, LV_ALIGN_TOP_RIGHT, -62, 8);
  // A small LVGL label is difficult to hit on the 3.5-inch panel.
  // Use a real 36x36 touch target with a centred download glyph.
  p.updateBadge = lv_button_create(p.root);
  lv_obj_set_size(p.updateBadge, 36, 36);
  lv_obj_set_style_bg_opa(p.updateBadge, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(p.updateBadge, 0, 0);
  lv_obj_set_style_border_width(p.updateBadge, 0, 0);
  lv_obj_set_style_pad_all(p.updateBadge, 0, 0);
  lv_obj_t* updateGlyph = lv_label_create(p.updateBadge);
  lv_label_set_text(updateGlyph, LV_SYMBOL_DOWNLOAD);
  lv_obj_set_style_text_color(updateGlyph, lv_color_hex(C_OK), 0);
  lv_obj_center(updateGlyph);
  lv_obj_align(p.updateBadge, LV_ALIGN_TOP_RIGHT, -81, 0);
  lv_obj_add_flag(p.updateBadge, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(p.updateBadge, openFirmwareConfirmation, LV_EVENT_CLICKED, nullptr);

  // Two tracks per cell: tiles sit and span in half cells. Static descriptor
  // arrays per page (LVGL keeps the pointers).
  const int cols = def.cols[orient] * 2, rows = def.rows[orient] * 2;
  // 8 columns / 6 rows at half-cell resolution, plus terminator.
  static int32_t colDsc[MAX_PAGES][17];
  static int32_t rowDsc[MAX_PAGES][13];
  for (int i = 0; i < cols; i++) colDsc[page][i] = LV_GRID_FR(1);
  colDsc[page][cols] = LV_GRID_TEMPLATE_LAST;
  for (int i = 0; i < rows; i++) rowDsc[page][i] = LV_GRID_FR(1);
  rowDsc[page][rows] = LV_GRID_TEMPLATE_LAST;

  lv_obj_t* grid = lv_obj_create(p.root);
  p.grid = grid;
  lv_obj_remove_style_all(grid);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);
  lv_obj_set_grid_dsc_array(grid, colDsc[page], rowDsc[page]);
  lv_obj_set_style_pad_all(grid, 8, 0);

  // Decide this from actually placed/visible tiles, not merely the page's
  // configuration: a conditional clock or one that cannot fit must not
  // suppress the header time.
  std::vector<Cell> cells;
  placeTiles(def, orient, cells);
  for (size_t i = 0; i < cells.size(); i++)
    if (cells[i].placed && (def.tiles[i].type == TILE_CLOCK || def.tiles[i].type == TILE_WEATHER_CLOCK)) {
      p.hasClockTile = true;
      break;
    }
  layoutHeader(p);
  lv_obj_set_style_pad_row(grid, 7, 0);
  lv_obj_set_style_pad_column(grid, 7, 0);

  // Long press on an empty part of the page opens the edit mode.
  lv_obj_add_flag(grid, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(grid, onGridHold, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)page);

  // Build even conditionally hidden tiles. They keep stable LVGL callbacks
  // and can be rearranged without destroying every page on a when flip.
  // Create containers before their children, independent of saved tile order.
  for (int pass = 0; pass < 2; ++pass) for (size_t i = 0; i < cells.size(); i++) {
    if ((def.tiles[i].type == TILE_GROUP) != (pass == 0)) continue;
    lv_obj_t* parent = grid;
    if (def.tiles[i].parent.length()) {
      for (const TileRt& group : s_tiles)
        if (group.page == page && group.groupBody && group.def->id == def.tiles[i].parent)
          parent = group.groupBody;
    }
    const Cell& c = cells[i];
    if (!c.placed && !c.off) debugSerial.printf("Deck: page '%s' has no room for tile '%s'\n",
                       def.id.c_str(), def.tiles[i].id.c_str());
    makeTile(parent, def.tiles[i], page, c.placed ? c.x : 0, c.placed ? c.y : 0, c.w, c.h);
    addPinShield(s_tiles.back(), s_tiles.size() - 1);
    if (!c.placed) {
      s_tiles.back().off = true;
      lv_obj_add_flag(s_tiles.back().obj, LV_OBJ_FLAG_HIDDEN);
    }
  }

  layoutGroups(page);
  for (const TileRt& child : s_tiles)
    if (child.page == page && !child.off && (child.def->type == TILE_CLOCK || child.def->type == TILE_WEATHER_CLOCK)) p.hasClockTile = true;
  layoutHeader(p);
#if defined(BOARD_IS_JC8012P4A1C)
  // Cinematic uses a live active page and only needs snapshots for nearby
  // gesture targets. Allocate those on demand after the dashboard is built.
  if (!themeUsesModernLayout(s_theme))
#endif
    p.snap = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, 0);
  p.stale = true;
  s_count++;
}

// Update just the affected page's tile grid; retain every tile's LVGL state.
void placePage(int page) {
  Page& p = s_pages[page];
  const PageDef& def = *p.def;
  std::vector<Cell> cells;
  placeTiles(def, logicalW() > logicalH() ? ORIENT_LANDSCAPE : ORIENT_PORTRAIT, cells);
  struct Was { TileRt* r; bool off; lv_area_t area; };
  std::vector<Was> was;
  bool clockVisible = false;
  for (size_t i = 0; i < cells.size(); ++i)
    if (cells[i].placed && (def.tiles[i].type == TILE_CLOCK || def.tiles[i].type == TILE_WEATHER_CLOCK)) clockVisible = true;
  lv_obj_update_layout(p.root);
  for (TileRt& r : s_tiles) {
    if (r.page != page || r.def->parent.length()) continue;
    const size_t index = r.def - def.tiles.data();
    if (index >= cells.size()) continue;
    const Cell& c = cells[index];
    lv_obj_t* tile = r.obj;
    was.push_back({&r, r.off, {}});
    lv_obj_get_coords(tile, &was.back().area);
    if (c.placed && (lv_obj_get_style_grid_cell_column_pos(tile, LV_PART_MAIN) != c.x ||
                     lv_obj_get_style_grid_cell_row_pos(tile, LV_PART_MAIN) != c.y ||
                     lv_obj_get_style_grid_cell_column_span(tile, LV_PART_MAIN) != c.w ||
                     lv_obj_get_style_grid_cell_row_span(tile, LV_PART_MAIN) != c.h))
      lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, c.x, c.w, LV_GRID_ALIGN_STRETCH, c.y, c.h);
    if (r.off == !c.placed) continue;
    r.off = !c.placed;
    if (c.placed) lv_obj_remove_flag(tile, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(tile, LV_OBJ_FLAG_HIDDEN);
  }
  layoutGroups(page);
  markPageStale(page);
  s_zonesDirty = true;
  for (const TileRt& child : s_tiles)
    if (child.page == page && child.def->parent.length() && !child.off && (child.def->type == TILE_CLOCK || child.def->type == TILE_WEATHER_CLOCK))
      clockVisible = true;
  const bool clockChanged = p.hasClockTile != clockVisible;
  if (clockChanged) {
    p.hasClockTile = clockVisible;
    layoutHeader(p);
  }
  lv_obj_update_layout(p.root);
  lv_area_t rc, changed = {LV_COORD_MAX, LV_COORD_MAX, LV_COORD_MIN, LV_COORD_MIN};
  lv_obj_get_coords(p.root, &rc);
  auto add = [&](const lv_area_t& a) {
    changed.x1 = LV_MIN(changed.x1, a.x1 - rc.x1);
    changed.y1 = LV_MIN(changed.y1, a.y1 - rc.y1);
    changed.x2 = LV_MAX(changed.x2, a.x2 - rc.x1);
    changed.y2 = LV_MAX(changed.y2, a.y2 - rc.y1);
  };
  for (const Was& prev : was) {
    lv_area_t now;
    lv_obj_get_coords(prev.r->obj, &now);
    const bool same = now.x1 == prev.area.x1 && now.y1 == prev.area.y1 &&
                      now.x2 == prev.area.x2 && now.y2 == prev.area.y2;
    if (prev.off == prev.r->off && (prev.off || same)) continue;
    if (!prev.off) add(prev.area);
    if (!prev.r->off) add(now);
  }
  if (changed.x1 > changed.x2) {
    if (clockChanged) p.stale = true;
    return;
  }
  // Re-bake only where tiles moved. A missing glass buffer falls back to full bake.
  if (themeUsesWallpaper(s_theme) && wallSharp()) bakeGlass(p, &changed);
  p.stale = true;
  s_zonesDirty = true;
}

void buildPages() {
  fxQuiet();  // tiles get their first state: no animations
  const int w = logicalW();
  if (themeUsesWallpaper(s_theme)) {
    const bool cinematic = themeUsesModernLayout(s_theme);
    wallLoad(w, logicalH(), cinematic ? 0 : dispSettings.glassBlur,
             cinematic && !themeUsesGlassMode(s_theme) ? max<int>(dispSettings.glassDim, 35) : dispSettings.glassDim);
  }
  else wallFree();
  s_count = 0;
  s_tiles.clear();
  s_zonesDirty = true;
  s_tiles.reserve(64);
  whenApply();
  int keep = -1;
  for (const PageDef& pd : s_cfg.pages) {
    if (s_count >= MAX_PAGES) break;
    if (!pd.shown) continue;
    if (s_keepPage.length() && pd.id == s_keepPage) keep = s_count;
    addTilesPage(pd);
  }
  const bool byId = s_keepPage.length() > 0;
  s_keepPage = "";

  portENTER_CRITICAL(&s_mux);
  sh.count = s_count;
  sh.owned = false;
  for (int i = 0; i < MAX_PAGES; i++) { sh.valid[i] = false; sh.busy[i] = false; }
  const int active = byId ? (keep >= 0 ? keep : 0) : sh.active < s_count ? sh.active : 0;
  sh.active = active;
  sh.nCols = 0;
  for (int i = 0; i < s_count; i++) {
    const String& g = s_pages[i].def->group;
    const bool joins = i > 0 && g.length() && g == s_pages[i - 1].def->group;
    if (!joins) {
      sh.colFirst[sh.nCols] = i;
      sh.colLen[sh.nCols] = 0;
      sh.colCur[sh.nCols] = i;
      sh.nCols++;
    }
    sh.col[i] = sh.nCols - 1;
    sh.colLen[sh.nCols - 1]++;
  }
  sh.colCur[sh.col[active]] = active;
  portEXIT_CRITICAL(&s_mux);

  setPager(active);
  // Inactive pages wait just off the right edge: they keep their layout and
  // can be snapshotted, but their invalidations are clipped away.
  for (int i = 0; i < s_count; i++) lv_obj_set_pos(s_pages[i].root, i == active ? 0 : w, 0);
  unsigned baked = 0, reused = 0;
  uint32_t bakeMs = 0;
  if (themeUsesWallpaper(s_theme) && !themeUsesModernLayout(s_theme)) {
    for (int i = 0; i < s_count; i++) {
      Page& p = s_pages[i];
      std::vector<uint32_t> key = glassKey(p);
      if (key.empty()) continue;
      for (GlassCacheEntry& c : s_glassCache) {
        if (!c.buffer || c.id != p.def->id || c.key != key) continue;
        p.glass = c.buffer;
        c.buffer = nullptr;
        p.glassKey.swap(key);
        lv_obj_set_style_bg_image_src(p.root, p.glass, 0);
        autoTextPage(p);
        lv_obj_invalidate(p.root);
        p.stale = true;
        reused++;
        break;
      }
    }
  }
  // Drop removed or changed pages before allocating any replacement bakes.
  clearGlassCache();
  if (themeUsesWallpaper(s_theme) && !themeUsesModernLayout(s_theme)) {
    for (int i = 0; i < s_count; i++) {
      if (s_pages[i].glass) continue;
#if defined(BOARD_IS_JC8012P4A1C)
      if (i != active && wallSharp()) {
        // Keep the active page fast to update. The normal wallpaper tick
        // bakes one inactive page per pass when interaction is clear.
        lv_obj_set_style_bg_image_src(s_pages[i].root, wallSharp(), 0);
        s_pages[i].wallBake = true;
        continue;
      }
#endif
      const uint32_t start = millis();
      if (bakeGlass(s_pages[i])) baked++;
      bakeMs += millis() - start;
    }
  }
  debugSerial.printf("Deck: glass %u baked, %u reused, %lu ms\n", baked, reused, (unsigned long)bakeMs);
  s_lastMinute = -1;
  s_lastWifi = -1;
  buildQuickPanel();
  collectEntities();
}

void setPager(int page) {
  const int c = sh.col[page];
  panelSetPager(sh.nCols, c, sh.colLen[c], page - sh.colFirst[c]);
}

void destroyPages(bool keepGlass = false) {
  if (s_firmwareConfirm) closeFirmwareConfirm(nullptr);
  lockWidgets();
  s_editorGrant = -1;
  inlineClimateFlush();  // Don't lose an unsent setpoint during page rebuild.
  clearGlassCache();
  edClose();
  closeModal();
  destroyQuickPanel();
  for (int i = 0; i < s_count; i++) {
    Page& p = s_pages[i];
    if (keepGlass && themeUsesWallpaper(s_theme) && p.glass && p.def && !p.glassKey.empty()) {
      GlassCacheEntry& c = s_glassCache[i];
      c.id = p.def->id;  // the config may be replaced before buildPages
      c.key.swap(p.glassKey);
      c.buffer = p.glass;
      p.glass = nullptr;
    }
    if (s_pages[i].root) lv_obj_delete(s_pages[i].root);
    if (s_pages[i].snap) lv_draw_buf_destroy(s_pages[i].snap);
    if (s_pages[i].glass) lv_draw_buf_destroy(s_pages[i].glass);
    s_pages[i] = {};
  }
  s_count = 0;
  for (TileRt& r : s_tiles) {
    if (r.pts) lv_free(r.pts);
  }
  s_tiles.clear();
  portENTER_CRITICAL(&s_mux);
  sh.count = 0;
  for (int i = 0; i < MAX_PAGES; i++) sh.valid[i] = false;
  portEXIT_CRITICAL(&s_mux);
}

void invalidateSwipeZones() { s_zonesDirty = true; }

void syncSwipeZones() {
  const int a = sh.active;
  if (!s_zonesDirty && a == s_zonesPage) return;
  s_zonesDirty = false;
  s_zonesPage = a;
  Shared::Zone z[MAX_PAGE_TILES];
  int n = 0;
  if (a >= 0 && a < s_count && s_pages[a].root) {
    lv_obj_update_layout(s_pages[a].root);
    lv_area_t root;
    lv_obj_get_coords(s_pages[a].root, &root);
    for (const TileRt& r : s_tiles) {
      if (r.page != a || n == MAX_PAGE_TILES) continue;
      lv_obj_t* control = r.fill ? r.obj :
          r.climDial && !r.off && !lv_obj_has_flag(r.climDial, LV_OBJ_FLAG_HIDDEN) ? r.climDial : nullptr;
      const bool volume = !r.off && r.haMediaVolume &&
                          !lv_obj_has_flag(r.haMediaVolume, LV_OBJ_FLAG_HIDDEN);
      if (volume) control = r.haMediaVolume;
      if (!control) continue;
      lv_area_t t;
      lv_obj_get_coords(control, &t);
      if (volume) {
        t.x1 -= 9;
        t.x2 += 9;
        t.y1 -= 9;
        t.y2 += 9;
      }
      z[n++] = {(int16_t)(t.x1 - root.x1), (int16_t)(t.y1 - root.y1), (int16_t)(t.x2 - root.x1),
                (int16_t)(t.y2 - root.y1)};
    }
  }
  portENTER_CRITICAL(&s_mux);
  memcpy(sh.zones, z, sizeof(Shared::Zone) * n);
  sh.nZones = n;
  portEXIT_CRITICAL(&s_mux);
}

void switchTo(int page) {
  const int w = logicalW();
  const int old = sh.active;
  if (page == old || page < 0 || page >= s_count) return;
  if (s_pinTarget != -1) dismissFirmwareConfirm();
  lockWidgets();
  lv_obj_set_x(s_pages[old].root, w);
  lv_obj_set_x(s_pages[page].root, 0);
  portENTER_CRITICAL(&s_mux);
  sh.active = page;
  sh.colCur[sh.col[page]] = page;
  portEXIT_CRITICAL(&s_mux);
  setPager(page);
  s_gen++;
  // New neighbours; cheap to re-take, and the page we left changed state.
  for (int i = 0; i < s_count; i++) s_pages[i].stale = true;
  qpMarkStale();  // Glass: the panel frosts the new page
}

void releaseSwipe() {
  portENTER_CRITICAL(&s_mux);
  sh.owned = false;
  portEXIT_CRITICAL(&s_mux);
}

// Before the page images are freed: no gesture may take them any more, and
// one the touch task (other core) took just before gets time to reach the
// compositor, so panelQuiesce cancels it.
void stopGestures() {
  if (frameActive() && !s_frameSync) {
    frameAbort();
    presenceWake();
  }
  portENTER_CRITICAL(&s_mux);
  for (int i = 0; i < MAX_PAGES; i++) sh.valid[i] = false;
#if defined(BOARD_IS_JC8012P4A1C)
  ++s_directEpoch;
  s_directOrigin = s_directTarget = -1;
#endif
  sh.panelValid = false;
  portEXIT_CRITICAL(&s_mux);
  vTaskDelay(2);
  panelQuiesce();
  releaseSwipe();
}

void refreshOneSnapshot() {
#if defined(BOARD_IS_JC8012P4A1C)
  if (s_displayTest == DeckDisplayTest::WithoutSnapshots) return;
#endif
  if (touchIsDown() || compMode() != COMP_NONE) return;
  if (themeUsesModernLayout(s_theme) && modalShown()) return;
  const uint32_t now = millis();
#if defined(BOARD_IS_JC8012P4A1C)
  // Background snapshot rendering must not overlap a just-posted LVGL
  // frame or immediately follow a gesture. The touch fallback can navigate
  // without a cached destination while this work is deferred.
  if (themeUsesModernLayout(s_theme) && !s_qp.open) {
    if (now - touchLastActivity() < 500) return;
    while (s_lvOut > 0 && panelWaitLvReady(0)) --s_lvOut;
    if (s_lvOut > 0) return;
  }
  const uint32_t spacing = themeUsesModernLayout(s_theme) && !s_qp.open
      ? CINEMATIC_SNAP_SPACING_MS : SNAP_SPACING_MS;
#else
  const uint32_t spacing = SNAP_SPACING_MS;
#endif
  if (now - s_lastSnapMs < spacing) return;
  // An open panel is what the next gesture slides; it goes first. So does a
  // panel with no image or an old one: busy pages (a print, gliding charts)
  // are always due and would keep it from ever opening.
  portENTER_CRITICAL(&s_mux);
  const bool panelOk = sh.panelValid;
  portEXIT_CRITICAL(&s_mux);
  if (s_qp.stale && (s_qp.open || !panelOk || now - s_panelSnapMs >= PANEL_SNAP_MS)) {
    s_panelSnapMs = now;
    refreshPanelSnapshot();
    return;
  }
  // The pages one gesture away first: next / previous column, then the
  // pages above and below in this column.
  const int a = sh.active;
  const int c = sh.col[a];
  int pick = -1;
  const int order[4] = {c + 1 < sh.nCols ? sh.colCur[c + 1] : -1, c > 0 ? sh.colCur[c - 1] : -1,
                        a + 1 < s_count && sh.col[a + 1] == c ? a + 1 : -1,
                        a > 0 && sh.col[a - 1] == c ? a - 1 : -1};
#if defined(BOARD_IS_JC8012P4A1C)
  const bool bounded = themeUsesModernLayout(s_theme);
  if (bounded) {
    // The compositor may hold raw pointers to snapshots. Invalidate an
    // evicted entry under the same lock used by gesture acquisition, and
    // never evict while a gesture or compositor animation owns a source.
    for (int p = 0; p < s_count; ++p) {
      if (p == a || !s_pages[p].snap) continue;
      bool nearby = false;
      for (int k = 0; k < 4; ++k) nearby |= order[k] == p;
      if (nearby) continue;
      lv_draw_buf_t* retired = nullptr;
      portENTER_CRITICAL(&s_mux);
      if (!sh.owned && !sh.busy[p]) {
        sh.valid[p] = false;
        sh.src[p] = {};
        retired = s_pages[p].snap;
        s_pages[p].snap = nullptr;
      }
      portEXIT_CRITICAL(&s_mux);
      if (retired) {
        lv_draw_buf_destroy(retired);
        s_pages[p].stale = true;
      }
    }
  }
#endif
  // The shown page swipes from the live frame; only closing the panel slides
  // off its image (showPanel marks it), so then it goes first.
  auto due = [&](int p) {
    if (p < 0 || !s_pages[p].stale || s_pages[p].wallBake || (p == a && !s_qp.open)) return false;
#if defined(BOARD_IS_JC8012P4A1C)
    if (bounded && p != a) {
      bool nearby = false;
      for (int k = 0; k < 4; ++k) nearby |= order[k] == p;
      if (!nearby) return false;
    }
#endif
#if defined(BOARD_IS_JC8012P4A1C)
    // Background pages with an existing image can wait before spending PSRAM bandwidth again.
    if (themeUsesModernLayout(s_theme) && p != a && sh.valid[p])
      return now - s_pages[p].snapMs >= CINEMATIC_PAGE_SNAP_MS;
#endif
    return true;
  };
  if (s_qp.open && due(a)) pick = a;
  // A page without an image cannot be swiped to at all.
  for (int k = 0; k < 4 && pick < 0; k++)
    if (due(order[k]) && !sh.valid[order[k]]) pick = order[k];
  for (int p = 0; p < s_count && pick < 0; p++)
    if (due(p) && !sh.valid[p]) pick = p;
  for (int k = 0; k < 4 && pick < 0; k++)
    if (due(order[k])) pick = order[k];
  for (int p = 0; p < s_count && pick < 0; p++)
    if (due(p)) pick = p;
  if (pick < 0) {
    if (s_qp.stale) s_panelSnapMs = now;
    refreshPanelSnapshot();
    return;
  }
  snapPage(pick);
}

bool snapPage(int pick) {
  Page& pg = s_pages[pick];
#if defined(BOARD_IS_JC8012P4A1C)
  if (!pg.snap && themeUsesModernLayout(s_theme)) {
    // Keep a failed allocation from being retried on every loop iteration.
    const uint32_t now = millis();
    static uint32_t retryAfter = 0;
    if (retryAfter && static_cast<int32_t>(now - retryAfter) < 0) return false;
    const size_t bytes = size_t(logicalW()) * logicalH() * 2;
    constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    if (heap_caps_get_largest_free_block(caps) < bytes + 4096 ||
        heap_caps_get_free_size(caps) < bytes + 1024 * 1024) {
      retryAfter = now + 2000;
      s_lastSnapMs = now;
      return false;
    }
    pg.snap = lv_draw_buf_create(logicalW(), logicalH(), LV_COLOR_FORMAT_RGB565, 0);
    if (!pg.snap) {
      retryAfter = now + 2000;
      s_lastSnapMs = now;
      return false;
    }
    retryAfter = 0;
  }
#endif
  if (!pg.snap) return false;

  portENTER_CRITICAL(&s_mux);
  const bool owned = sh.owned;
  if (!owned) sh.busy[pick] = true;
  portEXIT_CRITICAL(&s_mux);
  if (owned) return false;

  const uint32_t t0 = micros();
#if defined(BOARD_IS_JC8012P4A1C)
  const uint32_t underrunsBefore = p4Board::scanoutUnderruns();
#endif
  lv_snapshot_reshape_draw_buf(pg.root, pg.snap);
  const bool ok = lv_snapshot_take_to_draw_buf(pg.root, LV_COLOR_FORMAT_RGB565, pg.snap) == LV_RESULT_OK;
#if defined(BOARD_IS_JC8012P4A1C)
  s_snapshotUnderruns += p4Board::scanoutUnderruns() - underrunsBefore;
#endif
  const uint32_t dt = micros() - t0;
  if (dt > s_snapUsMax) s_snapUsMax = dt;

  portENTER_CRITICAL(&s_mux);
  sh.busy[pick] = false;
  sh.valid[pick] = ok;
  if (ok) sh.src[pick] = {(const uint16_t*)pg.snap->data, (uint16_t)(pg.snap->header.stride / 2), true};
  portEXIT_CRITICAL(&s_mux);
  pg.stale = !ok;
  pg.snapMs = s_lastSnapMs = millis();
  return ok;
}

void bakeWallpaperPage(int index) {
  Page& p = s_pages[index];
  const uint32_t start = millis();
  if (!bakeGlass(p)) {
    lv_obj_set_style_bg_image_src(p.root, wallSharp(), 0);
    lv_obj_invalidate(p.root);
  }
  p.wallBake = false;
  p.stale = true;
  debugSerial.printf("Deck: wallpaper page %d bake %lu ms\n", index, (unsigned long)(millis() - start));
}

void tickWallpaper() {
  const bool safe = s_count && themeUsesWallpaper(s_theme) && getScreenState() == SCREEN_DECK &&
                    !displaySleepingForTouch() && !manualDisplayOff() && currentBacklightLevel() &&
                    !touchIsDown() && !s_qp.open && !s_modalCard && !s_ed.root &&
                    s_ed.pending == EditState::P_NONE && compMode() == COMP_NONE && !sh.owned;
  const bool idle = millis() - touchLastActivity() >= WALL_IDLE_MS;
  bool baking = false;
  for (int i = 0; i < s_count; i++) baking |= s_pages[i].wallBake;
  if (wallPoll(logicalW(), logicalH(), idle, safe && !baking)) {
    stopGestures();
    for (int i = 0; i < s_count; i++) s_pages[i].stale = true;
    // A touch may have started while the compositor was being quiesced.
    if (!wallPoll(logicalW(), logicalH(), millis() - touchLastActivity() >= WALL_IDLE_MS, !touchIsDown())) return;
    wallCommit();
    if (themeUsesModernLayout(s_theme)) {
      // Cinematic draws tiles over the shared wallpaper. Give the live frame
      // time to present before making any off-screen navigation snapshots.
      for (int i = 0; i < s_count; ++i) {
        Page& p = s_pages[i];
        p.wallBake = false;
        lv_obj_set_style_bg_image_src(p.root, wallSharp(), 0);
        lv_obj_invalidate(p.root);
      }
      s_lastSnapMs = millis();
    } else {
      for (int i = 0; i < s_count; i++) s_pages[i].wallBake = true;
      bakeWallpaperPage(sh.active);
    }
    qpMarkStale();
    ++s_gen;
    return;
  }
  if (!safe || !baking) return;
  for (int i = 0; i < s_count; i++) {
    if (!s_pages[i].wallBake) continue;
    bakeWallpaperPage(i);
    break;
  }
}

// The colon of the shown page's clocks blinks with the second; other pages
// keep it lit, so their images stay valid.
void blinkClocks() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  const bool lit = tv.tv_sec < 1609459200 || tv.tv_usec < 500000;
  for (TileRt& r : s_tiles) {
    if (!r.colon) continue;
    const bool on = lit || r.page != sh.active;
    if (on == r.colonLit) continue;
    r.colonLit = on;
    lv_obj_set_style_text_opa(r.colon, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (r.page != sh.active) markPageStale(r.page);
  }
}

// Header clock and WiFi indicator, checked once a second; page images are
// only marked stale when something visible changed.
void updateHeaders() {
  const uint32_t now = millis();
  if (now - s_lastClockCheck < 1000) return;
  s_lastClockCheck = now;
  const auto updateState = firmwareUpdate::snapshot();
  // Requests from the local web UI only bring the existing LVGL physical
  // confirmation to the foreground; they never install or reboot remotely.
  if (s_pinMask && (millis() - s_pinOpened >= 120000 ||
      (s_pinTarget == -1 && (!updateState.staged || updateState.remoteInstalling ||
        strcmp(s_confirmVersion, updateState.version) != 0)) ||
      (s_pinTarget != -1 && s_pinPage != sh.active))) closeFirmwareConfirm(nullptr);
  if (s_pinTarget == -1 && !updateState.allowTouchUpdates &&
      !updateState.webConfirmationAuthorized && s_firmwareConfirm)
    dismissFirmwareConfirm();
  if (firmwareUpdate::takeTouchConfirmationRequest()) {
    dismissFirmwareConfirm();
    openFirmwareConfirmation(nullptr);
  }
  // Staging happens asynchronously in the updater task. Show the physical
  // install confirmation once as soon as the image is verified, even if the
  // optional header update badge (or the whole header) is disabled.
  if (!updateState.staged) {
    s_firmwarePrompted = false;
  } else if (updateState.allowTouchUpdates && !s_firmwarePrompted && updateState.promptOnStage) {
    // Only a manual Download OTA request interrupts the touchscreen.
    // Automatic downloads advertise readiness through the header icon.
    s_firmwarePrompted = true;
    openFirmwareConfirmation(nullptr);
  }
  for (int i = 0; i < s_count; ++i) {
    Page& page = s_pages[i];
    if (!page.updateBadge) continue;
    const bool visible = s_header && updateState.allowTouchUpdates && updateState.checksEnabled &&
      (updateState.showBadge || (updateState.staged && !updateState.promptOnStage)) &&
      (updateState.available || updateState.staged);
    const bool hidden = lv_obj_has_flag(page.updateBadge, LV_OBJ_FLAG_HIDDEN);
    if (visible == hidden) {
      if (visible) lv_obj_remove_flag(page.updateBadge, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(page.updateBadge, LV_OBJ_FLAG_HIDDEN);
      page.stale = true;
    }
  }
  const int wifi = WiFi.status() == WL_CONNECTED ? 1 : 0;
  if (wifi != s_lastWifi) {
    s_lastWifi = wifi;
    for (int i = 0; i < s_count; i++) {
      if (!s_pages[i].wifi) continue;
      lv_obj_set_style_text_color(s_pages[i].wifi, lv_color_hex(wifi ? C_OK : C_DIM), 0);
      s_pages[i].stale = true;
    }
  }
  const time_t epoch = time(nullptr);
  if (epoch < 1609459200) return;  // not synced yet
  struct tm lt;
  localtime_r(&epoch, &lt);
  const int minute = lt.tm_hour * 60 + lt.tm_min;
  if (minute == s_lastMinute) return;
  s_lastMinute = minute;
  char buf[12];
  if (netSettings.use24h) snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
  else snprintf(buf, sizeof(buf), "%d:%02d %s", (lt.tm_hour + 11) % 12 + 1, lt.tm_min,
                lt.tm_hour < 12 ? "AM" : "PM");
  for (int i = 0; i < s_count; i++) {
    if (!s_pages[i].clock || s_pages[i].hasClockTile) continue;
    lv_label_set_text(s_pages[i].clock, buf);
    s_pages[i].stale = true;
  }
  for (TileRt& r : s_tiles)
    if (r.def->type == TILE_CLOCK) applyClock(r);
    else if (r.def->type == TILE_WEATHER_CLOCK) applyWeatherClock(r);
}

// Pages are rebuilt only once nothing is in the user's hands.
bool canRebuild() { return !touchIsDown() && !s_ed.root && !s_modalCard && !s_qp.open && !s_firmwareConfirm; }

// The shown page stays by id, even when pages before it come or go.
void rebuildPages() {
  stopGestures();
  s_keepPage = activePageId();
  destroyPages(true);
  buildPages();
  s_gen++;
  lv_obj_invalidate(lv_screen_active());
}

String s_autoFrom;  // the page shown before an "auto" page took over
String s_autoTo;    // an "auto" page that appeared and is not shown yet
uint32_t s_autoUntil = 0;
constexpr uint32_t AUTO_WAIT_MS = 3000;

// The slide to an "auto" page needs its image: wait for it, then jump when it
// is late. Dropped if the user is busy with the screen all that time.
void autoNavTick() {
  if (!s_autoTo.length()) return;
  const bool late = (int32_t)(millis() - s_autoUntil) > 0;
  if (!canRebuild() || compMode() != COMP_NONE) {
    if (late) s_autoTo = "";
    return;
  }
  const int i = pageIndexOf(s_autoTo);
  if (i >= 0 && i != sh.active && !sh.valid[i] && !late) return;
  if (i >= 0 && i != sh.active) goToPage(s_autoTo);
  s_autoTo = "";
}


// A page condition rebuilds all pages; tile conditions re-place one page. A page
// that goes while shown is left first (back where an "auto" switch came from).
void whenSync() {
  whenTrack();
  autoNavTick();
  if (!canRebuild() || compMode() != COMP_NONE) return;
  const String cur = activePageId();
  String autoTo;
  bool flip = false;
  for (const PageDef& pd : s_cfg.pages) {
    if (pd.when.kind == WHEN_ALWAYS || whenWanted(pd) == pd.shown) continue;
    if (pd.shown && pd.id == cur) {
      int back = pageIndexOf(s_autoFrom);
      if (back < 0 || back == sh.active) back = 0;
      if (back != sh.active) {
        s_autoFrom = "";
        goToPage(s_pages[back].def->id);
        if (!s_frameSync) return;  // rebuilt once the slide is over
      }
    }
    if (!pd.shown && pd.autoShow && !autoTo.length()) autoTo = pd.id;
    debugSerial.printf("Deck: page '%s' %s\n", pd.id.c_str(), pd.shown ? "hides" : "shows");
    flip = true;
  }
  if (!flip) {
    for (PageDef& pd : s_cfg.pages) {
      bool moved = false;
      for (TileDef& t : pd.tiles) {
        if (whenWanted(t) == t.shown) continue;
        debugSerial.printf("Deck: tile '%s' %s\n", t.id.c_str(), t.shown ? "hides" : "shows");
        t.shown = !t.shown;
        moved = true;
      }
      const int page = moved ? pageIndexOf(pd.id) : -1;
      if (page >= 0) placePage(page);
    }
    return;
  }
  rebuildPages();
  if (autoTo.length()) {
    s_autoFrom = cur;
    s_autoTo = autoTo;
    s_autoUntil = millis() + AUTO_WAIT_MS;
  }
}

// Frame wake: what deckTick skipped while asleep runs under the picture, so
// the page that flies back in is current.
void frameCatchUp() {
  s_frameSync = true;
  updateHeaders();
  haSync();
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  bambuSync();
#endif
  inlineClimateTick();
  httpSync();
  whenSync();
  s_frameSync = false;
}

void logStats() {
  const uint32_t now = millis();
  if (now - s_lastStats < STATS_MS) return;
  s_lastStats = now;
  const PanelStats p = panelTakeStats();
#if defined(BOARD_IS_JC8012P4A1C)
  p4Trace::MainScope diagnostics(p4Trace::Stage::Diagnostics);
  static uint32_t previousUnderruns = 0;
  if (!p4Trace::verbose()) {
    previousUnderruns = p4Board::scanoutUnderruns();
    s_snapshotUnderruns = 0;
    s_waitTimeouts = 0;
    s_snapUsMax = 0;
    p4Trace::clearStats();
    p4Trace::takeInterval();
    return;
  }
  const uint32_t underruns = p4Board::scanoutUnderruns();
  if (underruns != previousUnderruns)
    debugSerial.printf("Deck: P4 scanout underruns=%u snapshot_underruns=%u\n",
                       (unsigned)(underruns - previousUnderruns), (unsigned)s_snapshotUnderruns);
  previousUnderruns = underruns;
  s_snapshotUnderruns = 0;
  p4Trace::report(now);
  const auto cadence = p4Trace::takeInterval();
  {
  p4Trace::MainScope output(p4Trace::Stage::DiagnosticOutput);
  const uint32_t dmaNow = micros();
  const uint32_t lastLate = cadence.lateFrames ? (dmaNow - cadence.lastLateUs) / 1000U : 0;
  debugSerial.printf("Deck: P4 DMA t=%ums max_gap_us=%u late_frames=%u last_late_ago_ms=%u last_late_us32=%u\n",
                     (unsigned)millis(), (unsigned)cadence.worstGapUs,
                     (unsigned)cadence.lateFrames, (unsigned)lastLate,
                     (unsigned)(cadence.lateFrames ? cadence.lastLateUs : 0));
  debugSerial.printf("Deck: P4 output t=%ums checked=%u mismatch_max=%u/64 native_partial=%u ppa_full=%u cpu_full=%u\n",
                     (unsigned)now, (unsigned)p.outputChecked, (unsigned)p.outputMismatchMax,
                     (unsigned)p.nativePartial, (unsigned)p.ppaFull, (unsigned)p.cpuFull);
  if (p4Board::hasPresentedFrame()) {
    static uint32_t previousBacklightWrites = 0;
    const auto bl = p4Board::backlightStats();
    debugSerial.printf("Deck: P4 backlight t=%ums level=%u duty=%u hz=%u writes=%u\n",
                       (unsigned)now, (unsigned)currentBacklightLevel(), (unsigned)bl.duty,
                       (unsigned)bl.frequencyHz, (unsigned)(bl.writes - previousBacklightWrites));
    previousBacklightWrites = bl.writes;
  }
  }
#endif
  if (!p.frames && !s_waitTimeouts) return;
#if defined(BOARD_IS_JC8012P4A1C)
  unsigned internalFree;
  {
    p4Trace::MainScope heap(p4Trace::Stage::HeapInspect);
    internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  }
  p4Trace::MainScope output(p4Trace::Stage::DiagnosticOutput);
#else
  const unsigned internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#endif
  debugSerial.printf("Deck: frames=%u comp=%u flush=%u/%uus build=%uus te_late=%u te_to=%u "
                "lv_drop=%u gap_max=%uus wait_to=%u snap_max=%uus int_free=%u\n",
                (unsigned)p.frames, (unsigned)p.compFrames, (unsigned)p.flushUsAvg,
                (unsigned)p.flushUsMax, (unsigned)p.buildUsAvg, (unsigned)p.teLate,
                (unsigned)p.teTimeouts, (unsigned)p.lvDropped, (unsigned)p.gapMaxUs,
                (unsigned)s_waitTimeouts, (unsigned)s_snapUsMax,
                internalFree);
  s_waitTimeouts = 0;
  s_snapUsMax = 0;
}

}  // namespace deckui

using namespace deckui;

static String s_deckBootSource;
static String s_deckBootError;
String deckBootConfigSource() { return s_deckBootSource; }
String deckBootConfigError() { return s_deckBootError; }

static void loadBootConfig() {
  String source, err;
  deckConfigLoad(s_cfg, &source, &err);
  s_deckBootSource = source;
  s_deckBootError = err;
  debugSerial.printf("Deck: config from %s, %u pages%s%s\n", source.c_str(),
                (unsigned)s_cfg.pages.size(), err.length() ? ", error: " : "",
                err.c_str());
}

// --- Public API ----------------------------------------------------------------------
bool deckBegin(uint8_t rotation) {
  if (!panelBegin(rotation)) return false;
#if !defined(BOARD_IS_JC8012P4A1C)
  loadBootConfig();
#endif
  lv_init();
  lv_tick_set_cb(tickCb);
  s_disp = lv_display_create(logicalW(), logicalH());
  lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(s_disp, panelLvBuffer(0), panelLvBuffer(1), FRAME_BYTES,
                         LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(s_disp, flushCb);
  lv_display_set_flush_wait_cb(s_disp, flushWaitCb);

  static const lv_style_prop_t kPressProps[] = {
    LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT, LV_STYLE_BG_COLOR, LV_STYLE_PROP_INV};
  lv_style_transition_dsc_init(&s_pressTrans, kPressProps, lv_anim_path_ease_out, 90, 0, nullptr);

  setPalette(dispSettings.theme);
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  lv_obj_set_style_text_font(scr, &deck_font_14, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  return true;
}

#if defined(BOARD_IS_JC8012P4A1C)
void deckShowBootSplash(const char* status) {
  sysScreenSync(SCREEN_SPLASH);
  sysSplashStatus(status);
  lv_timer_handler();
  lv_refr_now(s_disp);
  updateBacklightControl();
}
#endif

void deckLoadDashboard() {
#if defined(BOARD_IS_JC8012P4A1C)
  deckShowBootSplash("Reading dashboard layout...");
  loadBootConfig();
  deckShowBootSplash("Loading icons...");
#endif
  String err;
  if (!iconsLoad(iconsPath(), err)) debugSerial.printf("Deck: icons: %s\n", err.c_str());
  if (wallOnSd() && !brandLoad(err)) debugSerial.printf("Deck: brand icons: %s\n", err.c_str());
  {
    Preferences prefs;
    if (openOptionalPreferences(prefs, "deckui")) {
      s_header = prefs.getBool("hdr", true);
      prefs.end();
    }
  }
  haBegin();
#if defined(BOARD_IS_JC8012P4A1C)
  deckShowBootSplash("Building dashboard...");
#endif
  buildPages();
  s_indev = lv_indev_create();
  lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(s_indev, indevRead);
  lv_indev_set_long_press_time(s_indev, 450);
  touchBegin();
  debugSerial.printf("Deck: LVGL %d.%d.%d up, %d pages, %dx%d\n", LVGL_VERSION_MAJOR,
                LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, s_count, logicalW(), logicalH());
}

#if defined(BOARD_IS_JC8012P4A1C)
bool deckScanoutHoldActive() {
  return s_displayTest == DeckDisplayTest::Hold || s_displayTest == DeckDisplayTest::Replay ||
         s_displayTest == DeckDisplayTest::DmaReload;
}

bool deckBeginDisplayTest(DeckDisplayTest test) {
  // The boot-selected live driver cannot be reinitialised or temporarily
  // switched back to an interrupt-driven diagnostic without rebooting.
  if (p4Board::liveReloadActive()) return test == DeckDisplayTest::DmaLiveReload;
  if (test == DeckDisplayTest::None || !s_disp || s_displayTest != DeckDisplayTest::None ||
      getScreenState() != SCREEN_DECK ||
      !currentBacklightLevel() || !p4Board::hasPresentedFrame() || frameActive() ||
      !canRebuild() || compMode() != COMP_NONE) return false;

  const bool pauseSnapshots = test == DeckDisplayTest::WithoutSnapshots;
  const bool partialScanout = test == DeckDisplayTest::PartialScanout;
  const bool cpuScanout = test == DeckDisplayTest::CpuScanout;
  const bool liveScanoutTest = partialScanout || cpuScanout;
  portENTER_CRITICAL(&s_mux);
  const bool owned = sh.owned;
  if (!owned && !pauseSnapshots && !liveScanoutTest) sh.swipeEnabled = false;
  portEXIT_CRITICAL(&s_mux);
  if (owned) return false;
  s_displayTest = test;
  const bool replay = test == DeckDisplayTest::Replay;
  const bool renderOnly = test == DeckDisplayTest::RenderOnly;
  if (liveScanoutTest) {
    panelSetScanoutPath(cpuScanout ? PanelScanoutPath::Cpu : PanelScanoutPath::Partial);
    if (cpuScanout) lv_obj_invalidate(lv_screen_active());
  } else if (!pauseSnapshots) {
    panelBeginScanoutHold(renderOnly ? ScanoutHold::RenderOnly :
                          replay ? ScanoutHold::Replay : ScanoutHold::Static);
  }
  if (test == DeckDisplayTest::DmaLiveReload) {
    if (!p4Board::testLiveReload()) {
      panelEndScanoutHold();
      s_displayTest = DeckDisplayTest::None;
      lv_obj_invalidate(lv_screen_active());
      debugSerial.println("Deck: P4 live DMA reload refused (normal driver retained)");
      return false;
    }
    panelSetScanoutPath(PanelScanoutPath::Ppa);
    panelEndScanoutHold();
    lv_obj_invalidate(lv_screen_active());
    debugSerial.println("Deck: P4 live DMA reload active; staged band copies; reboot to exit");
  }
  if (test == DeckDisplayTest::DmaReload) {
    if (!p4Board::testStaticReload()) {
      panelEndScanoutHold();
      s_displayTest = DeckDisplayTest::None;
      lv_obj_invalidate(lv_screen_active());
      debugSerial.println("Deck: P4 DMA reload test refused (normal driver retained)");
      return false;
    }
    debugSerial.println("Deck: P4 DMA reload test active; fixed frame; stop or reboot to exit");
  }
  s_displayTestStarted = millis();
  s_displayTestFrames = p4Board::scanoutFrames();
  s_displayTestUnderruns = p4Board::scanoutUnderruns();
  s_displayTestSubmitted = 0;
  if (test == DeckDisplayTest::DmaReload || test == DeckDisplayTest::DmaLiveReload) {
    return true;  // No 60-second timer or interrupt-based frame count.
  }
  if (pauseSnapshots) {
    debugSerial.printf("Deck: P4 snapshot pause start t=%ums duration=%us\n",
                       (unsigned)s_displayTestStarted, (unsigned)(SNAPSHOT_TEST_MS / 1000));
  } else {
    debugSerial.printf("Deck: P4 scanout %s start t=%ums duration=%us\n",
                       cpuScanout ? "CPU full-frame test" : partialScanout ? "partial test" :
                       renderOnly ? "render-only" : replay ? "replay" : "hold",
                       (unsigned)s_displayTestStarted,
                       (unsigned)((cpuScanout ? CPU_SCANOUT_TEST_MS : SCANOUT_HOLD_MS) / 1000));
  }
  return true;
}
#endif

#if defined(BOARD_IS_JC8012P4A1C)
static bool s_displayTestStopRequested = false;
void deckStopDisplayTest() {
  if (s_displayTest != DeckDisplayTest::None || p4Board::liveReloadActive())
    s_displayTestStopRequested = true;
}
#endif

void deckTick() {
  if (!s_disp) return;
#if defined(BOARD_IS_JC8012P4A1C)
  const bool stopTest = s_displayTestStopRequested;
  s_displayTestStopRequested = false;
  if (p4Board::liveReloadActive() && stopTest) {
    p4Board::setDmaBootEnabled(false);
    p4Board::blankBacklightForRestart();
    ESP.restart();
    return;
  }
  if (s_displayTest == DeckDisplayTest::DmaReload) {
    // DMA frame interrupts are intentionally disabled in this mode. Never
    // release the panel hold into the old driver; a reboot restores it.
    if (stopTest) {
      p4Board::blankBacklightForRestart();
      ESP.restart();
    }
    return;
  }
  if (deckScanoutHoldActive()) {
    const uint32_t now = millis();
    if (!stopTest && now - s_displayTestStarted < SCANOUT_HOLD_MS) return;
    const auto stats = panelEndScanoutHold();
    debugSerial.printf("Deck: P4 scanout %s end t=%ums scanout_frames=%u underruns=%u replayed=%u failed=%u\n",
                       s_displayTest == DeckDisplayTest::Replay ? "replay" : "hold", (unsigned)millis(),
                       (unsigned)(p4Board::scanoutFrames() - s_displayTestFrames),
                       (unsigned)(p4Board::scanoutUnderruns() - s_displayTestUnderruns),
                       (unsigned)stats.presented, (unsigned)stats.failed);
    s_displayTest = DeckDisplayTest::None;
    touchSuppressUntilRelease();
    lv_obj_invalidate(lv_screen_active());
  }
  if (s_displayTest == DeckDisplayTest::RenderOnly &&
      (stopTest || millis() - s_displayTestStarted >= SCANOUT_HOLD_MS || getScreenState() != SCREEN_DECK)) {
    const auto stats = panelEndScanoutHold();
    debugSerial.printf("Deck: P4 scanout render-only end t=%ums scanout_frames=%u underruns=%u rendered=%u\n",
                       (unsigned)millis(), (unsigned)(p4Board::scanoutFrames() - s_displayTestFrames),
                       (unsigned)(p4Board::scanoutUnderruns() - s_displayTestUnderruns),
                       (unsigned)stats.rendered);
    s_displayTest = DeckDisplayTest::None;
    touchSuppressUntilRelease();
    lv_obj_invalidate(lv_screen_active());
  }
  const bool cpuScanout = s_displayTest == DeckDisplayTest::CpuScanout;
  if ((s_displayTest == DeckDisplayTest::PartialScanout || cpuScanout) &&
      (stopTest || millis() - s_displayTestStarted >= (cpuScanout ? CPU_SCANOUT_TEST_MS : SCANOUT_HOLD_MS) ||
       getScreenState() != SCREEN_DECK)) {
    panelSetScanoutPath(PanelScanoutPath::Ppa);
    debugSerial.printf("Deck: P4 scanout %s test end t=%ums submitted=%u scanout_frames=%u underruns=%u\n",
                       cpuScanout ? "CPU full-frame" : "partial", (unsigned)millis(), (unsigned)s_displayTestSubmitted,
                       (unsigned)(p4Board::scanoutFrames() - s_displayTestFrames),
                       (unsigned)(p4Board::scanoutUnderruns() - s_displayTestUnderruns));
    s_displayTest = DeckDisplayTest::None;
  }
  if (s_displayTest == DeckDisplayTest::WithoutSnapshots) {
    const uint32_t now = millis();
    if (stopTest || now - s_displayTestStarted >= SNAPSHOT_TEST_MS) {
      debugSerial.printf("Deck: P4 snapshot pause end t=%ums submitted=%u scanout_frames=%u underruns=%u\n",
                         (unsigned)now, (unsigned)s_displayTestSubmitted,
                         (unsigned)(p4Board::scanoutFrames() - s_displayTestFrames),
                         (unsigned)(p4Board::scanoutUnderruns() - s_displayTestUnderruns));
      s_displayTest = DeckDisplayTest::None;
    }
  }
#endif

  int8_t landing;
  if (compTakeLanding(&landing)) {
    if (landing) {
      switch (sh.gesture) {
        case Shared::GEST_PAGE: switchTo(sh.target); break;
        case Shared::GEST_PANEL_OPEN: showPanel(true); break;
        case Shared::GEST_PANEL_CLOSE: showPanel(false); break;
      }
      compSetHoldGen(s_gen);
    }
    releaseSwipe();
  }
  qpServiceShortcut();
  portENTER_CRITICAL(&s_mux);
  const int8_t livePanel = s_livePanelRequest;
  s_livePanelRequest = 0;
  portEXIT_CRITICAL(&s_mux);
  if (livePanel && getScreenState() == SCREEN_DECK && !frameActive() &&
      !s_modalCard && !s_ed.root && !s_firmwareConfirm && compMode() == COMP_NONE)
    showPanel(livePanel > 0);
#if defined(BOARD_IS_JC8012P4A1C)
  // A swipe without a cached image still navigates on release. Never call
  // LVGL from the touch task; recheck ownership and the original page here.
  if (!touchIsDown() && compMode() == COMP_NONE) {
    int direct = -1;
    portENTER_CRITICAL(&s_mux);
    if (s_directTarget >= 0 && s_directOrigin == sh.active &&
        s_directTarget < sh.count && sh.swipeEnabled && !sh.owned &&
        !sh.panelOpen && !sh.modalOpen) direct = s_directTarget;
    s_directOrigin = s_directTarget = -1;
    portEXIT_CRITICAL(&s_mux);
    if (direct >= 0 && getScreenState() == SCREEN_DECK && !frameActive() &&
        !s_ed.root && !modalShown()) switchTo(direct);
  }
#endif
  // The input callback cancels the press. Clear its visual feedback here.
  if (touchTakeSwipeStarted()) fxCancelRipples();
  if (s_homeTapPending) {
    s_homeTapPending = false;
    if (s_homeTapPage == sh.active && s_count > 0 && !touchIsDown() &&
        !frameActive() && !s_qp.open && !s_modalCard && !s_ed.root &&
        getScreenState() == SCREEN_DECK && compMode() == COMP_NONE &&
        !s_firmwareConfirm && s_pages[0].def) {
      goToPage(s_pages[0].def->id);
    }
  }
  panelSetPageFx(fxPage());
  // Cascade moves each page's grid columns; the header above moves as one.
  {
    const int orient = logicalW() > logicalH() ? ORIENT_LANDSCAPE : ORIENT_PORTRAIT;
    uint8_t cols[MAX_PAGES] = {};
    int16_t top[MAX_PAGES] = {};
    uint16_t cuts[MAX_PAGES] = {};
    const int w = logicalW();
    for (int i = 0; i < s_count && i < MAX_PAGES; i++) {
      const Page& pg = s_pages[i];
      cols[i] = pg.grid && pg.def ? pg.def->cols[orient] : 0;
      top[i] = pg.grid ? (int16_t)(lv_obj_get_y(pg.grid) + 4) : 0;
      cuts[i] = cols[i] > 1 ? (uint16_t)(((1u << cols[i]) - 1) & ~1u) : 0;  // edges 1 .. cols-1
    }
    // Same edge formula as the compositor; a tile across one drops that cut.
    for (const TileRt& r : s_tiles) {
      if (r.page < 0 || r.page >= MAX_PAGES || cols[r.page] < 2 || r.def->parent.length()) continue;
      const int n = cols[r.page], x1 = lv_obj_get_x(r.obj), x2 = x1 + lv_obj_get_width(r.obj);
      const float cw = (float)(w - 16 - 7 * (n - 1)) / n;
      for (int k = 1; k < n; k++) {
        const int e = 8 + (int)lroundf(k * (cw + 7)) - 4;
        if (x1 < e && e < x2) cuts[r.page] &= (uint16_t)~(1u << k);
      }
    }
    // No usable cut at all would mean "all" to the compositor: one band then.
    for (int i = 0; i < MAX_PAGES; i++)
      if (cols[i] > 1 && !cuts[i]) cols[i] = 1;
    portENTER_CRITICAL(&s_mux);
    memcpy(sh.gridCols, cols, sizeof(cols));
    memcpy(sh.gridTop, top, sizeof(top));
    memcpy(sh.gridCuts, cuts, sizeof(cuts));
    portEXIT_CRITICAL(&s_mux);
  }

  if (touchTakeActivity() && (displaySleepingForTouch() || frameActive())) {
    wakeDisplayFromTouch();
    touchSuppressUntilRelease();
  }
  bool renderOnly = false;
#if defined(BOARD_IS_JC8012P4A1C)
  renderOnly = s_displayTest == DeckDisplayTest::RenderOnly;
#endif
  if (!renderOnly) {
    presenceTick();
    frameTick();
  }
  const bool screenLocked = displaySleepingForTouch() || manualDisplayOff() ||
      frameActive() || getScreenState() != SCREEN_DECK;
  if (s_widgetUnlocked >= 0 && (screenLocked ||
      size_t(s_widgetUnlocked) >= s_tiles.size() || !widgetPinGranted(s_tiles[s_widgetUnlocked])))
    lockWidgets();
  if (s_pinTarget != -1 && screenLocked) dismissFirmwareConfirm();
  if (s_ed.root && editorNeedsPin() &&
      (screenLocked || s_editorRevision != deckAuth::widgetPinRevision() ||
       millis() - touchLastActivity() >= 120000)) edClose();

  // System screens (splash, WiFi setup, OTA) cover the pages: no gestures,
  // no panel or card under them.
  const ScreenState sc = getScreenState();
  const bool systemScreen = sc != SCREEN_DECK;
  if (systemScreen && (sh.swipeEnabled || s_qp.open || s_modalCard || s_ed.root)) {
    compCancel();
    releaseSwipe();
    hidePanelNow();
    closeModal();
    edClose();
  }
  portENTER_CRITICAL(&s_mux);
  sh.swipeEnabled = !systemScreen && !frameActive() && !renderOnly && !s_firmwareConfirm;
  portEXIT_CRITICAL(&s_mux);
  p4Trace::run(p4Trace::Stage::SystemScreen, [&] { sysScreenSync(sc); });
  // Once per boot, when the deck is first visible.
  static bool sdWarned = false;
  if (!sdWarned && !systemScreen && currentBacklightLevel() && !compOwnsScreen() && wallSdError()[0]) {
    sdWarned = true;
    deckToastShow("microSD card not working, using internal storage", 6000);
  }

  if (compOwnsScreen()) return;  // LVGL stays frozen during a drag or slide

  p4Trace::run(p4Trace::Stage::Mqtt, [&] { mqttTick(); });
  if (compOwnsScreen()) return;

  if (frameActive()) {
    frameTick();
    if (!compOwnsScreen()) {
      dailyBarSync();
      p4Trace::run(p4Trace::Stage::Render, [&] { lv_timer_handler(); });
    }
    return;
  }

  p4Trace::run(p4Trace::Stage::TileImages, [systemScreen] {
    // Update the artwork in place, on the same loop task as the web server.
    if (canRebuild() && compMode() == COMP_NONE) {
      if (!s_changedTilePngs.empty()) {
        refreshTilePngs(s_changedTilePngs);
        s_changedTilePngs.clear();
      }
      if (!systemScreen && wallOnSd()) retryTilePngs();
    }
  });
  if (s_ed.pending != EditState::P_NONE) edFinish();
  p4Trace::run(p4Trace::Stage::Wallpaper, [&] { tickWallpaper(); });
  p4Trace::run(p4Trace::Stage::UiHousekeeping, [&] {
    dailyBarSync();
    updateHeaders();
    blinkClocks();
    animateWeatherClocks();
    tickHaMediaSheet();
    qpUpdateStatus(false);
  });
  p4Trace::run(p4Trace::Stage::HomeAssistant, [&] { haSync(); });
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  // Printer state is updated by a worker on the other core. Normal deck
  // frames must sync it too; frameCatchUp() only runs after a photo frame wake.
  p4Trace::run(p4Trace::Stage::PrinterUi, [&] { bambuSync(); });
#endif
  p4Trace::run(p4Trace::Stage::ClimateHttp, [] {
    inlineClimateTick();
    httpSync();
  });
  p4Trace::run(p4Trace::Stage::Layout, [] {
    whenSync();
    syncSwipeZones();
  });
  p4Trace::run(p4Trace::Stage::Render, [&] { lv_timer_handler(); });
  if (!systemScreen) p4Trace::run(p4Trace::Stage::Snapshots, [&] { refreshOneSnapshot(); });
  logStats();
}

void deckToast(const char* text) {
  if (s_disp) deckToastShow(text, 1800);
}

void deckBedroomIconsChanged(const String& name) {
  if (std::find(s_changedTilePngs.begin(), s_changedTilePngs.end(), name) ==
      s_changedTilePngs.end())
    s_changedTilePngs.push_back(name);
}

void deckRefreshNow() {
  if (s_disp && !compOwnsScreen()) lv_timer_handler();
}

void deckApplyRotation(uint8_t rotation) {
#if defined(BOARD_IS_JC8012P4A1C)
  // The BSP configures PPA and touch rotation at panel startup.
  if (s_disp && (rotation & 3) != panelRotation())
    debugSerial.println("Display: P4 rotation will apply on restart");
  return;
#endif
  wallInvalidateJobs();
  if (!s_disp) return;
  rotation &= 3;
  if (rotation == panelRotation()) return;
  stopGestures();
  destroyPages();
  // set_buffers restarts LVGL on buffer 0: no frame of ours may be in flight.
  while (s_lvOut > 0 && panelWaitLvReady(250)) s_lvOut--;
  s_lvOut = 0;
  panelSetRotation(rotation);
  lv_display_set_resolution(s_disp, logicalW(), logicalH());
  lv_display_set_buffers(s_disp, panelLvBuffer(0), panelLvBuffer(1), FRAME_BYTES,
                         LV_DISPLAY_RENDER_MODE_DIRECT);
  buildPages();
  sysScreenRelayout();
  s_gen++;
  lv_obj_invalidate(lv_screen_active());
}

void deckApplyTheme(uint8_t theme) {
  wallInvalidateJobs();
  if (!s_disp || theme == s_theme) return;
  stopGestures();
  destroyPages();
  setPalette(theme);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(C_BG), 0);
  buildPages();
  s_gen++;
  lv_obj_invalidate(lv_screen_active());
}

void deckReloadWallpaper(bool fileChanged) {
  wallInvalidateJobs();
  if (!s_disp || !themeUsesWallpaper(s_theme)) return;
  stopGestures();
  destroyPages();
  if (fileChanged) wallFree();  // else wallLoad re-blurs only when blur or dimming changed
  buildPages();
  s_gen++;
  lv_obj_invalidate(lv_screen_active());
}

bool deckBenchRender(int n, uint32_t& avgUs, uint32_t& maxUs) {
  if (frameActive()) return false;
  if (!s_disp || !s_count) return false;
  const int a = sh.active;
  Page& pg = s_pages[a];
  if (!pg.snap) return false;
  portENTER_CRITICAL(&s_mux);
  const bool owned = sh.owned;
  if (!owned) {
    sh.busy[a] = true;
    sh.valid[a] = false;
  }
  portEXIT_CRITICAL(&s_mux);
  if (owned) return false;
  lv_obj_update_layout(pg.root);
  uint64_t sum = 0;
  maxUs = 0;
  n = constrain(n, 1, 20);
  for (int i = 0; i < n; i++) {
    const uint32_t t0 = micros();
    lv_snapshot_reshape_draw_buf(pg.root, pg.snap);
    lv_snapshot_take_to_draw_buf(pg.root, LV_COLOR_FORMAT_RGB565, pg.snap);
    const uint32_t dt = micros() - t0;
    sum += dt;
    if (dt > maxUs) maxUs = dt;
  }
  avgUs = (uint32_t)(sum / n);
  portENTER_CRITICAL(&s_mux);
  sh.busy[a] = false;
  portEXIT_CRITICAL(&s_mux);
  pg.stale = true;
  return true;
}

bool deckApplyConfig(const String& text, String& err) {
  if (!s_disp) {
    err = "display not running";
    return false;
  }
  const uint32_t saveStart = millis();
#if defined(BOARD_IS_JC8012P4A1C)
  const uint32_t underrunsBeforeSave = p4Board::scanoutUnderruns();
#endif
  DeckConfig next;
  if (!deckConfigStore(text, next, err)) return false;
  const uint32_t renderStart = millis();
#if defined(BOARD_IS_JC8012P4A1C)
  const uint32_t underrunsAfterSave = p4Board::scanoutUnderruns();
#endif
  // Tiles point into s_cfg: tear the pages down before replacing it.
  stopGestures();
  s_keepPage = activePageId();
  destroyPages(true);
  s_cfg = std::move(next);
  buildPages();
  s_deckBootSource = "/deck.json";
  s_deckBootError = "";
  s_gen++;
  lv_obj_invalidate(lv_screen_active());
  debugSerial.printf("Deck: new config applied, %u pages (save %lu ms, rebuild %lu ms)\n",
                     (unsigned)s_cfg.pages.size(), (unsigned long)(renderStart - saveStart),
                     (unsigned long)(millis() - renderStart));
#if defined(BOARD_IS_JC8012P4A1C)
  debugSerial.printf("Deck: P4 page save scanout underruns (storage %lu, rebuild %lu)\n",
                     (unsigned long)(underrunsAfterSave - underrunsBeforeSave),
                     (unsigned long)(p4Board::scanoutUnderruns() - underrunsAfterSave));
#endif
  return true;
}

#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
void deckRefreshPrinterPage() {
  if (!s_disp) return;
  String saved, error;
  deckConfigText(saved);
  DeckConfig next;
  if (!deckConfigParse(saved.c_str(), saved.length(), next, &error)) {
    debugSerial.printf("Deck: printer page refresh rejected: %s\n", error.c_str());
    return;
  }
  // Generated Bambu tiles exist only in memory. Keep the currently shown
  // page by ID, so a printer being enabled/disabled doesn't jump the user.
  s_keepPage = activePageId();
  stopGestures();
  destroyPages(true);
  s_cfg = next;
  buildPages();
  s_gen++;
  lv_obj_invalidate(lv_screen_active());
  debugSerial.printf("Deck: printer configuration changed, %u pages\n",
                (unsigned)s_cfg.pages.size());
}
#endif

String deckLedEntity() { return s_cfg.ledEntity; }

bool deckSetLedEntity(const String& id, String& err) {
  if (id == s_cfg.ledEntity) return true;
  String text;
  deckConfigText(text);
  JsonDocument doc(psramJson());
  if (deserializeJson(doc, text)) {
    err = "stored deck.json is not valid JSON";
    return false;
  }
  if (id.length()) doc["led"]["entity"] = id;
  else doc.remove("led");
  text = "";
  serializeJson(doc, text);
  return deckApplyConfig(text, err);
}

namespace deckui {
void setHeader(bool on) {
  if (on == s_header) return;
  if (frameActive()) stopGestures();
  s_header = on;
  Preferences prefs;
  if (prefs.begin("deckui", false)) {
    prefs.putBool("hdr", on);
    prefs.end();
  }
  for (int i = 0; i < s_count; i++) layoutHeader(s_pages[i]);
  if (s_qp.headerBtn) {
    if (on) lv_obj_add_state(s_qp.headerBtn, LV_STATE_CHECKED);
    else lv_obj_remove_state(s_qp.headerBtn, LV_STATE_CHECKED);
    qpMarkStale();
  }
}
}  // namespace deckui

void deckSetHeader(bool on) {
  if (s_disp) setHeader(on);
}

bool deckHeaderVisible() { return s_header; }

bool deckInstallIcons(const char* uploaded, String& err) {
  if (!s_disp) {
    err = "display not running";
    return false;
  }
  // The pages hold the current fonts: tear them down before the swap.
  stopGestures();
  destroyPages();
  const bool ok = iconsLoad(uploaded, err);
  if (ok) {
    LittleFS.remove(iconsPath());
    if (!LittleFS.rename(uploaded, iconsPath())) err = "font loaded but not stored";
  } else {
    LittleFS.remove(uploaded);
  }
  buildPages();
  s_gen++;
  lv_obj_invalidate(lv_screen_active());
  return ok && !err.length();
}

void deckReleaseIcons() {
  if (!s_disp) {
    iconsUnload();
    return;
  }
  stopGestures();
  destroyPages();
  iconsUnload();
  buildPages();
  s_gen++;
  lv_obj_invalidate(lv_screen_active());
}

bool deckInstallBrand(const char* font, const char* names, String& err) {
  if (!s_disp) { err = "display not running"; return false; }
  stopGestures();
  destroyPages();
  const bool ok = brandInstall(font, names, err);
  buildPages();
  s_gen++;
  lv_obj_invalidate(lv_screen_active());
  return ok;
}

bool deckRemoveBrand(String& err) {
  if (!s_disp) return brandRemove(err);
  stopGestures();
  destroyPages();
  const bool ok = brandRemove(err);
  buildPages();
  s_gen++;
  lv_obj_invalidate(lv_screen_active());
  return ok;
}

bool deckTestCard(const String& tileId) {
  if (frameActive()) return false;
  if (s_ed.root) return false;  // the editor blocks gestures until it closes
  for (size_t i = 0; i < s_tiles.size(); i++) {
    if (s_tiles[i].def->id != tileId) continue;
    if (!tilePinAccess(s_tiles[i], false) || s_tiles[i].def->requirePin) return false;
    openModal((int)i);
    return true;
  }
  return false;
}

bool deckTestTap(const String& tileId, bool hold) {
  if (frameActive()) return false;
  if (s_ed.root) return false;
  for (TileRt& r : s_tiles) {
    if (r.def->id != tileId || r.def->type == TILE_CLOCK || r.def->type == TILE_WEATHER_CLOCK) continue;
    if (r.def->requirePin) return false; // bench endpoint cannot use a physical grant
    const TileAction& a = hold ? r.def->hold : r.def->tap;
    requestAction(r, hold);
    return true;
  }
  return false;
}

bool deckTestSlide(const String& tileId, int pct) {
  if (frameActive()) return false;
  for (TileRt& r : s_tiles) {
    if (r.def->id != tileId || !r.fill) continue;
    if (r.def->requirePin) return false;
    const float f = constrain(pct, 0, 100) / 100.0f;
    return setWish(r.def->entity, true, f);
  }
  return false;
}

bool deckTestConfirm(bool ok) {
  if (s_confirmHold < 0 || s_modalTile < 0 || (size_t)s_modalTile >= s_tiles.size()) return false;
  const TileDef& d = *s_tiles[s_modalTile].def;
  if (d.requirePin) return false;
  const TileAction& a = s_confirmHold ? d.hold : d.tap;
  if (ok) onConfirmOk(nullptr);
  else closeModal();
  return true;
}

bool deckTestEdit(const String& op, int tile, int x, int y, int w, int h) {
  if (editorNeedsPin()) return false;
  if (frameActive()) return false;
  if (!s_disp) return false;
  if (op == "open") {
    edOpen(sh.active);
    return s_ed.root != nullptr;
  }
  if (!s_ed.root) return false;
  if (op == "done" || op == "cancel") {
    s_ed.pending = op == "done" ? EditState::P_SAVE : EditState::P_CANCEL;
    return true;
  }
  // Picker: pick = open it on group `tile`, pickrow = add row `tile`, pickclose.
  if (op == "pick") {
    if (s_pick) pickFill(tile, x > 0 ? x : 0);  // x = first row, for paging
    else pickOpen(tile);
    return s_pick != nullptr;
  }
  if (op == "pickrow") {
    if (!s_pick || tile < 0 || tile >= (int)s_pickRows.size()) return false;
    const bool ok = edAdd(s_pickRows[tile].id, s_pickRows[tile].name);
    if (ok) pickClose();
    return ok;
  }
  if (op == "pickclose") {
    pickClose();
    return true;
  }
  if (tile < 0 || tile >= (int)s_ed.tiles.size() || !s_ed.tiles[tile].box || s_ed.tiles[tile].removed)
    return false;
  if (op == "remove") {
    edRemove(tile);
    return true;
  }
  if (op != "move") return false;
  const Cell& cur = s_ed.tiles[tile].c;
  const Cell c = {true, x, y, w > 0 ? w : cur.w, h > 0 ? h : cur.h};
  const bool resize = c.w != cur.w || c.h != cur.h;
  if (!edApply(tile, c, resize)) return false;
  edLayout();
  return true;
}

bool deckTestSwipe(int dir, bool vertical) {
  if (!s_disp || (dir != 1 && dir != -1)) return false;
  return slideBy(dir, vertical);
}

bool deckTestPanel(bool open) {
  if (!s_disp || open == s_qp.open) return false;
  if (open && !s_qp.snap && getScreenState() == SCREEN_DECK && !frameActive() && !s_modalCard && !s_ed.root) {
    showPanel(true);
    return true;
  }
  if (!open) {
    closePanelAnimated();
    return true;
  }
  Src panel, page;
  int so, ph;
  if (!uiTryBeginPanel(true, panel, page, so, ph)) return false;
  if (!compBeginPanel(panel, page, so)) {
    releaseSwipe();
    return false;
  }
  compRelease(ph, 240, 1);
  return true;
}

namespace deck {

bool uiTryBeginSwipe(int dir, bool vertical, Src& a, Src& b, int& startOff, int& idxA) {
  bool ok = false;
  portENTER_CRITICAL(&s_mux);
  const int cur = sh.active;
  const int c = cur < sh.count ? sh.col[cur] : 0;
  int target = -1, idxCur = 0, idxTarget = 0;
  if (vertical) {
    target = cur + dir;
    if (target < 0 || target >= sh.count || sh.col[target] != c) target = -1;
    idxCur = cur - sh.colFirst[c];
    idxTarget = idxCur + dir;
  } else if (c + dir >= 0 && c + dir < sh.nCols) {
    target = sh.colCur[c + dir];
    idxCur = c;
    idxTarget = c + dir;
  }
  if (sh.swipeEnabled && !sh.owned && !sh.panelOpen && !sh.modalOpen &&
      target >= 0 && sh.valid[target] && !sh.busy[target]) {
    sh.owned = true;
    sh.gesture = Shared::GEST_PAGE;
    sh.target = target;
    // The live LVGL frame; both carry their page's grid for the cascade.
    const Src live = {nullptr, 0, true, 0, sh.gridCols[cur], sh.gridTop[cur], sh.gridCuts[cur]};
    if (dir > 0) {
      a = live;
      b = sh.src[target];
      b.cols = sh.gridCols[target];
      b.top = sh.gridTop[target];
      b.cuts = sh.gridCuts[target];
      startOff = 0;
      idxA = idxCur;
    } else {
      a = sh.src[target];
      a.cols = sh.gridCols[target];
      a.top = sh.gridTop[target];
      a.cuts = sh.gridCuts[target];
      b = live;
      startOff = vertical ? logicalH() : logicalW();
      idxA = idxTarget;
    }
    ok = true;
  }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

#if defined(BOARD_IS_JC8012P4A1C)
bool uiUncachedSwipeTarget(int dir, bool vertical, int& origin, int& target, uint32_t& epoch) {
  bool ok = false;
  portENTER_CRITICAL(&s_mux);
  const int cur = sh.active;
  if (sh.swipeEnabled && !sh.owned && !sh.panelOpen && !sh.modalOpen &&
      cur >= 0 && cur < sh.count && (dir == -1 || dir == 1)) {
    const int c = sh.col[cur];
    int next = -1;
    if (vertical) {
      const int p = cur + dir;
      if (p >= 0 && p < sh.count && sh.col[p] == c) next = p;
    } else if (c + dir >= 0 && c + dir < sh.nCols) next = sh.colCur[c + dir];
    if (next >= 0 && next < sh.count && (!sh.valid[next] || sh.busy[next])) {
      origin = cur;
      target = next;
      epoch = s_directEpoch;
      ok = true;
    }
  }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

void uiQueueUncachedSwipe(int origin, int target, uint32_t epoch) {
  portENTER_CRITICAL(&s_mux);
  if (epoch == s_directEpoch && origin == sh.active && target >= 0 &&
      target < sh.count && sh.swipeEnabled && !sh.owned && !sh.panelOpen && !sh.modalOpen) {
    s_directOrigin = origin;
    s_directTarget = target;
  }
  portEXIT_CRITICAL(&s_mux);
}
#endif

void uiSwipeAborted() { releaseSwipe(); }

bool uiSwipeOwnsTouch() {
  portENTER_CRITICAL(&s_mux);
  const bool own = sh.swipeEnabled && !sh.panelOpen && !sh.modalOpen;
  portEXIT_CRITICAL(&s_mux);
  return own;
}

bool uiNoSwipeAt(int x, int y) {
  bool hit = false;
  portENTER_CRITICAL(&s_mux);
  const int n = sh.panelOpen || sh.modalOpen ? 0 : sh.nZones;  // those cover the tiles
  for (int i = 0; i < n && !hit; i++) {
    const Shared::Zone& z = sh.zones[i];
    hit = x >= z.x1 && x <= z.x2 && y >= z.y1 && y <= z.y2;
  }
  portEXIT_CRITICAL(&s_mux);
  return hit;
}

bool uiTryBeginPanel(bool opening, Src& panel, Src& page, int& startOff, int& panelH) {
  bool ok = false;
  portENTER_CRITICAL(&s_mux);
  const int a = sh.active;
  if (sh.swipeEnabled && !sh.owned && !sh.modalOpen && sh.panelValid && !sh.panelBusy &&
      sh.panelOpen != opening) {
    if (opening) {
      page = {nullptr, 0, true};  // the live LVGL frame, no panel in it yet
      ok = true;
    } else if (sh.valid[a] && !sh.busy[a]) {
      page = sh.src[a];  // the page without the panel on top
      ok = true;
    }
    if (ok) {
      sh.owned = true;
      sh.gesture = opening ? Shared::GEST_PANEL_OPEN : Shared::GEST_PANEL_CLOSE;
      panel = sh.panelSrc;
      panelH = panel.h;
      startOff = opening ? 0 : panel.h;
    }
  }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

bool uiRequestLivePanel(bool opening) {
  bool ok = false;
  portENTER_CRITICAL(&s_mux);
  if (sh.swipeEnabled && !sh.owned && !sh.modalOpen && !sh.panelBusy &&
      sh.panelOpen != opening &&
      (!sh.panelValid || (!opening && !sh.valid[sh.active]))) {
    s_livePanelRequest = opening ? 1 : -1;
    ok = true;
  }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

bool uiPanelIsOpen() {
  portENTER_CRITICAL(&s_mux);
  const bool open = sh.panelOpen;
  portEXIT_CRITICAL(&s_mux);
  return open;
}

}  // namespace deck


