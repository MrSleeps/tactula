// Guition deck configuration loader. See deck_config.h for the schema.
#include "deck_config.h"
#include "config.h"
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
#include "deck_office_bambu.h"
#endif
#include "psram_json.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_partition.h>

namespace deck {
namespace {

const char* const CONFIG_PATH = "/deck.json";

// Factory starter: a single neutral clock page. No Home Assistant entities,
// personal rooms or navigation. Existing /deck.json is never overwritten.
const char kDefaultDeck[] PROGMEM = R"JSON({
  "schema": 1,
  "theme": "glass",
  "pages": [
    {
      "id": "home",
      "name": "Home",
      "layouts": {
        "landscape": { "cols": 4, "rows": 3 },
        "portrait": { "cols": 3, "rows": 4 }
      },
      "tiles": [
        {
          "id": "clock",
          "type": "clock",
          "label": "",
          "box": false,
          "pos": {
            "landscape": { "x": 0, "y": 0, "w": 4, "h": 2 },
            "portrait": { "x": 0, "y": 0, "w": 3, "h": 2 }
          }
        }
      ]
    }
  ]
})JSON";

bool parseColor(const char* s, uint32_t& out) {
  if (!s || s[0] != '#' || strlen(s) != 7) return false;
  char* end = nullptr;
  const unsigned long v = strtoul(s + 1, &end, 16);
  if (!end || *end) return false;
  out = (uint32_t)v;
  return true;
}

bool parseHttp(JsonVariantConst v, TileAction& a, String* err, const String& where) {
  a.url = (const char*)(v["url"] | "");
  if (a.url.startsWith("https://")) {
    if (err) *err = where + ": https is not supported, use http on the LAN";
    return false;
  }
  if (!a.url.startsWith("http://") || a.url.length() < 8 || a.url.length() > 512) {
    if (err) *err = where + ": http needs an http:// url (max 512 characters)";
    return false;
  }
  for (JsonPairConst kv : v["headers"].as<JsonObjectConst>()) {
    const String name = kv.key().c_str();
    const String val = kv.value().as<const char*>() ? kv.value().as<const char*>() : "";
    if (!name.length() || name.indexOf(':') >= 0 || (name + val).indexOf('\n') >= 0 ||
        (name + val).indexOf('\r') >= 0) {
      if (err) *err = where + ": bad http header '" + name + "'";
      return false;
    }
    a.headers += name + ": " + val + "\n";
  }
  return true;
}

bool parseAction(JsonVariantConst v, TileAction& a, String* err, const String& where) {
  a = TileAction();
  if (v.isNull()) return true;
  const char* kind = v["action"] | "none";
  if (!strcmp(kind, "none")) {
    a.kind = ACT_NONE;
  } else if (!strcmp(kind, "toggle")) {
    a.kind = ACT_TOGGLE;
  } else if (!strcmp(kind, "more-info")) {
    a.kind = ACT_MORE_INFO;
  } else if (!strcmp(kind, "page")) {
    a.kind = ACT_PAGE;
    a.page = (const char*)(v["page"] | "");
    if (!a.page.length()) {
      if (err) *err = where + ": page action without a page id";
      return false;
    }
  } else if (!strcmp(kind, "http")) {
    a.kind = ACT_HTTP;
    if (!parseHttp(v, a, err, where)) return false;
    // A JSON body may be given as an object; it is sent as application/json.
    JsonVariantConst body = v["body"];
    if (body.is<const char*>()) a.body = (const char*)body;
    else if (!body.isNull()) {
      serializeJson(body, a.body);
      a.ctype = "application/json";
    }
    if (a.body.length() > 2048) {
      if (err) *err = where + ": http body too long (max 2048)";
      return false;
    }
    if (v["contentType"].is<const char*>()) a.ctype = (const char*)v["contentType"];
    if (!a.ctype.length() && a.body.length())
      a.ctype = a.body[0] == '{' || a.body[0] == '[' ? "application/json" : "text/plain";
    a.method = (const char*)(v["method"] | (a.body.length() ? "POST" : "GET"));
    a.method.toUpperCase();
    if (a.method != "GET" && a.method != "POST" && a.method != "PUT" && a.method != "PATCH" &&
        a.method != "DELETE") {
      if (err) *err = where + ": unknown http method '" + a.method + "'";
      return false;
    }
  } else if (!strcmp(kind, "call-service")) {
    a.kind = ACT_CALL_SERVICE;
    a.domain = (const char*)(v["domain"] | "");
    a.service = (const char*)(v["service"] | "");
    if (!a.domain.length() || !a.service.length()) {
      if (err) *err = where + ": call-service needs domain and service";
      return false;
    }
    if (!v["data"].isNull()) serializeJson(v["data"], a.data);
  } else {
    if (err) *err = where + ": unknown action '" + kind + "'";
    return false;
  }
  return true;
}

// A number of cells in half cells (1.5 -> 3), rounded to the nearest half.
uint8_t halves(JsonVariantConst v, int def, int lo, int hi) {
  if (!v.is<float>()) return (uint8_t)def;
  return (uint8_t)constrain((int)lroundf(v.as<float>() * 2), lo, hi);
}

void parsePos(JsonVariantConst v, TilePos& p) {
  if (v.isNull()) return;
  p.set = v["x"].is<float>() && v["y"].is<float>();
  p.x = halves(v["x"], 0, 0, 30);
  p.y = halves(v["y"], 0, 0, 30);
  p.w = halves(v["w"], 2, 1, 12);
  p.h = halves(v["h"], 2, 1, 12);
}

bool parseWhen(JsonVariantConst v, WhenDef& w, String* err, const String& where) {
  if (v.isNull()) return true;
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    // String conditions belonged to the removed PC companion.
    if (err) *err = where + ": unsupported legacy condition '" + String(s) + "'";
    return false;
  }
  const char* entity = v["entity"] | "";
  const char* state = v["state"] | "";
  if (!strchr(entity, '.') || state[0] == '\0' || !strcmp(state, "!")) {
    if (err) *err = where + ": when needs an entity and a state";
    return false;
  }
  w.kind = WHEN_ENTITY;
  w.entity = entity;
  w.negate = state[0] == '!';
  w.state = state + (w.negate ? 1 : 0);
  return true;
}

void parseGrid(JsonVariantConst v, uint8_t& cols, uint8_t& rows) {
  if (v.isNull()) return;
  cols = (uint8_t)constrain((int)(v["cols"] | cols), 1, 6);
  rows = (uint8_t)constrain((int)(v["rows"] | rows), 1, 6);
}

bool s_fsTried = false;
bool s_fsOk = false;

// A fresh flash partition is completely erased (all 0xFF). An unreadable
// filesystem isn't necessarily blank: it could contain a previous install's
// /deck.json, MDI font and wallpapers. Never destroy non-blank data silently.
// Scan only after a mount failure, not at every startup.
bool isInternalFsPartitionErased() {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  if (!part || part->size == 0) {
    Serial.println("Deck: internal flash partition not found");
    return false;
  }
  uint8_t buf[512];
  for (size_t offset = 0; offset < part->size; offset += sizeof(buf)) {
    const size_t remaining = size_t(part->size - offset);
    const size_t count = remaining < sizeof(buf) ? remaining : sizeof(buf);
    if (esp_partition_read(part, offset, buf, count) != ESP_OK) {
      Serial.println("Deck: flash read failed; refusing automatic format");
      return false;
    }
    for (size_t i = 0; i < count; ++i) {
      if (buf[i] != 0xFF) return false;
    }
  }
  return true;
}

bool mountFs() {
  if (!s_fsTried) {
    s_fsTried = true;
    // 8 held by icon fonts. Mount existing data without formatting first.
    s_fsOk = LittleFS.begin(false, "/littlefs", 14, "spiffs");
    if (!s_fsOk) {
      if (isInternalFsPartitionErased()) {
        Serial.println("Deck: brand-new blank internal flash; initializing LittleFS");
        s_fsOk = LittleFS.begin(true, "/littlefs", 14, "spiffs");
        Serial.println(s_fsOk ? "Deck: internal flash ready" :
                               "Deck: automatic LittleFS initialization FAILED");
      } else {
        Serial.println("Deck: LittleFS mount FAILED; existing/unknown data preserved");
      }
    }
  }
  return s_fsOk;
}

}  // namespace

bool deckConfigParse(const char* json, size_t len, DeckConfig& out, String* err) {
  JsonDocument doc(psramJson());
  const DeserializationError de = deserializeJson(doc, json, len);
  if (de) {
    if (err) *err = String("JSON: ") + de.c_str();
    return false;
  }
  DeckConfig cfg;
  cfg.schema = doc["schema"] | 0;
  if (cfg.schema != 1) {
    if (err) *err = "unsupported schema (expected 1)";
    return false;
  }
  cfg.theme = (const char*)(doc["theme"] | "midnight");
  cfg.ledEntity = (const char*)(doc["led"]["entity"] | "");
  if (cfg.ledEntity.length() && !cfg.ledEntity.startsWith("input_boolean.") &&
      !cfg.ledEntity.startsWith("input_number.") && !cfg.ledEntity.startsWith("light.")) {
    if (err) *err = "led entity must be an input_boolean, input_number or light";
    return false;
  }
  JsonArrayConst pages = doc["pages"].as<JsonArrayConst>();
  if (pages.isNull() || pages.size() == 0) {
    if (err) *err = "no pages";
    return false;
  }
  if (pages.size() > MAX_DECK_PAGES) {
    if (err) *err = "too many pages (max " + String(MAX_DECK_PAGES) + ")";
    return false;
  }
  for (JsonVariantConst pv : pages) {
    // Keep saved Bambu layouts in deck.json when the printer is disabled or
    // when switching to a non-Bambu image. They are simply not rendered then.
    const char* storedPageId = pv["id"] | "";
    const bool isBambuPage = !strcmp(storedPageId, "office_printer") ||
                             !strcmp(storedPageId, "office_ams");
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
    if (isBambuPage && !officeBambuConfigured()) continue;
#else
    if (isBambuPage) continue;
#endif
    PageDef p;
    p.id = (const char*)(pv["id"] | "");
    p.name = (const char*)(pv["name"] | "");
    p.group = (const char*)(pv["group"] | "");
    if (strlen(pv["printer"] | "")) continue;  // Ignore legacy printer pages.
    const char* pageWhen = pv["when"] | "";
    if (!strcmp(pageWhen, "pc-on") || !strcmp(pageWhen, "pc-off")) continue;
    const char* type = pv["type"] | "tiles";
    if (!strcmp(type, "metrics")) continue;  // Legacy PC-monitor pages.
    if (!p.id.length()) p.id = "page" + String(cfg.pages.size() + 1);
    if (!parseWhen(pv["when"], p.when, err, "page '" + p.id + "'")) return false;
    p.autoShow = p.when.kind != WHEN_ALWAYS && (pv["auto"] | false);
    parseGrid(pv["layouts"]["landscape"], p.cols[ORIENT_LANDSCAPE], p.rows[ORIENT_LANDSCAPE]);
    parseGrid(pv["layouts"]["portrait"], p.cols[ORIENT_PORTRAIT], p.rows[ORIENT_PORTRAIT]);
    JsonArrayConst tiles = pv["tiles"].as<JsonArrayConst>();
    if (tiles.size() > MAX_PAGE_TILES) {
      if (err) *err = "page '" + p.id + "': too many tiles";
      return false;
    }
    for (JsonVariantConst tv : tiles) {
      if (!strcmp(tv["type"] | "", "printer")) continue;  // Legacy saved tile.
      // A custom page can contain an independently moved Bambu widget. If
      // printer support is unavailable or disabled, omit that tile at runtime
      // but retain its JSON so switching firmware/enabling restores it.
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
      if (!strcmp(tv["type"] | "", "bambu") && !officeBambuConfigured()) continue;
#else
      if (!strcmp(tv["type"] | "", "bambu")) continue;
#endif
      const char* legacyWhen = tv["when"] | "";
      if (!strcmp(legacyWhen, "pc-on") || !strcmp(legacyWhen, "pc-off")) continue;
      // Legacy PC companion tiles are ignored without changing the stored JSON.
      const char* legacyType = tv["type"] | "";
      if (!strcmp(legacyType, "metric") || !strcmp(legacyType, "media") ||
          !strcmp(tv["control"] | "", "pc-volume") ||
          !strcmp(tv["tap"]["action"] | "", "pc") ||
          !strcmp(tv["hold"]["action"] | "", "pc")) continue;
      TileDef t;
      t.id = (const char*)(tv["id"] | "");
      const String where = "page '" + p.id + "' tile '" + t.id + "'";
      const char* tt = tv["type"] | "action";
      if (!strcmp(tt, "toggle")) t.type = TILE_TOGGLE;
      else if (!strcmp(tt, "action")) t.type = TILE_ACTION;
      else if (!strcmp(tt, "sensor")) t.type = TILE_SENSOR;
      else if (!strcmp(tt, "slider")) t.type = TILE_SLIDER;
      else if (!strcmp(tt, "clock")) t.type = TILE_CLOCK;
      else if (!strcmp(tt, "climate")) t.type = TILE_CLIMATE;
      else if (!strcmp(tt, "ha-media")) t.type = TILE_HA_MEDIA;
      else if (!strcmp(tt, "nav")) t.type = TILE_NAV;
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
      else if (!strcmp(tt, "bambu")) t.type = TILE_BAMBU;
#endif
      else {
        if (err) *err = where + ": unknown type '" + tt + "'";
        return false;
      }
      t.entity = (const char*)(tv["entity"] | "");
      t.label = (const char*)(tv["label"] | "");
      t.label.trim();
      t.noName = tv["label"].is<const char*>() && !t.label.length();
      t.sub = (const char*)(tv["sub"] | "");
      t.icon = (const char*)(tv["icon"] | "");
      if (t.type == TILE_ACTION || t.type == TILE_TOGGLE || t.type == TILE_SLIDER ||
          t.type == TILE_SENSOR || t.type == TILE_NAV) {
        t.iconPlain = t.type == TILE_NAV || !strcmp(tv["iconStyle"] | "", "plain");
        t.onIcon = (const char*)(tv["onIcon"] | "");
        t.statusEntity = (const char*)(tv["statusEntity"] | "");
        t.activeState = (const char*)(tv["activeState"] | "on");
        if (!t.activeState.length()) t.activeState = "on";
        t.onImage = (const char*)(tv["onImage"] | "");
        t.offImage = (const char*)(tv["offImage"] | "");
        // Old lightIcon settings continue to work until a new mode is selected.
        const char* lightColor = tv["lightColor"] | "";
        t.lightColorMode = !strcmp(lightColor, "both") ? 3 :
                           !strcmp(lightColor, "background") ? 2 :
                           !strcmp(lightColor, "icon") ? 1 : 0;
        if (!tv["lightColor"].is<const char*>() && (tv["lightIcon"] | false))
          t.lightColorMode = 1;
      }
      if ((t.type == TILE_CLOCK || t.type == TILE_SENSOR) && tv["box"].is<bool>())
        t.clockBox = tv["box"].as<bool>();
      if (t.type == TILE_SENSOR && tv["showEntityId"].is<bool>())
        t.showEntityId = tv["showEntityId"].as<bool>();
      t.hasColor = parseColor(tv["color"] | (const char*)nullptr, t.color);
      t.hasTextColor = parseColor(tv["textColor"] | (const char*)nullptr, t.textColor);
      t.hasChartColor = parseColor(tv["chartColor"] | (const char*)nullptr, t.chartColor);
      t.hasBgColor = parseColor(tv["bgColor"] | (const char*)nullptr, t.bgColor);
      t.hasOnColor = parseColor(tv["onColor"] | (const char*)nullptr, t.onColor);
      t.hasOnTextColor = parseColor(tv["onTextColor"] | (const char*)nullptr, t.onTextColor);
      t.hasIconColor = parseColor(tv["iconColor"] | (const char*)nullptr, t.iconColor);
      t.hasOnIconColor = parseColor(tv["onIconColor"] | (const char*)nullptr, t.onIconColor);
      if (tv["opacity"].is<int>()) t.opacity = (int8_t)constrain(tv["opacity"].as<int>(), 0, 100);
      if (tv["onOpacity"].is<int>()) t.onOpacity = (int8_t)constrain(tv["onOpacity"].as<int>(), 0, 100);
      parsePos(tv["pos"]["landscape"], t.pos[ORIENT_LANDSCAPE]);
      parsePos(tv["pos"]["portrait"], t.pos[ORIENT_PORTRAIT]);
      if (!parseWhen(tv["when"], t.when, err, where)) return false;
      if (t.type == TILE_SENSOR) {
        if (!tv["http"].isNull()) {
          TileAction source;
          if (!tv["http"].is<JsonObjectConst>() || !parseHttp(tv["http"], source, err, where + " http")) {
            if (err && !err->length()) *err = where + ": http must be an object";
            return false;
          }
          t.http.enabled = true;
          t.http.url = source.url;
          t.http.headers = source.headers;
          t.http.path = (const char*)(tv["http"]["path"] | "");
          t.http.poll = (uint16_t)constrain((int)(tv["http"]["poll"] | 30), 5, 3600);
          t.unit = (const char*)(tv["unit"] | "");
          t.detail = (const char*)(tv["detail"] | "");
          t.entity = "";
          JsonVariantConst rows = tv["rows"];
          if (!rows.isNull()) {
            if (!rows.is<JsonArrayConst>() || rows.size() < 1 || rows.size() > MAX_SENSOR_ROWS) {
              if (err) *err = where + ": rows must be an array of 1.." + String(MAX_SENSOR_ROWS);
              return false;
            }
            for (JsonVariantConst rv : rows.as<JsonArrayConst>()) {
              SensorRowDef row;
              row.label = (const char*)(rv["label"] | "");
              row.path = (const char*)(rv["path"] | "");
              row.value = (const char*)(rv["value"] | "");
              if (!row.path.length()) {
                if (err) *err = where + ": every row needs a path";
                return false;
              }
              t.rows.push_back(row);
            }
            t.scaleMax = (uint16_t)constrain((int)(tv["scaleMax"] | 0), 0, 65535);
          }
        }
        const char* style = tv["style"] | "number";
        t.style = !strcmp(style, "chart") && t.rows.empty() ? METRIC_CHART :
                  !strcmp(style, "headline") && t.rows.empty() ? METRIC_HEADLINE :
                  !strcmp(style, "weather") && t.rows.empty() && t.entity.startsWith("weather.")
                    ? METRIC_WEATHER : METRIC_NUMBER;
        t.duration = !strcmp(tv["format"] | "", "duration");
        t.hours = (uint8_t)constrain((int)(tv["hours"] | 24), 1, 168);
        t.warn = tv["warn"] | 0.0f;
        t.crit = tv["crit"] | 0.0f;
        t.hasWarn = tv["warn"].is<float>() && isfinite(t.warn);
        t.hasCrit = tv["crit"].is<float>() && isfinite(t.crit);
      }
      if (t.type == TILE_HA_MEDIA) {
        if (!t.entity.startsWith("media_player.")) {
          if (err) *err = where + ": media controls require a media_player entity";
          return false;
        }
        p.tiles.push_back(t);
        continue;  // in-tile Home Assistant media controls
      }
      if (t.type == TILE_CLIMATE) {
        if (!t.entity.startsWith("climate.")) {
          if (err) *err = where + ": climate control requires a climate entity";
          return false;
        }
        p.tiles.push_back(t);
        continue;  // inline thermostat has its own controls, not tile tap actions
      }
      if (t.type == TILE_CLOCK) {
        p.tiles.push_back(t);
        continue;
      }
      if (!parseAction(tv["tap"], t.tap, err, where + " tap")) return false;
      if (!parseAction(tv["hold"], t.hold, err, where + " hold")) return false;
      // Navigation tiles only change pages; no entity or thermostat action.
      if (t.type == TILE_NAV && (t.tap.kind != ACT_PAGE || t.hold.kind != ACT_NONE)) {
        if (err) *err = where + ": navigation requires tap: {action: page, page: id} and no hold action";
        return false;
      }
      const char* cf = tv["confirm"] | "";
      if (!strcmp(cf, "off")) t.confirm = CONFIRM_OFF;
      else if (!strcmp(cf, "always")) t.confirm = CONFIRM_ALWAYS;
      else if (*cf) {
        if (err) *err = where + ": unknown confirm '" + cf + "'";
        return false;
      }
      // Defaults: a toggle toggles, anything holds to its detail card.
      if (tv["tap"].isNull() && t.type == TILE_TOGGLE) t.tap.kind = ACT_TOGGLE;
      // A read-only sensor opens its card on a tap too; weather details use it too.
      if (tv["tap"].isNull() && t.type == TILE_SENSOR && !t.http.enabled) t.tap.kind = ACT_MORE_INFO;
      // A slider toggles on a tap; a number has no on/off, so its card opens.
      if (tv["tap"].isNull() && t.type == TILE_SLIDER)
        t.tap.kind = t.entity.startsWith("input_number.") || t.entity.startsWith("number.")
                         ? ACT_MORE_INFO
                         : ACT_TOGGLE;
      if (tv["hold"].isNull() && !t.http.enabled && t.type != TILE_NAV) t.hold.kind = ACT_MORE_INFO;
      // Older saved bedroom decks can retain TV tiles but lose their tap
      // action. Restore only known streaming tile IDs with absent/no-op taps
      // and the expected state entity (or none). Never replace a custom action
      // or a tile connected to a different entity.
      if (p.id == "tv" && t.type == TILE_ACTION && t.tap.kind == ACT_NONE &&
          (!strcmp(tv["tap"]["action"] | "", "none"))) {
        const char* stateEntity = nullptr;
        const char* script = nullptr;
        if (t.id == "netflix") {
          stateEntity = "binary_sensor.bedroom_hal_lg_tv_is_netflix_on";
          script = "script.turn_on_tv_and_start_netflix";
        } else if (t.id == "plex") {
          stateEntity = "binary_sensor.bedroom_hal_lg_tv_is_plex_on";
          script = "script.turn_on_tv_and_start_plex";
        } else if (t.id == "iplayer") {
          stateEntity = "binary_sensor.bedroom_hal_lg_tv_is_bbc_iplayer_on";
          script = "script.turn_on_tv_and_start_iplayer";
        } else if (t.id == "disney") {
          stateEntity = "binary_sensor.bedroom_hal_lg_tv_is_disney_on";
          script = "script.turn_on_tv_and_start_disney";
        } else if (t.id == "amazon") {
          stateEntity = "binary_sensor.bedroom_hal_lg_tv_is_prime_video_on";
          script = "script.turn_on_tv_and_start_amazon_prime";
        }
        if (script && (!t.entity.length() || t.entity == stateEntity)) {
          t.entity = stateEntity;
          t.tap.kind = ACT_CALL_SERVICE;
          t.tap.domain = "script";
          t.tap.service = "turn_on";
          t.tap.data = String("{\"entity_id\":\"") + script + "\"}";
          if (tv["hold"].isNull() || !strcmp(tv["hold"]["action"] | "", "none")) {
            t.hold.kind = ACT_CALL_SERVICE;
            t.hold.domain = "script";
            t.hold.service = "turn_on";
            t.hold.data = "{\"entity_id\":\"script.hal_tv_play_pause\"}";
          }
        }
      }
      p.tiles.push_back(t);
    }
    cfg.pages.push_back(p);
  }
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  // The default printer screen is a generated overlay, never written to
  // /deck.json. A new with_bambu device starts with its one clock page and
  // acquires this page only after an enabled printer has valid saved credentials.
  // Do not make visibility depend on the printer being online: the user must
  // still be able to see printer status during WiFi or printer outages.
  const bool configured = deck::officeBambuConfigured();
  bool hasPrinterPage = false, hasAmsPage = false;
  for (const PageDef& page : cfg.pages) {
    if (page.id == "office_printer") hasPrinterPage = true;
    if (page.id == "office_ams") hasAmsPage = true;
  }
  // Runtime-only pages, modeled on the upstream TouchDeck printer and AMS
  // layouts. No printer-specific page or tile is persisted to /deck.json.
  auto addBambuTile = [](PageDef& page, const char* id, const char* title,
                         const char* view,
                         int lx, int ly, int lw, int lh,
                         int px, int py, int pw, int ph) {
    TileDef tile;
    tile.id = String("bambu_") + id;
    tile.type = TILE_BAMBU;
    tile.label = title;
    tile.sub = view;
    const int geometry[2][4] = {{lx, ly, lw, lh}, {px, py, pw, ph}};
    for (int orient = 0; orient < 2; ++orient) {
      tile.pos[orient].set = true;
      tile.pos[orient].x = geometry[orient][0] * 2;
      tile.pos[orient].y = geometry[orient][1] * 2;
      tile.pos[orient].w = geometry[orient][2] * 2;
      tile.pos[orient].h = geometry[orient][3] * 2;
    }
    page.tiles.push_back(tile);
  };
  if (configured && !(doc["bambu_pages_initialized"] | false) &&
      !hasPrinterPage && cfg.pages.size() < MAX_DECK_PAGES) {
    PageDef printer;
    printer.id = "office_printer";
    printer.name = "P1S Printer";
    printer.group = "bambu_printer";
    printer.cols[ORIENT_LANDSCAPE] = 4;
    printer.rows[ORIENT_LANDSCAPE] = 3;
    printer.cols[ORIENT_PORTRAIT] = 3;
    printer.rows[ORIENT_PORTRAIT] = 4;
    // Original TouchDeck-style 4x3 status layout: hero across the top,
    // four temperature cells, then filament (2), layer (1) and light (1).
    // These become ordinary editable tiles in /deck.json on first setup.
    addBambuTile(printer, "hero", "Print status", "hero",
                 0, 0, 4, 1, 0, 0, 3, 1);
    addBambuTile(printer, "nozzle_left", "Left nozzle", "nozzle_left",
                 0, 1, 1, 1, 0, 1, 1, 1);
    addBambuTile(printer, "nozzle_right", "Right nozzle", "nozzle_right",
                 1, 1, 1, 1, 1, 1, 1, 1);
    addBambuTile(printer, "bed", "Bed", "bed",
                 2, 1, 1, 1, 2, 1, 1, 1);
    addBambuTile(printer, "chamber", "Chamber", "chamber",
                 3, 1, 1, 1, 0, 2, 1, 1);
    addBambuTile(printer, "filament", "Filament", "filament",
                 0, 2, 2, 1, 1, 2, 2, 1);
    addBambuTile(printer, "layer", "Layer", "layer",
                 2, 2, 1, 1, 0, 3, 1, 1);
    addBambuTile(printer, "light", "Chamber light", "light",
                 3, 2, 1, 1, 1, 3, 2, 1);
    cfg.pages.push_back(printer);
  }
  if (configured && !(doc["bambu_pages_initialized"] | false) &&
      !hasAmsPage && cfg.pages.size() < MAX_DECK_PAGES) {
    PageDef ams;
    ams.id = "office_ams";
    ams.name = "P1S AMS";
    ams.group = "bambu_printer";  // swipe vertically from the printer page
    ams.cols[ORIENT_LANDSCAPE] = 4;
    ams.rows[ORIENT_LANDSCAPE] = 3;
    ams.cols[ORIENT_PORTRAIT] = 3;
    ams.rows[ORIENT_PORTRAIT] = 4;
    addBambuTile(ams, "ams", "AMS", "ams",
                 0, 0, 4, 1, 0, 0, 3, 1);
    addBambuTile(ams, "slots", "Filament slots", "slots",
                 0, 1, 4, 2, 0, 1, 3, 3);
    cfg.pages.push_back(ams);
  }
#endif
  if (cfg.pages.empty()) {
    if (err) *err = "no pages";
    return false;
  }
  out = cfg;
  return true;
}

bool deckActionParse(const char* json, size_t len, TileAction& out, String* err) {
  JsonDocument doc;
  const DeserializationError de = deserializeJson(doc, json, len);
  if (de) {
    if (err) *err = String("JSON: ") + de.c_str();
    return false;
  }
  return parseAction(doc.as<JsonVariantConst>(), out, err, "action");
}

bool deckFsReady() { return mountFs(); }

bool deckFsInitialize(String& err) {
  // Never format an already mounted filesystem (which may contain the user's
  // saved pages, icon fonts and pictures). A mount failure may mean either
  // virgin flash or older data written by another filesystem version.
  if (mountFs()) {
    err = "Internal flash is already mounted; no initialization needed.";
    return false;
  }
  Serial.println("Deck: explicit user-requested internal flash LittleFS format");
  // formatOnFail=true is deliberate here ONLY, after the user confirms the
  // destructive operation in Maintenance; never enable it during boot.
  s_fsOk = LittleFS.begin(true, "/littlefs", 14, "spiffs");
  s_fsTried = true;
  if (!s_fsOk) {
    err = "Internal flash initialization failed. Check the partition table and serial log.";
    Serial.println("Deck: explicit LittleFS initialization FAILED");
    return false;
  }
  Serial.println("Deck: LittleFS initialized and mounted");
  return true;
}

static bool wipeDirectory(const String& path, const char* keep) {
  std::vector<String> paths;
  File dir = LittleFS.open(path);
  if (!dir || !dir.isDirectory()) return false;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) paths.push_back(f.path());
  dir.close();
  bool ok = true;
  for (const String& p : paths) {
    if (p == keep) continue;
    File f = LittleFS.open(p);
    const bool directory = f && f.isDirectory();
    f.close();
    if (directory) {
      ok &= wipeDirectory(p, keep);
      ok &= LittleFS.rmdir(p);
    } else ok &= LittleFS.remove(p);
  }
  return ok;
}

bool deckFsWipe(const char* keep) {
  return mountFs() && wipeDirectory("/", keep);
}

void deckConfigText(String& out) {
  if (mountFs() && LittleFS.exists(CONFIG_PATH)) {
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (f) {
      f.setBufferSize(8192);  // over 4 KB: malloc puts it in PSRAM
      out = f.readString();
      f.close();
      // Stored files passed deckConfigParse already; only catch a damaged one.
      // A full parse here took ~14 KB of internal RAM in small Strings.
      JsonDocument doc(psramJson());
      if (!deserializeJson(doc, out) && doc["pages"].is<JsonArrayConst>()) {
        // Keep the web editor in step with the effective actions used by
        // deckConfigParse. Saving persists only these absent/no-op defaults.
        bool changed = false;
        for (JsonObject page : doc["pages"].as<JsonArray>()) {
          if (strcmp(page["id"] | "", "tv")) continue;
          for (JsonObject tile : page["tiles"].as<JsonArray>()) {
            if (strcmp(tile["type"] | "action", "action") ||
                strcmp(tile["tap"]["action"] | "", "none")) continue;
            const char* id = tile["id"] | "";
            const char* stateEntity = nullptr;
            const char* script = nullptr;
            if (!strcmp(id, "netflix")) {
              stateEntity = "binary_sensor.bedroom_hal_lg_tv_is_netflix_on";
              script = "script.turn_on_tv_and_start_netflix";
            } else if (!strcmp(id, "plex")) {
              stateEntity = "binary_sensor.bedroom_hal_lg_tv_is_plex_on";
              script = "script.turn_on_tv_and_start_plex";
            } else if (!strcmp(id, "iplayer")) {
              stateEntity = "binary_sensor.bedroom_hal_lg_tv_is_bbc_iplayer_on";
              script = "script.turn_on_tv_and_start_iplayer";
            } else if (!strcmp(id, "disney")) {
              stateEntity = "binary_sensor.bedroom_hal_lg_tv_is_disney_on";
              script = "script.turn_on_tv_and_start_disney";
            } else if (!strcmp(id, "amazon")) {
              stateEntity = "binary_sensor.bedroom_hal_lg_tv_is_prime_video_on";
              script = "script.turn_on_tv_and_start_amazon_prime";
            }
            if (!script) continue;
            const char* currentEntity = tile["entity"] | "";
            if (strlen(currentEntity) && strcmp(currentEntity, stateEntity)) continue;
            tile["entity"] = stateEntity;
            tile["tap"]["action"] = "call-service";
            tile["tap"]["domain"] = "script";
            tile["tap"]["service"] = "turn_on";
            tile["tap"]["data"]["entity_id"] = script;
            if (tile["hold"].isNull() || !strcmp(tile["hold"]["action"] | "", "none")) {
              tile["hold"]["action"] = "call-service";
              tile["hold"]["domain"] = "script";
              tile["hold"]["service"] = "turn_on";
              tile["hold"]["data"]["entity_id"] = "script.hal_tv_play_pause";
            }
            changed = true;
          }
        }
        if (changed) {
          out = "";  // ArduinoJson's String writer appends to the output.
          serializeJson(doc, out);
        }
        return;
      }
    }
  }
  out = FPSTR(kDefaultDeck);
}

#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
bool deckEnsureBambuStarterPages(String& err) {
  if (!officeBambuConfigured()) return true;

  String original;
  deckConfigText(original);
  JsonDocument doc(psramJson());
  if (deserializeJson(doc, original)) {
    err = "Could not read layout to initialize Bambu pages";
    return false;
  }
  if (doc["bambu_pages_initialized"] | false) return true;
  JsonArray pages = doc["pages"].as<JsonArray>();
  if (pages.isNull()) {
    err = "Cannot find layout pages";
    return false;
  }

  // Reuse the firmware's canonical starter template (not a second hardcoded
  // design). On the very first setup deckConfigParse() generates the two
  // runtime printer pages if there is room; serialize them to /deck.json.
  DeckConfig defaults;
  if (!deckConfigParse(original.c_str(), original.length(), defaults, &err))
    return false;
  bool any = false;
  for (const PageDef& page : defaults.pages) {
    if (page.id != "office_printer" && page.id != "office_ams") continue;
    any = true;
    bool exists = false;
    for (JsonVariant p : pages)
      if (String((const char*)(p["id"] | "")) == page.id) exists = true;
    if (exists) continue;
    JsonObject jsonPage = pages.add<JsonObject>();
    jsonPage["id"] = page.id;
    jsonPage["name"] = page.name;
    jsonPage["group"] = page.group;
    JsonObject layouts = jsonPage["layouts"].to<JsonObject>();
    JsonObject land = layouts["landscape"].to<JsonObject>();
    land["cols"] = page.cols[ORIENT_LANDSCAPE];
    land["rows"] = page.rows[ORIENT_LANDSCAPE];
    JsonObject port = layouts["portrait"].to<JsonObject>();
    port["cols"] = page.cols[ORIENT_PORTRAIT];
    port["rows"] = page.rows[ORIENT_PORTRAIT];
    JsonArray tiles = jsonPage["tiles"].to<JsonArray>();
    for (const TileDef& tile : page.tiles) {
      JsonObject jt = tiles.add<JsonObject>();
      jt["id"] = tile.id;
      jt["type"] = "bambu";
      jt["label"] = tile.label;
      jt["sub"] = tile.sub;
      JsonObject pos = jt["pos"].to<JsonObject>();
      const char* names[] = {"landscape", "portrait"};
      for (int i = 0; i < 2; ++i) {
        JsonObject cell = pos[names[i]].to<JsonObject>();
        cell["x"] = tile.pos[i].x / 2.0f;
        cell["y"] = tile.pos[i].y / 2.0f;
        cell["w"] = tile.pos[i].w / 2.0f;
        cell["h"] = tile.pos[i].h / 2.0f;
      }
    }
  }
  if (!any) {
    // Full eight-page decks must not be silently modified or marked. If
    // space is freed, the user can configure the printer again to retry.
    return true;
  }

  doc["bambu_pages_initialized"] = true;
  String updated;
  serializeJson(doc, updated);
  DeckConfig checked;
  if (!deckConfigStore(updated, checked, err)) return false;
  Serial.println("Deck: saved editable Bambu starter pages in /deck.json");
  return true;
}
#endif

bool deckConfigStore(const String& text, DeckConfig& out, String& err) {
  DeckConfig cfg;
  if (!deckConfigParse(text.c_str(), text.length(), cfg, &err)) return false;
  if (!mountFs()) {
    err = "Internal flash not mounted. Go to Maintenance > Internal flash storage to check it.";
    return false;
  }
  const char* tmp = "/deck.json.tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) {
    err = "cannot write the file";
    return false;
  }
  const size_t n = f.print(text);
  f.close();
  if (n != text.length()) {
    LittleFS.remove(tmp);
    err = "storage full";
    return false;
  }
  LittleFS.remove(CONFIG_PATH);
  if (!LittleFS.rename(tmp, CONFIG_PATH)) {
    err = "cannot replace the file";
    return false;
  }
  out = cfg;
  return true;
}

bool deckConfigLoad(DeckConfig& out, String* source, String* err) {
  if (err) *err = "";
  if (mountFs() && LittleFS.exists(CONFIG_PATH)) {
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (f) {
      String text = f.readString();
      f.close();
      String why;
      if (deckConfigParse(text.c_str(), text.length(), out, &why)) {
        if (source) *source = CONFIG_PATH;
        return true;
      }
      if (err) *err = why;
      Serial.printf("Deck: %s rejected (%s), using the built-in deck\n", CONFIG_PATH, why.c_str());
    }
  }
  if (source) *source = "built-in";
  return deckConfigParse(kDefaultDeck, strlen_P(kDefaultDeck), out, nullptr);
}

}  // namespace deck
