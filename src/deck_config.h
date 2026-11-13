// Guition deck configuration: `deck.json` schema v1 (see the deck plan).
//
// { "schema": 1, "theme": "midnight",
//   "pages": [
//     { "id": "desk", "name": "Desk",
//       "layouts": { "landscape": { "cols": 4, "rows": 3 } },
//       "tiles": [
//         { "id": "t1", "type": "toggle", "entity": "light.desk_lamp",
//           "label": "Desk lamp", "sub": null, "icon": "mdi:lightbulb",
//           "color": "#F6C56B",
//           "pos": { "landscape": { "x": 0, "y": 0, "w": 1, "h": 1 } },
//           "tap":  { "action": "toggle" },
//           "hold": { "action": "more-info" } } ] } ] }
//
// "led": { "entity": "input_number.desk_led" } (optional): the status LED's
// day level follows an input_boolean, input_number or light.
//
// A sensor tile: { "type": "sensor", "entity": "sensor.temperature",
//   "style": "number" | "chart", "hours": 24 (1..168),
//   "warn": 25, "crit": 30 } (thresholds optional, in the sensor unit).
// crit < warn means lower is worse; otherwise higher is worse.
// "format": "duration" shows a number in s / min / h / d as "1h 49m".
//
// An HTTP sensor uses "http" instead of "entity":
// { "type": "sensor", "http": { "url": "http://host/usage",
//   "path": "cpu.used", "poll": 30, "headers": { "Name": "value" } },
//   "unit": "%", "style": "number" | "chart", "warn": 70, "crit": 90 }.
// GET only; poll = 5..3600 seconds (default 30). Dot paths accept array
// indexes; empty path reads a scalar body. Charts keep 48 numeric readings.
// HTTP sensors have no default tap/hold action or HA detail card.
// "detail": "runtime {ups.runtime_min|min}" adds a small line with
// values from the same reply; "|unit" formats a number as a duration.
// "rows": [{ "label": "5h", "path": "claude.five_hour.used" }, ...] (1..4)
// shows label, bar and value per row under the name instead of one reading;
// the bar spans 0..scaleMax (default 100) and goes when the tile is too narrow.
// A row "value": "{codex.five_hour.reset_min|min}" shows that text instead of
// the reading while the bar fits, else the reading. "{path}" fields also work
// in an HTTP sensor's "label" ("Codex {codex.five_hour.used}%").
// "icon": "none" on a button or sensor tile shows no icon.
//
// "type": "clock": the time, and the date when the tile has room
// (24h and time zone from the device settings). Read-only.
//
// "textColor": "#RRGGBB" (optional) colours the tile's text; for a metric
// tile "chartColor" colours the chart or bar line apart from "color" (the name).
// Tile look, all optional, else the theme: "bgColor" (background while off,
// the glass tint in Glass), "onColor" (fill while on, default "color"),
// "onTextColor", and in Glass "opacity" / "onOpacity" (0..100 %, else the
// display's Tile opacity / On opacity; auto text may still darken the glass).
// Action, toggle, slider and sensor tiles take "iconStyle": "plain" (no
// circle, the icon fills its place), "iconColor" / "onIconColor" (else
// "color"; a plain icon while on: the text colour), "onIcon" (toggle) and
// "lightColor": "fixed" | "icon" | "background" | "both" for HA lights.
// Legacy "lightIcon": true is treated as "icon".
//
// "type": "slider": the tile is a level control (light brightness, cover
// position, fan speed, media volume, input_number / number); drag sideways to
// set it, tap toggles (numbers: opens the card).
// "confirm": "off" | "always" (optional, tap and hold actions): ask on the
// device before turning the tile off, or before any action it runs.
//
// "when" (optional, any tile): the tile shows only while a condition holds
// and leaves its cell free otherwise, so two tiles can share a cell.
//   { "entity": "switch.pc", "state": "on" }: the HA state equals "state";
//     "!on" = differs. Hidden until Home Assistant reports the entity.
// A page takes "when" too (its PC is the first known one) and "auto": true:
// the deck slides to the page when it appears and back to the page shown
// before when it goes.
//
// An HTTP request as a tap or hold action (plain http on the LAN only):
// { "action": "http", "method": "POST", "url": "http://host/path",
//   "body": "text" | { json }, "contentType": "...", "headers": { "K": "v" } }.
// method defaults to GET, or POST with a body.
//
// label / icon null = take them from Home Assistant later (Phase 3); until
// then the entity id stands in. `sub` is an optional fixed second line.
// Positions are keyed by orientation; a tile without one for the current
// orientation flows into the next free cell. Adjacent pages with the same
// "group" stack into one column: left/right moves between columns, up/down
// between the pages of a column. Pages of type "metrics" (left
// over from StatNode) are skipped.
#ifndef DECK_CONFIG_H
#define DECK_CONFIG_H

#include <Arduino.h>
#include <vector>

namespace deck {

constexpr int ORIENT_LANDSCAPE = 0;
constexpr int ORIENT_PORTRAIT = 1;
constexpr int MAX_DECK_PAGES = 8;
constexpr int MAX_PAGE_TILES = 24;

enum TileType : uint8_t { TILE_ACTION = 0, TILE_TOGGLE, TILE_SENSOR, TILE_SLIDER, TILE_CLOCK, TILE_CLIMATE, TILE_HA_MEDIA, TILE_NAV, TILE_BAMBU };
enum MetricStyle : uint8_t { METRIC_NUMBER = 0, METRIC_CHART, METRIC_BAR, METRIC_HEADLINE, METRIC_WEATHER };
enum ActionKind : uint8_t {
  ACT_NONE = 0,
  ACT_TOGGLE,        // toggle the tile's entity
  ACT_CALL_SERVICE,  // {domain, service, data}
  ACT_PAGE,          // {page: id}
  ACT_MORE_INFO,     // detail popup
  ACT_HTTP,          // {method, url, body, contentType, headers}
};
enum ConfirmMode : uint8_t { CONFIRM_NONE = 0, CONFIRM_OFF, CONFIRM_ALWAYS };
enum WhenKind : uint8_t { WHEN_ALWAYS = 0, WHEN_ENTITY };

struct WhenDef {
  WhenKind kind = WHEN_ALWAYS;
  String entity, state;  // WHEN_ENTITY
  bool negate = false;   // "state": "!on"
};

struct TileAction {
  ActionKind kind = ACT_NONE;
  String domain, service, data;  // data = service_data as JSON text
  String page;
  String method, url, body, ctype;  // ACT_HTTP
  String headers;                   // ACT_HTTP: "Name: value\n" lines
};

// set = an explicit cell (x, y). w and h always apply: a tile with only a
// span flows into the next free place that fits it. All four in half cells
// (deck.json takes 0.5 steps: "w": 1.5).
struct TilePos {
  bool set = false;
  uint8_t x = 0, y = 0, w = 2, h = 2;
};

struct HttpSensorDef {
  bool enabled = false;
  String url, headers, path;
  uint16_t poll = 30;
};

constexpr int MAX_SENSOR_ROWS = 4;
struct SensorRowDef {
  String label, path;
  String value;  // optional text template like "detail"; the bar keeps path
};

struct TileDef {
  String id;
  TileType type = TILE_ACTION;
  String entity, label, sub, icon;
  bool noName = false;  // "label": "" (or blank): show no name instead of HA's
  bool iconPlain = false;   // "iconStyle": "plain": no circle
  bool clockBox = true;    // Clock/headline/weather: "box": false removes fill and border.
  bool showEntityId = true; // Weather popup: show the Home Assistant entity ID under its title.
  String onIcon;            // toggle: the icon while on, "" = icon
  String statusEntity;      // optional HA status entity for visual state
  String activeState = "on";
  String onImage, offImage; // PNG filenames on microSD
  uint8_t lightColorMode = 0; // "lightColor": fixed=0, icon=1, background=2, both=3
  HttpSensorDef http;
  String unit;  // HTTP sensor: unit for JSON numbers only
  String detail;  // HTTP sensor: second line template, see above
  std::vector<SensorRowDef> rows;  // HTTP sensor: label + bar + value lines
  bool duration = false;  // sensor: "format": "duration"
  bool hasColor = false;
  uint32_t color = 0;
  bool hasTextColor = false, hasChartColor = false;
  uint32_t textColor = 0, chartColor = 0;
  bool hasBgColor = false, hasOnColor = false, hasOnTextColor = false;
  uint32_t bgColor = 0, onColor = 0, onTextColor = 0;
  bool hasIconColor = false, hasOnIconColor = false;
  uint32_t iconColor = 0, onIconColor = 0;
  int8_t opacity = -1, onOpacity = -1;  // Glass, %, -1 = the display setting
  TilePos pos[2];
  TileAction tap, hold;
  ConfirmMode confirm = CONFIRM_NONE;
  MetricStyle style = METRIC_NUMBER;
  uint8_t hours = 24;  // TILE_SENSOR: history period, 1..168 hours
  bool hasWarn = false, hasCrit = false;
  float warn = 0, crit = 0;
  uint16_t scaleMax = 0;  // 0 = full scale by unit (sensor rows: 100)
  WhenDef when;
  bool shown = true;  // runtime: "when" as the pages were last built
};

struct PageDef {
  String id, name;
  String group;  // adjacent pages with the same group form one column
  WhenDef when;
  bool autoShow = false;  // "auto": slide here when it appears, back when it goes
  bool shown = true;      // runtime, as for tiles
  uint8_t cols[2] = {4, 3};
  uint8_t rows[2] = {3, 4};
  std::vector<TileDef> tiles;
};

struct DeckConfig {
  uint8_t schema = 1;
  String theme;
  String ledEntity;  // HA entity the LED's day level follows, empty = none
  std::vector<PageDef> pages;
};

// Mounts LittleFS (the board's spare data partition) on first use. Loads
// /deck.json when present and valid, otherwise the built-in default. Fills
// *source with where the config came from and *err with why a stored file
// was rejected (empty when it was not).
bool deckConfigLoad(DeckConfig& out, String* source, String* err);
// Parse and validate a JSON document. Used by the loader and, later, by the
// portal before anything is written.
bool deckConfigParse(const char* json, size_t len, DeckConfig& out, String* err);
// One tap/hold action object on its own (the portal's HTTP Test button).
bool deckActionParse(const char* json, size_t len, TileAction& out, String* err);
// The stored /deck.json text, or the built-in default.
void deckConfigText(String& out);
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
// Persist the initial generated printer/AMS templates once, on first setup.
// Afterwards /deck.json owns the page layouts and deletion/order choices.
bool deckEnsureBambuStarterPages(String& err);
#endif
// Validate and store as /deck.json (written to a temp file, then renamed, so a
// power cut never leaves half a file). On success *out is the parsed config.
bool deckConfigStore(const String& text, DeckConfig& out, String& err);
// LittleFS mounted (mounts on first call).
bool deckFsReady();
// Explicit recovery/first-use operation. Formats only on user request when
// the internal filesystem cannot mount. WARNING: erases its old contents.
bool deckFsInitialize(String& err);
// Factory reset: delete every stored file but keep (a path such as "/mdi.ttf").
bool deckFsWipe(const char* keep);

}  // namespace deck

#endif  // DECK_CONFIG_H
