// Guition JC3248W535 deck UI. See deck_ui.h for the threading contract.
#include "deck_ui_int.h"
#include "deck_mqtt.h"
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
#include "deck_office_bambu.h"
#endif
#include "psram_json.h"

namespace deckui {

// Page images are refreshed one per pass, at most this often (each costs
// ~40 ms of loop time at 480x320).
constexpr uint32_t SNAP_SPACING_MS = 40;
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

// Theme colours (D11): Midnight (mockup A, default), Daylight, or Glass
// (Midnight text, tiles of tinted glass over a wallpaper), set by
// setPalette() before the pages are built.
uint8_t s_theme = 0;
uint32_t C_BG, C_TILE, C_TILE_PRESSED, C_TILE_LINE, C_CHIP, C_TEXT, C_TEXT_ON, C_TITLE,
    C_CLOCK, C_OK, C_DIM, C_PANEL, C_TRACK, C_BUTTON;

void setPalette(uint8_t theme) {
  clearGlassCache();
  s_theme = theme <= THEME_GLASS ? theme : THEME_MIDNIGHT;
  const bool day = s_theme == THEME_DAYLIGHT, glass = s_theme == THEME_GLASS;
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
// A web upload/delete can happen while a gesture is active. Rebuild lazily.
static bool s_bedroomIconsRefreshPending = false;
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
int s_zonesPage = -1;     // page the zones were taken from
bool s_zonesDirty = true;  // tiles rebuilt or moved: take them again

// --- LVGL glue ---------------------------------------------------------------------
uint32_t tickCb() { return millis(); }

// Posted LVGL frames whose framebuffer the flush task has not released yet.
int s_lvOut = 0;

void flushCb(lv_display_t* d, const lv_area_t*, uint8_t* px) {
  if (!lv_display_flush_is_last(d)) {
    lv_display_flush_ready(d);
    return;
  }
  const uint8_t* fb1 = (const uint8_t*)panelLvBuffer(1);
  const int idx = (px >= fb1 && px < fb1 + FRAME_BYTES) ? 1 : 0;
  panelPostLv(idx, s_gen);
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

void indevRead(lv_indev_t*, lv_indev_data_t* data) {
  const TouchPoint p = touchForLvgl();
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
// Track a freshly staged image independently of top-bar badge preferences.
// Declining the popup must not make it appear once per second.
static bool s_firmwarePrompted = false;
static void closeFirmwareConfirm(lv_event_t*) {
  if (s_firmwareConfirm) {
    lv_obj_delete_async(s_firmwareConfirm);
    s_firmwareConfirm = nullptr;
  }
}
static void approveFirmwareFromTouch(lv_event_t*) {
  const bool approved = firmwareUpdate::installStagedFromTouch();
  closeFirmwareConfirm(nullptr);
  if (!approved) Serial.println("Update: staged image no longer ready");
}
static void downloadFirmwareFromTouch(lv_event_t*) {
  // Queue a verified OTA download; the firmware is never installed without
  // a second physical confirmation after staging and validation.
  firmwareUpdate::requestDownload();
  closeFirmwareConfirm(nullptr);
}
static void openFirmwareConfirmation(lv_event_t*) {
  if (s_firmwareConfirm) return;
  const auto st = firmwareUpdate::snapshot();
  if (!st.available && !st.staged && !st.downloading) return;
  lv_obj_t* layer = lv_layer_top();
  s_firmwareConfirm = lv_obj_create(layer);
  lv_obj_set_size(s_firmwareConfirm, min<int>(logicalW() - 20, 340), LV_SIZE_CONTENT);
  lv_obj_center(s_firmwareConfirm);
  lv_obj_set_style_bg_color(s_firmwareConfirm, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_border_color(s_firmwareConfirm, lv_color_hex(C_OK), 0);
  lv_obj_set_style_border_width(s_firmwareConfirm, 2, 0);
  lv_obj_set_style_pad_all(s_firmwareConfirm, 15, 0);
  lv_obj_set_layout(s_firmwareConfirm, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_firmwareConfirm, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_firmwareConfirm, 12, 0);
  lv_obj_t* title = lv_label_create(s_firmwareConfirm);
  lv_label_set_text(title, st.staged ? "Install verified update?" :
                    st.downloading ? "Firmware downloading" :
                    "Firmware update available");
  lv_obj_set_style_text_color(title, lv_color_hex(C_TEXT), 0);
  lv_obj_t* version = lv_label_create(s_firmwareConfirm);
  if (st.staged)
    lv_label_set_text_fmt(version, "%s ready. The screen will restart.", st.version);
  else if (st.downloading)
    lv_label_set_text_fmt(version, "Downloading %s. Return here when ready.", st.version);
  else
    lv_label_set_text_fmt(version, "%s available. Download and verify it first.", st.version);
  lv_label_set_long_mode(version, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(version, LV_PCT(100));
  lv_obj_set_style_text_color(version, lv_color_hex(C_DIM), 0);
  lv_obj_t* cancel = lv_button_create(s_firmwareConfirm);
  lv_obj_set_width(cancel, LV_PCT(100));
  lv_obj_t* cancelText = lv_label_create(cancel);
  lv_label_set_text(cancelText, "Not now");
  lv_obj_center(cancelText);
  lv_obj_add_event_cb(cancel, closeFirmwareConfirm, LV_EVENT_CLICKED, nullptr);
  if (!st.downloading) {
    lv_obj_t* action = lv_button_create(s_firmwareConfirm);
    lv_obj_set_width(action, LV_PCT(100));
    lv_obj_t* actionText = lv_label_create(action);
    lv_label_set_text(actionText, st.staged ? "Install and restart" : "Download update");
    lv_obj_center(actionText);
    lv_obj_add_event_cb(action,
                        st.staged ? approveFirmwareFromTouch : downloadFirmwareFromTouch,
                        LV_EVENT_CLICKED, nullptr);
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
  if (p.updateBadge) {
    const auto st = firmwareUpdate::snapshot();
    if (s_header && st.checksEnabled && (st.showBadge || (st.staged && !st.promptOnStage)) && (st.available || st.staged))
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
  bool used[12][12] = {};
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
    out[i].off = !def.tiles[i].shown;
    if (!out[i].off && ps.set && fits(ps.x, ps.y, ps.w, ps.h)) take(out[i], ps.x, ps.y);
  }
  for (Cell& c : out) {
    if (c.off) continue;
    for (int y = 0; y < rows && !c.placed; y++)
      for (int x = 0; x < cols && !c.placed; x++)
        if (fits(x, y, c.w, c.h)) take(c, x, y);
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
  static int32_t colDsc[MAX_PAGES][13];
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
    if (cells[i].placed && def.tiles[i].type == TILE_CLOCK) {
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
  for (size_t i = 0; i < cells.size(); i++) {
    const Cell& c = cells[i];
    if (!c.placed && !c.off) Serial.printf("Deck: page '%s' has no room for tile '%s'\n",
                       def.id.c_str(), def.tiles[i].id.c_str());
    makeTile(grid, def.tiles[i], page, c.placed ? c.x : 0, c.placed ? c.y : 0, c.w, c.h);
    if (!c.placed) {
      s_tiles.back().off = true;
      lv_obj_add_flag(s_tiles.back().obj, LV_OBJ_FLAG_HIDDEN);
    }
  }

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
    if (cells[i].placed && def.tiles[i].type == TILE_CLOCK) clockVisible = true;
  lv_obj_update_layout(p.root);
  for (TileRt& r : s_tiles) {
    if (r.page != page) continue;
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
  if (s_theme == THEME_GLASS && wallBlur()) bakeGlass(p, &changed);
  p.stale = true;
  s_zonesDirty = true;
}

void buildPages() {
  fxQuiet();  // tiles get their first state: no animations
  const int w = logicalW();
  if (s_theme == THEME_GLASS) wallLoad(w, logicalH(), dispSettings.glassBlur, dispSettings.glassDim);
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
  if (s_theme == THEME_GLASS) {
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
  if (s_theme == THEME_GLASS) {
    for (int i = 0; i < s_count; i++) {
      if (s_pages[i].glass) continue;
      const uint32_t start = millis();
      if (bakeGlass(s_pages[i])) baked++;
      bakeMs += millis() - start;
    }
  }
  Serial.printf("Deck: glass %u baked, %u reused, %lu ms\n", baked, reused, (unsigned long)bakeMs);
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
  inlineClimateFlush();  // Don't lose an unsent setpoint during page rebuild.
  clearGlassCache();
  edClose();
  closeModal();
  destroyQuickPanel();
  for (int i = 0; i < s_count; i++) {
    Page& p = s_pages[i];
    if (keepGlass && s_theme == THEME_GLASS && p.glass && p.def && !p.glassKey.empty()) {
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
      if (r.page != a || !r.fill || n == MAX_PAGE_TILES) continue;
      lv_area_t t;
      lv_obj_get_coords(r.obj, &t);
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
  sh.panelValid = false;
  portEXIT_CRITICAL(&s_mux);
  vTaskDelay(2);
  panelQuiesce();
  releaseSwipe();
}

void refreshOneSnapshot() {
  if (touchIsDown() || compMode() != COMP_NONE) return;
  const uint32_t now = millis();
  if (now - s_lastSnapMs < SNAP_SPACING_MS) return;
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
  // The shown page swipes from the live frame; only closing the panel slides
  // off its image (showPanel marks it), so then it goes first.
  auto due = [&](int p) { return p >= 0 && s_pages[p].stale && !s_pages[p].wallBake && (p != a || s_qp.open); };
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
  if (!pg.snap) return false;

  portENTER_CRITICAL(&s_mux);
  const bool owned = sh.owned;
  if (!owned) sh.busy[pick] = true;
  portEXIT_CRITICAL(&s_mux);
  if (owned) return false;

  const uint32_t t0 = micros();
  lv_snapshot_reshape_draw_buf(pg.root, pg.snap);
  const bool ok = lv_snapshot_take_to_draw_buf(pg.root, LV_COLOR_FORMAT_RGB565, pg.snap) == LV_RESULT_OK;
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
  Serial.printf("Deck: wallpaper page %d bake %lu ms\n", index, (unsigned long)(millis() - start));
}

void tickWallpaper() {
  const bool safe = s_count && s_theme == THEME_GLASS && getScreenState() == SCREEN_DECK &&
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
    for (int i = 0; i < s_count; i++) s_pages[i].wallBake = true;
    bakeWallpaperPage(sh.active);
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
  if (firmwareUpdate::takeTouchConfirmationRequest())
    openFirmwareConfirmation(nullptr);
  // Staging happens asynchronously in the updater task. Show the physical
  // install confirmation once as soon as the image is verified, even if the
  // optional header update badge (or the whole header) is disabled.
  if (!updateState.staged) {
    s_firmwarePrompted = false;
  } else if (!s_firmwarePrompted && updateState.promptOnStage) {
    // Only a manual Download OTA request interrupts the touchscreen.
    // Automatic downloads advertise readiness through the header icon.
    s_firmwarePrompted = true;
    openFirmwareConfirmation(nullptr);
  }
  for (int i = 0; i < s_count; ++i) {
    Page& page = s_pages[i];
    if (!page.updateBadge) continue;
    const bool visible = s_header && updateState.checksEnabled &&
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
}

// Pages are rebuilt only once nothing is in the user's hands.
bool canRebuild() { return !touchIsDown() && !s_ed.root && !s_modalCard && !s_qp.open; }

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
    Serial.printf("Deck: page '%s' %s\n", pd.id.c_str(), pd.shown ? "hides" : "shows");
    flip = true;
  }
  if (!flip) {
    for (PageDef& pd : s_cfg.pages) {
      bool moved = false;
      for (TileDef& t : pd.tiles) {
        if (t.when.kind == WHEN_ALWAYS || whenWanted(t) == t.shown) continue;
        Serial.printf("Deck: tile '%s' %s\n", t.id.c_str(), t.shown ? "hides" : "shows");
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
  if (!p.frames && !s_waitTimeouts) return;
  Serial.printf("Deck: frames=%u comp=%u flush=%u/%uus build=%uus te_late=%u te_to=%u "
                "lv_drop=%u gap_max=%uus wait_to=%u snap_max=%uus int_free=%u\n",
                (unsigned)p.frames, (unsigned)p.compFrames, (unsigned)p.flushUsAvg,
                (unsigned)p.flushUsMax, (unsigned)p.buildUsAvg, (unsigned)p.teLate,
                (unsigned)p.teTimeouts, (unsigned)p.lvDropped, (unsigned)p.gapMaxUs,
                (unsigned)s_waitTimeouts, (unsigned)s_snapUsMax,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  s_waitTimeouts = 0;
  s_snapUsMax = 0;
}

}  // namespace deckui

using namespace deckui;

// --- Public API ----------------------------------------------------------------------
bool deckBegin(uint8_t rotation) {
  if (!panelBegin(rotation)) return false;

  String source, err;
  deckConfigLoad(s_cfg, &source, &err);
  Serial.printf("Deck: config from %s, %u pages\n", source.c_str(), (unsigned)s_cfg.pages.size());

  lv_init();
  lv_tick_set_cb(tickCb);
  s_disp = lv_display_create(logicalW(), logicalH());
  lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(s_disp, panelLvBuffer(0), panelLvBuffer(1), FRAME_BYTES,
                         LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(s_disp, flushCb);
  lv_display_set_flush_wait_cb(s_disp, flushWaitCb);

  s_indev = lv_indev_create();
  lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(s_indev, indevRead);
  lv_indev_set_long_press_time(s_indev, 450);

  static const lv_style_prop_t kPressProps[] = {
    LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT, LV_STYLE_BG_COLOR, LV_STYLE_PROP_INV};
  lv_style_transition_dsc_init(&s_pressTrans, kPressProps, lv_anim_path_ease_out, 90, 0, nullptr);

  setPalette(dispSettings.theme);
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  lv_obj_set_style_text_font(scr, &deck_font_14, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

  if (!iconsLoad(iconsPath(), err)) Serial.printf("Deck: icons: %s\n", err.c_str());
  if (wallOnSd() && !brandLoad(err)) Serial.printf("Deck: brand icons: %s\n", err.c_str());
  {
    Preferences prefs;
    if (prefs.begin("deckui", true)) {
      s_header = prefs.getBool("hdr", true);
      prefs.end();
    }
  }
  haBegin();
  buildPages();
  touchBegin();
  Serial.printf("Deck: LVGL %d.%d.%d up, %d pages, %dx%d\n", LVGL_VERSION_MAJOR,
                LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, s_count, logicalW(), logicalH());
  return true;
}

void deckTick() {
  if (!s_disp) return;

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
  // A swipe took the touch: make sure LVGL never turns it into a click.
  if (touchTakeSwipeStarted()) {
    lv_indev_reset(s_indev, nullptr);
    fxCancelRipples();
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
      if (r.page < 0 || r.page >= MAX_PAGES || cols[r.page] < 2) continue;
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
  presenceTick();
  frameTick();

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
  sh.swipeEnabled = !systemScreen && !frameActive();
  portEXIT_CRITICAL(&s_mux);
  sysScreenSync(sc);
  // Once per boot, when the deck is first visible.
  static bool sdWarned = false;
  if (!sdWarned && !systemScreen && currentBacklightLevel() && !compOwnsScreen() && wallSdError()[0]) {
    sdWarned = true;
    deckToastShow("microSD card not working, using internal storage", 6000);
  }

  if (compOwnsScreen()) return;  // LVGL stays frozen during a drag or slide

  mqttTick();
  if (compOwnsScreen()) return;

  if (frameActive()) {
    frameTick();
    if (!compOwnsScreen()) {
      dailyBarSync();
      lv_timer_handler();
    }
    return;
  }

  // A PNG may have appeared, changed in-place, or been deleted on the SD card.
  // Recreate tile image widgets when idle, after dropping cached decoded PNGs.
  // This happens on the loop task (the same task as the web server and LVGL).
  if (s_bedroomIconsRefreshPending && canRebuild()) {
    s_bedroomIconsRefreshPending = false;
    stopGestures();
    s_keepPage = activePageId();
    destroyPages(true);
    lv_image_cache_drop(nullptr);  // drop all stale file-backed PNG decodes
    buildPages();
    s_gen++;
    lv_obj_invalidate(lv_screen_active());
  }
  if (s_ed.pending != EditState::P_NONE) edFinish();
  tickWallpaper();
  dailyBarSync();
  updateHeaders();
  blinkClocks();
  qpUpdateStatus(false);
  haSync();
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  // Printer state is updated by a worker on the other core. Normal deck
  // frames must sync it too; frameCatchUp() only runs after a photo frame wake.
  bambuSync();
#endif
  inlineClimateTick();
  httpSync();
  whenSync();
  syncSwipeZones();
  lv_timer_handler();
  if (!systemScreen) refreshOneSnapshot();
  logStats();
}

void deckToast(const char* text) {
  if (s_disp) deckToastShow(text, 1800);
}

void deckBedroomIconsChanged() {
  // Never rebuild inside a multipart upload handler. deckTick() waits until
  // the user isn't touching the screen, preserving their current page.
  s_bedroomIconsRefreshPending = true;
}

void deckRefreshNow() {
  if (s_disp && !compOwnsScreen()) lv_timer_handler();
}

void deckApplyRotation(uint8_t rotation) {
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
  if (!s_disp || s_theme != THEME_GLASS) return;
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
  DeckConfig next;
  if (!deckConfigStore(text, next, err)) return false;
  // Tiles point into s_cfg: tear the pages down before replacing it.
  stopGestures();
  destroyPages(true);
  s_cfg = next;
  buildPages();
  s_gen++;
  lv_obj_invalidate(lv_screen_active());
  Serial.printf("Deck: new config applied, %u pages\n", (unsigned)s_cfg.pages.size());
  return true;
}

#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
void deckRefreshPrinterPage() {
  if (!s_disp) return;
  String saved, error;
  deckConfigText(saved);
  DeckConfig next;
  if (!deckConfigParse(saved.c_str(), saved.length(), next, &error)) {
    Serial.printf("Deck: printer page refresh rejected: %s\n", error.c_str());
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
  Serial.printf("Deck: printer configuration changed, %u pages\n",
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
    openModal((int)i);
    return true;
  }
  return false;
}

bool deckTestTap(const String& tileId, bool hold) {
  if (frameActive()) return false;
  if (s_ed.root) return false;
  for (TileRt& r : s_tiles) {
    if (r.def->id != tileId || r.def->type == TILE_CLOCK) continue;
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
    const float f = constrain(pct, 0, 100) / 100.0f;
    return setWish(r.def->entity, true, f);
  }
  return false;
}

bool deckTestConfirm(bool ok) {
  if (s_confirmHold < 0 || s_modalTile < 0 || (size_t)s_modalTile >= s_tiles.size()) return false;
  const TileDef& d = *s_tiles[s_modalTile].def;
  const TileAction& a = s_confirmHold ? d.hold : d.tap;
  if (ok) onConfirmOk(nullptr);
  else closeModal();
  return true;
}

bool deckTestEdit(const String& op, int tile, int x, int y, int w, int h) {
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

bool uiPanelIsOpen() {
  portENTER_CRITICAL(&s_mux);
  const bool open = sh.panelOpen;
  portEXIT_CRITICAL(&s_mux);
  return open;
}

}  // namespace deck

