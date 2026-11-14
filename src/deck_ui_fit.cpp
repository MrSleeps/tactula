// Deck UI: fitting tile content to the tile size.
#include "deck_ui_int.h"

namespace deckui {

// --- Pages -----------------------------------------------------------------------------
lv_obj_t* makeRoot(int w, int h) {
  lv_obj_t* r = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, w, h);
  lv_obj_set_style_bg_color(r, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  if (s_theme == THEME_GLASS && wallSharp()) lv_obj_set_style_bg_image_src(r, wallSharp(), 0);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  return r;
}

uint32_t tileText(const TileDef& d) { return d.hasTextColor ? d.textColor : C_TEXT; }

void setText(lv_obj_t* l, const char* text) {
  if (strcmp(lv_label_get_text(l), text)) lv_label_set_text(l, text);
}

// --- Tile fitting -----------------------------------------------------------------------
// A tile picks its padding, icon chip and type sizes from its pixel size (grid,
// span, top bar), so small tiles shrink text instead of overlapping it. Runs
// again on every size change.
constexpr int WIDE_W = 150;      // content width that earns the larger fonts

int lineH(const lv_font_t* f) { return lv_font_get_line_height(f); }
int textH(const lv_font_t* f, int lines) { return lines * lineH(f) + (lines - 1) * NAME_LINE_SP; }

void showObj(lv_obj_t* o, bool on) {
  if (!o) return;
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void setFont(lv_obj_t* o, const lv_font_t* f) {
  if (lv_obj_get_style_text_font(o, LV_PART_MAIN) != f) lv_obj_set_style_text_font(o, f, 0);
}

void fitName(lv_obj_t* name, const lv_font_t* f, int lines) {
  setFont(name, f);
  lv_obj_set_style_text_line_space(name, NAME_LINE_SP, 0);
  lv_obj_set_width(name, lv_pct(100));
  lv_obj_set_height(name, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(name, textH(f, lines), 0);
}

bool iconOff(const TileRt& r) { return r.def->icon == "none"; }

// A plain icon ("iconStyle": "plain") fills its box; one in a circle keeps a
// margin. Few sizes: each is a TinyTTF font holding a file open.
void fitChip(TileRt& r, int size) {
  lv_obj_set_size(r.chip, size, size);
  const int px = r.def->iconPlain ? (size >= 28 ? 28 : size >= 20 ? 20 : 16) : size >= 28 ? 20 : 16;
  if (px != r.iconPx) {
    r.iconPx = px;
    HaEntity e;
    const bool known = r.def->entity.length() && haGetEntity(r.def->entity, e);
    r.iconCp = showIcon(r.icon, *r.def, known ? &e : nullptr, r.on, px, 0);
  }
}

int textW(const char* text, const lv_font_t* f) {
  lv_point_t s;
  lv_text_get_size(&s, text, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return s.x;
}

// Lines of f that fit in h px, at most 3.
int linesIn(const lv_font_t* f, int h) {
  int n = 0;
  while (n < 3 && textH(f, n + 1) <= h) n++;
  return n;
}

// No word wider than the line (LVGL would break it mid-word) and the wrapped
// text within the lines.
bool nameFits(const char* text, const lv_font_t* f, int w, int lines) {
  char word[48];
  for (const char* p = text; *p;) {
    while (*p == ' ') p++;
    size_t n = 0;
    while (p[n] && p[n] != ' ') n++;
    if (n) {
      const size_t k = LV_MIN(n, sizeof(word) - 1);
      memcpy(word, p, k);
      word[k] = 0;
      if (textW(word, f) > w) return false;
    }
    p += n;
  }
  lv_point_t s;
  lv_text_get_size(&s, text, f, 0, NAME_LINE_SP, w, LV_TEXT_FLAG_NONE);
  return s.y <= textH(f, lines);
}

// The name font: the fitted one, or smaller until the name fits whole (12 px
// truncates). Up to three lines where the tile is tall enough.
void fitNameText(TileRt& r) {
  if (!r.nameFont) return;
  static const lv_font_t* const kSteps[] = {&deck_font_20, &deck_font_14, &deck_font_12};
  const char* text = lv_label_get_text(r.name);
  const lv_font_t* f = r.nameFont;
  int lines = 1;
  bool from = false;
  for (const lv_font_t* s : kSteps) {
    from = from || s == r.nameFont;
    if (!from) continue;
    f = s;
    lines = LV_MAX(1, linesIn(s, r.nameArea));
    if (nameFits(text, s, r.nameW, lines)) break;
  }
  fitName(r.name, f, lines);
}

// Action, toggle and slider: icon top left, state top right, name at the
// bottom; a one-line row on short wide tiles; the icon alone on tiny ones.
void fitButton(TileRt& r, int cw, int ch) {
  // Custom PNG logos use the body of the tile instead of the usual tiny
  // top-left icon chip. Keep the label centred below the artwork.
  if (r.pngIcon && !lv_obj_has_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN)) {
    // This is a dedicated image tile. The normal MDI chip is deliberately
    // hidden; it is positioned at the top-left and must not constrain the PNG.
    showObj(r.chip, false);
    showObj(r.name, !r.def->noName);
    showObj(r.sub, false);
    const lv_font_t* font = cw < 90 ? &deck_font_12 : &deck_font_14;
    fitName(r.name, font, 1);
    lv_obj_set_style_text_align(r.name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(r.name, cw);
    lv_obj_align(r.name, LV_ALIGN_BOTTOM_MID, 0, 0);
    fitPngIcon(r);
    return;
  }
  if (r.def->noName) {
    // With no caption, an MDI/brand icon should fill the inner area just
    // like a PNG. Keep all existing tile tap and toggle behaviour unchanged.
    showObj(r.name, false);
    showObj(r.sub, false);
    if (iconOff(r)) {
      showObj(r.chip, false);
      return;
    }
    showObj(r.chip, true);
    const int size = std::max(16, std::min(cw, ch));
    const int px = size >= 72 ? 40 : size >= 48 ? 28 : size >= 30 ? 20 : 16;
    lv_obj_set_size(r.chip, size, size);
    lv_obj_set_style_bg_opa(r.chip, r.def->iconPlain ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    if (r.iconPx != px) {
      r.iconPx = px;
      HaEntity e;
      const bool known = r.def->entity.length() && haGetEntity(r.def->entity, e);
      r.iconCp = showIcon(r.icon, *r.def, known ? &e : nullptr, r.on, px, 0);
    }
    lv_obj_center(r.chip);
    lv_obj_center(r.icon);
    return;
  }
  struct Opt {
    int chip;
    const lv_font_t* font;
    int lines;
    bool wideOnly;
  };
  static const Opt kOpts[] = {
      {30, &deck_font_20, 2, true}, {30, &deck_font_20, 1, true}, {30, &deck_font_14, 2, false},
      {22, &deck_font_12, 2, false}, {30, &deck_font_14, 1, false}, {22, &deck_font_12, 1, false},
  };
  // "icon": "none" keeps the layout and leaves the icon's corner empty.
  const bool icon = !iconOff(r);
  showObj(r.chip, icon);
  for (const Opt& o : kOpts) {
    if (cw < 40 || (o.wideOnly && cw < WIDE_W) || o.chip + textH(o.font, o.lines) > ch) continue;
    fitChip(r, o.chip);
    lv_obj_align(r.chip, LV_ALIGN_TOP_LEFT, 0, 0);
    r.nameFont = o.font;
    r.nameArea = ch - o.chip;
    r.nameW = cw;
    fitNameText(r);
    lv_obj_align(r.name, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    // The state beside the icon, smaller or gone when "Off" would not fit.
    const int room = cw - o.chip - 6;
    const lv_font_t* sf = o.chip >= 30 && textW("Off", &deck_font_14) <= room ? &deck_font_14 : &deck_font_12;
    setFont(r.sub, sf);
    lv_obj_set_size(r.sub, lv_pct(100), lineH(sf));
    lv_obj_set_style_pad_left(r.sub, icon ? o.chip + 6 : 0, 0);  // clear of the icon
    lv_obj_align(r.sub, LV_ALIGN_TOP_RIGHT, 0, (o.chip - lineH(sf)) / 2);
    showObj(r.name, true);
    showObj(r.sub, textW("Off", sf) <= room);
    return;
  }
  r.nameFont = nullptr;
  if (cw >= 80) {
    fitChip(r, 22);
    lv_obj_align(r.chip, LV_ALIGN_LEFT_MID, 0, 0);
    const bool state = cw >= 140;
    const int nameX = icon ? 28 : 0;
    fitName(r.name, &deck_font_12, 1);
    lv_obj_set_width(r.name, cw - nameX - (state ? 48 : 0));
    lv_obj_align(r.name, LV_ALIGN_LEFT_MID, nameX, 0);
    setFont(r.sub, &deck_font_12);
    lv_obj_set_size(r.sub, 44, lineH(&deck_font_12));
    lv_obj_set_style_pad_left(r.sub, 0, 0);
    lv_obj_align(r.sub, LV_ALIGN_RIGHT_MID, 0, 0);
    showObj(r.name, true);
    showObj(r.sub, state);
    return;
  }
  showObj(r.sub, false);
  if (!icon) {  // too small for the icon alone: the name takes its place
    fitName(r.name, &deck_font_12, LV_MAX(1, linesIn(&deck_font_12, ch)));
    lv_obj_set_width(r.name, cw);
    lv_obj_center(r.name);
    showObj(r.name, true);
    return;
  }
  const int s = LV_MIN(30, LV_MIN(cw, ch));
  fitChip(r, s);
  lv_obj_center(r.chip);
  showObj(r.name, false);
}

void fitDetail(TileRt& r, const lv_font_t* f, int w, lv_align_t align, int y, bool show) {
  if (!r.detail) return;
  showObj(r.detail, show);
  if (!show) return;
  setFont(r.detail, f);
  lv_obj_set_size(r.detail, w, lineH(f));
  lv_obj_align(r.detail, align, 0, y);
}

constexpr int ROW_BAR_MIN = 20;

// Sensor rows: labels and values take what they need, the bar the rest.
void layoutRows(TileRt& r) {
  const lv_font_t* f = r.valFont;
  if (!f || !r.rows) return;
  const TileDef& d = *r.def;
  const int n = LV_MIN((int)lv_obj_get_child_count(r.rows), (int)d.rows.size());
  bool templated = false;
  for (int i = 0; i < n; i++) templated = templated || d.rows[i].value.length();
  // Row value templates give way to the plain reading when the bar would not fit.
  const int gap = 6;
  int labelW = 0, valueW = 0, barX = 0, barW = 0;
  for (int pass = templated ? 0 : 1; pass < 2; pass++) {
    labelW = 0;
    valueW = textW(("100" + rowUnit(d)).c_str(), f);
    for (int i = 0; i < n; i++) {
      lv_obj_t* box = lv_obj_get_child(r.rows, i);
      setText(lv_obj_get_child(box, 2), rowText(d, i, pass == 0).c_str());
      labelW = LV_MAX(labelW, textW(lv_label_get_text(lv_obj_get_child(box, 0)), f));
      valueW = LV_MAX(valueW, textW(lv_label_get_text(lv_obj_get_child(box, 2)), f));
    }
    valueW = LV_MIN(valueW, r.valW - labelW);
    barX = labelW ? labelW + gap : 0;
    barW = r.valW - barX - valueW - gap;
    if (barW >= ROW_BAR_MIN) break;
  }
  for (int i = 0; i < n; i++) {
    lv_obj_t* box = lv_obj_get_child(r.rows, i);
    lv_obj_t* label = lv_obj_get_child(box, 0);
    lv_obj_t* bar = lv_obj_get_child(box, 1);
    lv_obj_t* value = lv_obj_get_child(box, 2);
    lv_obj_set_size(box, r.valW, r.rowH);
    lv_obj_set_pos(box, 0, i * r.rowH);
    setFont(label, f);
    setFont(value, f);
    lv_obj_set_size(label, labelW, lineH(f));
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(value, valueW, lineH(f));
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);
    showObj(bar, barW >= ROW_BAR_MIN);
    lv_obj_set_size(bar, LV_MAX(barW, 0), 6);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, barX, 0);
  }
}

// Sensor rows: the name and icon on top, the rows sharing the space under it.
void fitRows(TileRt& r, int cw, int ch) {
  struct Opt {
    const lv_font_t* name;
    const lv_font_t* row;
  };
  static const Opt kOpts[] = {{&deck_font_20, &deck_font_20}, {&deck_font_14, &deck_font_20},
                              {&deck_font_14, &deck_font_14}, {&deck_font_14, &deck_font_12},
                              {&deck_font_12, &deck_font_12}};
  const int n = lv_obj_get_child_count(r.rows);
  const Opt* o = &kOpts[4];
  for (const Opt& k : kOpts)
    if (lineH(k.name) + n * lineH(k.row) <= ch) { o = &k; break; }
  showObj(r.sub, false);
  showObj(r.detail, false);
  const int chip = iconOff(r) ? 0 : lineH(o->name) >= 22 ? 22 : 18;
  if (chip) {
    fitChip(r, chip);
    lv_obj_align(r.chip, LV_ALIGN_TOP_RIGHT, 2, -2);
  }
  showObj(r.chip, chip > 0);
  r.chipRoom = 0;
  r.nameFont = nullptr;
  fitName(r.name, o->name, 1);
  lv_obj_set_width(r.name, chip ? cw - chip - 4 : cw);
  lv_obj_align(r.name, LV_ALIGN_TOP_LEFT, 0, -2);
  showObj(r.name, true);
  const int top = lineH(o->name);
  r.rowH = n ? LV_MAX(lineH(o->row), (ch - top) / n) : 0;
  lv_obj_set_size(r.rows, cw, ch - top);
  lv_obj_align(r.rows, LV_ALIGN_TOP_LEFT, 0, top);
  r.valFont = o->row;
  r.valW = cw;
  layoutRows(r);
}

// Sensor: the reading big at the top with a small icon top right, the name
// under it; a row (name left, reading right) on short tiles.
void fitSensor(TileRt& r, int cw, int ch) {
  if (r.rows) {
    fitRows(r, cw, ch);
    return;
  }
  struct Opt {
    const lv_font_t* val;
    const lv_font_t* name;
    int lines;
    bool wideOnly;
  };
  static const Opt kOpts[] = {
      {&deck_font_28, &deck_font_20, 2, true},  {&deck_font_28, &deck_font_20, 1, true},
      {&deck_font_28, &deck_font_14, 1, true},  {&deck_font_20, &deck_font_14, 2, false},
      {&deck_font_20, &deck_font_12, 2, false}, {&deck_font_20, &deck_font_14, 1, false},
      {&deck_font_20, &deck_font_12, 1, false}, {&deck_font_14, &deck_font_12, 1, false},
  };
  for (int pass = r.detail ? 0 : 1; pass < 2; pass++)
  for (const Opt& o : kOpts) {
    const lv_font_t* df = o.val == &deck_font_28 ? &deck_font_14 : &deck_font_12;
    const int dh = pass ? 0 : lineH(df);
    if ((o.wideOnly && cw < WIDE_W) || lineH(o.val) + textH(o.name, o.lines) + dh > ch) continue;
    const int chip = iconOff(r) ? 0 : o.val == &deck_font_14 ? 18 : 22;
    if (chip) {
      fitChip(r, chip);
      lv_obj_align(r.chip, LV_ALIGN_TOP_RIGHT, 2, -2);
    }
    showObj(r.chip, chip > 0);
    lv_obj_set_style_pad_right(r.sub, 0, 0);
    setFont(r.sub, o.val);
    lv_obj_set_size(r.sub, lv_pct(100), lineH(o.val));
    lv_obj_set_style_text_align(r.sub, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(r.sub, LV_ALIGN_TOP_LEFT, 0, 0);
    r.valFont = o.val;
    r.valW = cw - chip;
    r.chipRoom = chip;
    fitValue(r);
    r.nameFont = o.name;
    r.nameArea = ch - lineH(o.val) - dh;
    r.nameW = cw;
    fitNameText(r);
    lv_obj_align(r.name, LV_ALIGN_BOTTOM_LEFT, 0, -dh);
    showObj(r.name, true);
    fitDetail(r, df, cw, LV_ALIGN_BOTTOM_LEFT, 0, !pass);
    return;
  }
  const lv_font_t* vf = ch >= lineH(&deck_font_20) ? &deck_font_20 : &deck_font_14;
  showObj(r.detail, false);
  showObj(r.chip, false);
  r.chipRoom = 0;
  r.nameFont = nullptr;
  lv_obj_set_style_pad_right(r.sub, 0, 0);
  lv_obj_set_style_text_align(r.sub, LV_TEXT_ALIGN_RIGHT, 0);
  const bool name = cw >= 80;
  lv_obj_set_size(r.sub, name ? cw / 2 : cw, lineH(vf));
  lv_obj_align(r.sub, name ? LV_ALIGN_RIGHT_MID : LV_ALIGN_CENTER, 0, 0);
  r.valFont = vf;
  r.valW = name ? cw / 2 : cw;
  fitValue(r);
  fitName(r.name, &deck_font_12, 1);
  lv_obj_set_width(r.name, cw - cw / 2 - 4);
  lv_obj_align(r.name, LV_ALIGN_LEFT_MID, 0, 0);
  showObj(r.name, name);
}

// Keep Clock tiles untouched. Weather/temperature need decimal dots and
// minus signs, which the compact Clock font glyph subsets don't contain.
// Lightweight font descriptors share the existing glyph data; no new bitmaps.
const lv_font_t* temperatureFont72() {
  static lv_font_t f = deck_font_clock_72;
  f.fallback = &lv_font_montserrat_40;
  return &f;
}
const lv_font_t* temperatureFont56() {
  static lv_font_t f = deck_font_clock_56;
  f.fallback = &lv_font_montserrat_40;
  return &f;
}

// The largest font from `from` down that fits text in room, null if none.
const lv_font_t* valueFont(const lv_font_t* from, const char* text, int room) {
  static const lv_font_t* const kSteps[] = {temperatureFont72(), temperatureFont56(),
                                            &lv_font_montserrat_40, &deck_font_28, &deck_font_20,
                                            &deck_font_14, &deck_font_12};
  bool on = false;
  for (const lv_font_t* s : kSteps) {
    on = on || s == from;
    if (on && textW(text, s) <= room) return s;
  }
  return nullptr;
}

void setWidth(lv_obj_t* o, int w) {
  if (lv_obj_get_style_width(o, LV_PART_MAIN) != w) lv_obj_set_width(o, w);
}

constexpr int ROW_VIZ_MIN = 40;

int rowNameW(const TileRt& r) {
  const int t = textW(lv_label_get_text(r.name), lv_obj_get_style_text_font(r.name, LV_PART_MAIN));
  return LV_MIN(t, r.valW * 2 / 5);
}

// Chart or bar width between the name and the widest number seen (at least two
// digits): it shrinks once when the value grows and never widens back.
int rowVizW(const TileRt& r, const lv_font_t* f, int unitW) {
  const int most = textW(r.rowMax[0] ? r.rowMax : "00", f);
  const int valueW = LV_MAX(textW(lv_label_get_text(r.sub), f), most) + unitW;
  return r.valW - rowNameW(r) - valueW - 16;
}

// Remembers the widest number with its digits as zeros. State words
// ("Unavailable" before HA answers) do not count.
void noteRowValue(TileRt& r, const char* text, const lv_font_t* f) {
  if (!isdigit((unsigned char)text[*text == '-'])) return;
  char z[sizeof(r.rowMax)];
  size_t n = 0;
  for (; text[n] && n < sizeof(z) - 1; n++) z[n] = isdigit((unsigned char)text[n]) ? '0' : text[n];
  z[n] = 0;
  if (!r.rowMax[0] || textW(z, f) > textW(r.rowMax, f)) memcpy(r.rowMax, z, n + 1);
}

// Row: the name gets what the value leaves, or on a wide row shares it with
// the chart or bar.
void layoutRow(TileRt& r, const lv_font_t* f, int unitW) {
  const int w = r.row == 2 ? rowVizW(r, f, unitW) : 0;
  const bool viz = w >= ROW_VIZ_MIN;
  if (r.row == 2) showObj(r.viz, viz);
  if (!viz) {
    setWidth(r.name, LV_MAX(0, r.valW - textW(lv_label_get_text(r.sub), f) - unitW - 8));
    return;
  }
  const int nameW = rowNameW(r);
  const int x = nameW ? nameW + 10 : 0;
  setWidth(r.name, nameW);
  setWidth(r.viz, w);
  if (lv_obj_get_style_x(r.viz, LV_PART_MAIN) != x) lv_obj_align(r.viz, LV_ALIGN_LEFT_MID, x, 0);
}

// Sensor reading or metric value: the largest font, from the fitted one down,
// that fits the room left next to the unit. The unit sits on the value's
// baseline, never larger than the value, and goes when nothing fits beside it.
void fitValue(TileRt& r) {
  if (!r.valFont) return;
  const char* text = lv_label_get_text(r.sub);
  int room = r.valW;
  if (r.besideName)
    room -= LV_MIN(textW(lv_label_get_text(r.name), lv_obj_get_style_text_font(r.name, LV_PART_MAIN)),
                   r.valW * (r.row ? 35 : 55) / 100) + 8;
  // A sensor's icon gives way when the reading does not fit beside it.
  if (r.chipRoom) {
    const bool chip = textW(text, r.valFont) <= room;
    showObj(r.chip, chip);
    lv_obj_set_style_pad_right(r.sub, chip ? r.chipRoom : 0, 0);
    if (!chip) room += r.chipRoom;
  }
  const lv_font_t* uf = r.unitFont;
  const int unitW = r.unit && uf ? 4 + textW(lv_label_get_text(r.unit), uf) : 0;
  const lv_font_t* f = valueFont(r.valFont, text, room - unitW);
  bool unit = unitW > 0;
  if (!f && unit) {
    unit = false;
    f = valueFont(r.valFont, text, room);
  }
  if (!f) f = &deck_font_12;
  if (r.row == 2) noteRowValue(r, text, f);
  // A wide row gives up the largest value font for its chart or bar.
  if (r.row == 2 && f == &deck_font_28 && rowVizW(r, f, unit ? unitW : 0) < ROW_VIZ_MIN) f = &deck_font_20;
  setFont(r.sub, f);
  if (r.unit && uf) {
    showObj(r.unit, unit);
    if (lineH(uf) >= lineH(f)) uf = &deck_font_12;
    setFont(r.unit, uf);
    const int pad = LV_MAX(0, f->base_line - uf->base_line);
    if (lv_obj_get_style_pad_bottom(r.unit, LV_PART_MAIN) != pad) lv_obj_set_style_pad_bottom(r.unit, pad, 0);
  }
  if (r.row) layoutRow(r, f, unit ? unitW : 0);
}

// PC metric: a chart or bar under a smaller number on wide tiles with the
// room, else the name on top and the number big at the bottom, else a row
// (a wide one keeps the chart or bar, between the name and the number).
void fitMetric(TileRt& r, int cw, int ch) {
  const TileDef& d = *r.def;
  lv_obj_t* row = lv_obj_get_parent(r.sub);
  // Bar: name, number and the 8 px bar stacked; chart: the line under the number.
  const lv_font_t* nf = ch >= 64 ? &deck_font_20 : &deck_font_14;
  const bool wideViz = r.viz && cw >= WIDE_W;
  const bool viz = wideViz &&
                   (d.style == METRIC_BAR ? ch >= lineH(nf) + lineH(&deck_font_28) * 3 / 4 + 10 : ch >= 44);
  showObj(r.viz, viz);
  r.besideName = viz && d.style != METRIC_BAR;
  r.row = 0;
  if (viz) {
    setFont(r.name, nf);
    lv_obj_set_size(r.name, d.style == METRIC_BAR ? lv_pct(100) : lv_pct(55), lineH(nf));
    lv_obj_align(r.name, LV_ALIGN_TOP_LEFT, 0, -2);
    r.unitFont = &deck_font_14;
    r.valFont = &deck_font_28;
    lv_obj_set_width(r.viz, lv_pct(100));
    lv_obj_align(r.viz, LV_ALIGN_BOTTOM_MID, 0, 0);
    if (d.style == METRIC_BAR) {
      lv_obj_set_height(r.viz, 8);
      r.valW = cw;
      lv_obj_align(row, LV_ALIGN_BOTTOM_LEFT, 0, -10);
    } else {
      r.valW = cw;
      lv_obj_align(row, LV_ALIGN_TOP_RIGHT, 0, -6);
      const int top = lineH(nf) - 2 + lineH(&deck_font_12);
      const bool detail = r.detail && ch - top >= 24;
      lv_obj_set_height(r.viz, LV_MIN(ch * 62 / 100, detail ? ch - top : ch - 22));
      fitDetail(r, &deck_font_12, cw * 55 / 100, LV_ALIGN_TOP_LEFT, lineH(nf) - 2, detail);
    }
    showObj(r.name, true);
    fitValue(r);
    return;
  }
  // Digits have no accents or descenders: a quarter of the value's line box is air.
  struct Opt {
    const lv_font_t* name;
    const lv_font_t* val;
    const lv_font_t* unit;
    bool wideOnly;
  };
  static const Opt kOpts[] = {
      {&deck_font_20, &lv_font_montserrat_40, &deck_font_20, true},
      {&deck_font_14, &deck_font_28, &deck_font_14, false},
      {&deck_font_12, &deck_font_20, &deck_font_12, false},
  };
  for (int pass = r.detail ? 0 : 1; pass < 2 && !wideViz; pass++)
  for (const Opt& o : kOpts) {
    const int dh = pass ? 0 : lineH(&deck_font_12);
    if ((o.wideOnly && cw < WIDE_W) || lineH(o.name) + dh + lineH(o.val) * 3 / 4 > ch) continue;
    setFont(r.name, o.name);
    lv_obj_set_size(r.name, lv_pct(100), lineH(o.name));
    lv_obj_align(r.name, LV_ALIGN_TOP_LEFT, 0, -2);
    r.unitFont = o.unit;
    r.valFont = o.val;
    r.valW = cw;
    lv_obj_align(row, LV_ALIGN_BOTTOM_LEFT, 0, 2);
    showObj(r.name, true);
    fitDetail(r, &deck_font_12, cw, LV_ALIGN_TOP_LEFT, lineH(o.name) - 2, !pass);
    fitValue(r);
    return;
  }
  showObj(r.detail, false);
  const bool name = cw >= 80;
  const lv_font_t* rf = cw >= WIDE_W && ch >= lineH(&deck_font_14) ? &deck_font_14 : &deck_font_12;
  setFont(r.name, rf);
  lv_obj_set_height(r.name, lineH(rf));
  lv_obj_align(r.name, LV_ALIGN_LEFT_MID, 0, 0);
  showObj(r.name, name);
  r.unitFont = name ? &deck_font_14 : &deck_font_12;
  r.valFont = lineH(&deck_font_28) * 3 / 4 <= ch ? &deck_font_28
              : lineH(&deck_font_20) * 3 / 4 <= ch ? &deck_font_20 : &deck_font_14;
  r.valW = cw;
  r.besideName = name;
  r.row = !name ? 0 : wideViz ? 2 : 1;
  if (r.row == 2) {
    lv_obj_set_height(r.viz, d.style == METRIC_BAR ? 6 : ch);
    lv_obj_align(r.viz, LV_ALIGN_LEFT_MID, 0, 0);  // layoutRow places it
  }
  lv_obj_align(row, name ? LV_ALIGN_RIGHT_MID : LV_ALIGN_CENTER, 0, 0);
  fitValue(r);
}

// A neighbouring Clock tile centres a time row *above* its date. If a
// headline temperature sits in the same horizontal row, give it an equivalent
// lower text line so its main numeral baseline lines up with the clock's.
bool headlineBesideClock(const TileRt& r) {
  if (!r.obj) return false;
  lv_area_t self;
  lv_obj_get_coords(r.obj, &self);
  const int selfH = self.y2 - self.y1 + 1;
  if (selfH <= 0) return false;
  for (const TileRt& other : s_tiles) {
    if (other.page != r.page || !other.obj || other.def->type != TILE_CLOCK) continue;
    lv_area_t peer;
    lv_obj_get_coords(other.obj, &peer);
    // Side-by-side, not stacked pages or tiles in a different grid row.
    if (peer.x2 >= self.x1 && self.x2 >= peer.x1) continue;
    // LVGL coordinates are long on this toolchain; cast the small screen
    // overlap to int before using std::max so both arguments have one type.
    const int overlap = std::max(0, static_cast<int>(
        std::min(self.y2, peer.y2) - std::max(self.y1, peer.y1) + 1));
    const int peerH = peer.y2 - peer.y1 + 1;
    if (peerH > 0 && overlap * 3 >= std::min(selfH, peerH) * 2)
      return true;
  }
  return false;
}

// Sensor headline: match the Clock's large digit stack where adjacent.
// Otherwise retain the ordinary centred label-over-number presentation.
void fitHeadline(TileRt& r, int cw, int ch) {
  lv_obj_set_flex_flow(r.obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(r.obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  showObj(r.detail, false);  // an optional sensor detail cannot shift the clock alignment
  const bool paired = ch >= 68 && headlineBesideClock(r);
  const lv_font_t* labelFont = paired
      ? (ch >= 120 ? &deck_font_20 : &deck_font_14)
      : (cw >= 145 ? &deck_font_20 : &deck_font_14);
  const bool label = paired || (ch >= 48 && !r.def->noName);

  // The Clock has its date AFTER the time. Put the sensor label after the
  // temperature; when explicitly hidden, use an invisible spacer of the same
  // height instead of leaving the temperature vertically centred by itself.
  const int last = (int)lv_obj_get_child_count(r.obj) - 1;
  lv_obj_move_to_index(r.name, paired ? last : 0);
  if (r.def->noName) {
    lv_label_set_text(r.name, paired ? " " : "");
    lv_obj_set_style_text_opa(r.name, paired ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  } else {
    lv_obj_set_style_text_opa(r.name, paired ? LV_OPA_70 : LV_OPA_COVER, 0);
  }
  setFont(r.name, labelFont);
  lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(r.name, cw);
  lv_obj_set_style_text_align(r.name, LV_TEXT_ALIGN_CENTER, 0);
  showObj(r.name, label);

  const int available = ch - (label ? lineH(labelFont) : 0);
  const lv_font_t* maxFont = &deck_font_14;
  static const lv_font_t* const kReadingFonts[] = {
      temperatureFont72(), temperatureFont56(), &lv_font_montserrat_40,
      &deck_font_28, &deck_font_20, &deck_font_14
  };
  for (const lv_font_t* font : kReadingFonts) {
    if (lineH(font) * 3 / 4 <= available) { maxFont = font; break; }
  }
  r.valFont = maxFont;
  r.valW = cw;
  r.unitFont = &deck_font_14;
  r.chipRoom = 0;
  r.besideName = false;
  r.row = 0;
  fitValue(r);
  const lv_font_t* actual = lv_obj_get_style_text_font(r.sub, LV_PART_MAIN);
  // Match fitClock's slight reduction in the gap to its date.
  lv_obj_set_style_margin_top(r.name, paired ? -lineH(actual) / 8 : 0, 0);
}

// Weather needs two responsive presentations. In a normal 2x1 tile there
// isn't enough vertical space for THREE separate text bands, so the weather
// condition takes the header and the temperature gets the rest. Taller
// tiles can show a separate title and bottom condition.
void fitWeather(TileRt& r, int cw, int ch) {
  if (cw <= 0 || ch <= 0) return;
  lv_obj_t* valueRow = lv_obj_get_parent(r.sub);
  const bool compact = ch < 108;
  const bool tiny = ch < 42;
  const lv_font_t* nameFont = &deck_font_14;
  const lv_font_t* conditionFont = compact ? &deck_font_12 : &deck_font_14;
  const int iconSize = tiny ? 0 : compact ? (ch < 68 ? 16 : 20) : 30;

  // Compact: one top row = condition + weather icon, with no duplicate
  // "Weather" title. Tall: title/icon above and condition below the value.
  const bool showName = !compact && !r.def->noName;
  const bool showCondition = !tiny;
  const int headerH = compact
      ? (showCondition ? lineH(conditionFont) : 0)
      : std::max(showName ? lineH(nameFont) : 0, iconSize);
  const int bottomH = !compact && showCondition ? lineH(conditionFont) : 0;
  const int gap = ch >= 72 ? 2 : 0;
  const int readingTop = headerH + gap;
  const int readingH = std::max(0, ch - readingTop - bottomH - (bottomH ? gap : 0));

  setFont(r.name, nameFont);
  lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_size(r.name, std::max(1, cw - iconSize - 6), lineH(nameFont));
  lv_obj_align(r.name, LV_ALIGN_TOP_LEFT, 0, 0);
  showObj(r.name, showName);

  setFont(r.detail, conditionFont);
  lv_label_set_long_mode(r.detail, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_align(r.detail, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_size(r.detail,
                  compact ? std::max(1, cw - iconSize - 6) : cw,
                  lineH(conditionFont));
  lv_obj_align(r.detail, compact ? LV_ALIGN_TOP_LEFT : LV_ALIGN_BOTTOM_LEFT, 0, 0);
  showObj(r.detail, showCondition);

  showObj(r.chip, !tiny);
  if (!tiny) {
    lv_obj_set_size(r.chip, iconSize, iconSize);
    lv_obj_align(r.chip, LV_ALIGN_TOP_RIGHT, 0, 0);
    const int iconPx = iconSize >= 30 ? 28 : 16;
    if (r.iconPx != iconPx) {
      r.iconPx = iconPx;
      HaEntity e;
      if (haGetEntity(r.def->entity, e)) {
        TileDef visual = *r.def;
        if (!visual.icon.length()) visual.icon = String("mdi:") + weatherIcon(e.state);
        r.iconCp = showIcon(r.icon, visual, &e, false, iconPx, 0);
      } else r.iconCp = showIcon(r.icon, *r.def, nullptr, false, iconPx, 0);
    }
  }

  const lv_font_t* unitFont = cw >= 140 ? &deck_font_14 : &deck_font_12;
  const char* value = lv_label_get_text(r.sub);
  const char* unit = lv_label_get_text(r.unit);
  const int unitW = unit[0] ? 4 + textW(unit, unitFont) : 0;
  static const lv_font_t* const kFonts[] = {
      temperatureFont72(), temperatureFont56(), &lv_font_montserrat_40,
      &deck_font_28, &deck_font_20, &deck_font_14, &deck_font_12
  };
  const lv_font_t* chosen = &deck_font_12;
  bool showUnit = unitW > 0;
  // The actual glyph widths and the full font line heights both matter.
  // In compact mode cap at Clock 56, which is prominent but not as tall as
  // the original 72px reading. Never arbitrarily cap compact tiles at 20px.
  for (int i = compact ? 1 : 0; i < (int)(sizeof(kFonts) / sizeof(kFonts[0])); ++i) {
    const lv_font_t* font = kFonts[i];
    if (lineH(font) <= readingH && textW(value, font) + unitW <= cw) {
      chosen = font;
      break;
    }
  }
  if (textW(value, chosen) + unitW > cw) showUnit = false;
  if (showUnit) {
    // The same row is used for both labels: keep the unit baseline below
    // the number without consuming the separate header/condition space.
    setFont(r.unit, unitFont);
    lv_obj_set_style_pad_bottom(r.unit, LV_MAX(0, chosen->base_line - unitFont->base_line), 0);
  }
  setFont(r.sub, chosen);
  showObj(r.unit, showUnit);
  r.valFont = chosen;
  r.unitFont = unitFont;
  r.valW = cw;
  const int rowH = lineH(chosen);
  lv_obj_set_size(valueRow, cw, rowH);
  lv_obj_set_flex_align(valueRow, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(valueRow, LV_ALIGN_TOP_MID, 0,
               readingTop + std::max(0, (readingH - rowH) / 2));
}

// Digit ink in a line box: rows of air above it and its height. The clock
// fonts are digits only and have no air; text fonts do.
static void digitInk(const lv_font_t* f, int& top, int& h) {
  lv_font_glyph_dsc_t g = {};
  if (!lv_font_get_glyph_dsc(f, &g, '0', 0)) {
    top = lineH(f) / 8;
    h = lineH(f) * 3 / 4;
    return;
  }
  top = lineH(f) - f->base_line - (g.ofs_y + g.box_h);
  h = g.box_h;
}

// Date ink: from the digit tops down to the descenders (Sep, Aug, May).
static void dateInk(const lv_font_t* f, int& top, int& h) {
  digitInk(f, top, h);
  lv_font_glyph_dsc_t g = {};
  const int bot = lv_font_get_glyph_dsc(f, &g, 'p', 0) ? lineH(f) - f->base_line - g.ofs_y : lineH(f);
  h = LV_MAX(h, bot - top);
}

// Clock: the largest time that fits, the date under it when there is room
// (a one step smaller time is worth it for the date). "date": "always" takes
// a tighter gap first, then any smaller time. Fit and centring go by the ink,
// not by the line boxes.
void fitClock(TileRt& r, int cw, int ch) {
  static const lv_font_t* const kTime[] = {&deck_font_clock_72, &deck_font_clock_56, &lv_font_montserrat_40,
                                           &deck_font_28, &deck_font_20, &deck_font_14};
  constexpr int N = sizeof(kTime) / sizeof(kTime[0]);
  const lv_font_t* df = ch >= 120 ? &deck_font_20 : &deck_font_14;
  const bool ampm = !netSettings.use24h;
  auto unitFont = [](const lv_font_t* f) { return lineH(f) >= 48 ? &deck_font_20 : &deck_font_14; };
  int dTop, dH;
  dateInk(df, dTop, dH);
  const int gap = lv_obj_get_style_pad_row(r.obj, LV_PART_MAIN);  // theme gap between time and date
  constexpr int MIN_GAP = 5;  // ink rows between digits and date when "always" tightens it
  int cut = 0;                // rows taken off the gap
  // Ink rows from the top of the column; the date line sits L/8 up into the time's.
  auto dateY = [&](const lv_font_t* f) { return lineH(f) - lineH(f) / 8 + gap - cut; };
  auto inkSpan = [&](const lv_font_t* f, bool withDate, int& top, int& bot) {
    int h;
    digitInk(f, top, h);
    bot = withDate ? dateY(f) + dTop + dH : top + h;
  };
  int best = N - 1, dated = -1;
  for (int i = N - 1; i >= 0; i--) {
    const lv_font_t* f = kTime[i];
    const int w = textW("00:00", f) + (ampm ? 4 + textW("PM", unitFont(f)) : 0);
    int top, bot;
    inkSpan(f, false, top, bot);
    if (w > cw || bot - top > ch) continue;
    best = i;
    inkSpan(f, true, top, bot);
    if (bot - top <= ch) dated = i;
  }
  // Fonts from best down already fit the width and, without the date, the height.
  if (r.def->clockDate) {
    const int last = dated < 0 ? N : dated;
    for (int i = best; i < last; i++) {
      const lv_font_t* f = kTime[i];
      int top, bot, h;
      inkSpan(f, true, top, bot);
      digitInk(f, top, h);
      const int over = bot - top - ch, room = dateY(f) + dTop - (top + h) - MIN_GAP;
      if (over <= room) {
        cut = LV_MAX(0, over);
        dated = i;
        break;
      }
    }
  }
  const bool date = !r.def->clockDateNever && dated >= 0 && (dated <= best + 1 || r.def->clockDate);
  const lv_font_t* f = kTime[date ? dated : best];
  // Flex centres the boxes; move them so the ink is centred instead.
  int top, bot;
  inkSpan(f, date, top, bot);
  const int boxH = date ? dateY(f) + lineH(df) : lineH(f);
  const int shift = boxH / 2 - (top + bot) / 2;
  lv_obj_set_style_translate_y(lv_obj_get_parent(r.unit), shift, 0);
  lv_obj_set_style_translate_y(r.name, shift, 0);
  setFont(r.sub, f);
  setFont(r.colon, f);
  setFont(r.mins, f);
  // Fonts put the colon at x-height middle: centre it on the digits instead.
  lv_font_glyph_dsc_t dg = {}, dc = {};
  if (lv_font_get_glyph_dsc(f, &dg, '0', 0) && lv_font_get_glyph_dsc(f, &dc, ':', 0))
    lv_obj_set_style_translate_y(r.colon, (dc.ofs_y * 2 + dc.box_h - dg.ofs_y * 2 - dg.box_h) / 2, 0);
  setFont(r.unit, unitFont(f));
  lv_obj_set_style_pad_bottom(r.unit, (lineH(f) - lineH(unitFont(f))) / 5, 0);  // share the baseline
  setFont(r.name, df);
  lv_obj_set_style_margin_top(r.name, -lineH(f) / 8 - cut, 0);
  showObj(r.name, date);
}


// Navigation has no state, label or secondary line on the display. Give the
// full inner rectangle to the chosen MDI/brand icon or uploaded PNG.
void fitNav(TileRt& r, int cw, int ch) {
  showObj(r.name, false);
  showObj(r.sub, false);
  if (r.pngIcon && !lv_obj_has_flag(r.pngIcon, LV_OBJ_FLAG_HIDDEN)) {
    showObj(r.chip, false);
    fitPngIcon(r);  // centred, with no label reservation for navigation
    return;
  }
  if (r.def->icon == "none") {
    showObj(r.chip, false);
    return;
  }
  showObj(r.chip, true);
  const int size = std::max(16, std::min(cw, ch));
  const int px = size >= 75 ? 40 : size >= 45 ? 28 : size >= 30 ? 20 : 16;
  lv_obj_set_size(r.chip, size, size);
  lv_obj_set_style_bg_opa(r.chip, LV_OPA_TRANSP, 0);
  lv_obj_center(r.chip);
  if (r.iconPx != px) {
    r.iconPx = px;
    r.iconCp = showIcon(r.icon, *r.def, nullptr, false, px, 0);
  }
  lv_obj_center(r.icon);
}

void fitTile(TileRt& r) {
  const int w = lv_obj_get_width(r.obj), h = lv_obj_get_height(r.obj);
  if (w <= 0 || h <= 0 || (w == r.fitW && h == r.fitH)) return;
  r.fitW = w;
  r.fitH = h;
  const int m = LV_MIN(w, h);
  // The clock uses a large time line and a small date; regular tile padding
  // needlessly makes a 2x1 clock too shallow for the 40px numerals.
  const int pad = m >= 80 ? 9 : m >= 56 ? 7 : 5;
  lv_obj_set_style_pad_all(r.obj, pad, 0);
  const int cw = w - 2 * pad, ch = h - 2 * pad;
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  if (r.def->type == TILE_BAMBU) fitBambu(r, cw, ch);
  else
#endif
  if (r.def->type == TILE_NAV) fitNav(r, cw, ch);
  else if (r.def->type == TILE_HA_MEDIA) fitHaMedia(r, cw, ch);
  else if (r.def->type == TILE_CLIMATE) fitInlineClimate(r, cw, ch);
  else if (r.def->type == TILE_SENSOR && r.def->style == METRIC_CHART) fitMetric(r, cw, ch);
  else if (r.def->type == TILE_SENSOR && r.def->style == METRIC_HEADLINE) fitHeadline(r, cw, ch);
  else if (r.def->type == TILE_SENSOR && r.def->style == METRIC_WEATHER) fitWeather(r, cw, ch);
  else if (r.def->type == TILE_CLOCK) fitClock(r, cw, ch);
  else if (r.def->type == TILE_SENSOR) fitSensor(r, cw, ch);
  else fitButton(r, cw, ch);
  markPageStale(r.page);
}

void fitEvent(lv_event_t* e) {
  const size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
  if (idx < s_tiles.size()) fitTile(s_tiles[idx]);
}

}  // namespace deckui
