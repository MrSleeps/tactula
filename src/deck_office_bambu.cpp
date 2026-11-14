// Office-only Bambu controller. Secrets are stored in NVS and never sent to
// a browser, serialized into deck.json, or included in configuration backups.
#include "deck_office_bambu.h"
#include "deck_bambu.h"
#include "deck_bambu_ha.h"
#include "deck_ha.h"
#include "psram_json.h"
#include "deck_ui.h"
#include "deck_config.h"
#include <ArduinoJson.h>
#include <Preferences.h>

namespace deck {
namespace {
constexpr const char* NVS = "officebambu";
constexpr const char* ID = "p1s";
bool s_enabled = false;
bool s_ha = false;
String s_address, s_serial, s_code, s_displayName;
String s_liveHaName;
uint32_t s_lastNameLookup = 0;

bool validAddress(const String& s) {
  if (!s.length() || s.length() > 128) return false;
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (!isalnum((unsigned char)c) && c != '.' && c != '-' && c != ':') return false;
  }
  return true;
}

bool validSecret(const String& s, size_t maxLength) {
  if (s.length() > maxLength) return false;
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (!isalnum((unsigned char)c) && c != '-' && c != '_') return false;
  }
  return true;
}

void apply() {
  // Tear down both potential sources before changing connection settings.
  printersHaSet({});
  printersSet({});
  if (!s_enabled) return;
  if (s_ha) {
    if (s_serial.length()) {
      printersHaSet({{ID, s_serial}});
      printerHaPower(ID, true);
    }
  } else if (s_address.length() && s_serial.length() && s_code.length()) {
    printersSet({{ID, s_address, s_serial, s_code}});
    printerPower(ID, true);
  }
}
}

void officeBambuBegin() {
  Preferences pref;
  if (pref.begin(NVS, true)) {
    s_enabled = pref.getBool("enabled", false);
    s_ha = pref.getBool("ha", false);
    s_address = pref.getString("address", "");
    s_serial = pref.getString("serial", "");
    s_code = pref.getString("code", "");
    s_displayName = pref.getString("display_name", "");
    pref.end();
  }
  apply();
  if (s_enabled) {
    String layoutError;
    if (!deckEnsureBambuStarterPages(layoutError))
      Serial.printf("Bambu: could not persist starter pages: %s\n", layoutError.c_str());
  }
}

void officeBambuTick() {
  if (s_enabled && s_ha) printersHaTick();
  // A LAN-connected printer never enters printersHaTick(), so it needs a
  // separate (infrequent) name lookup. Match the entity's *entity ID* here;
  // the Bambu HA registry unique ID instead ends in "_name".
  if (!s_enabled || s_ha || !s_serial.length() || haLink() != HA_CONNECTED) return;
  const uint32_t now = millis();
  if (s_lastNameLookup && now - s_lastNameLookup < 300000UL) return;
  s_lastNameLookup = now;
  String serialSlug = s_serial;
  serialSlug.toLowerCase();
  const String entity = "sensor.p1s_" + serialSlug + "_printer_name";
  String result, err;
  // Serial is restricted to alphanumeric/_/- by officeBambuSave().
  const String tmpl = "{{ states('" + entity + "') }}";
  if (!haRenderTemplate(tmpl, result, err)) return;
  result.trim();
  if (result.length() > 63 || result == "unknown" ||
      result == "unavailable" || result == "none") result = "";
  s_liveHaName = result;
}

bool officeBambuEnabled() { return s_enabled; }

String officeBambuLiveName() { return s_liveHaName; }

String officeBambuDisplayName() {
  // Deck pages may be built before officeBambuBegin() loads NVS.
  Preferences pref;
  if (pref.begin(NVS, true)) {
    String name = pref.getString("display_name", "");
    pref.end();
    name.trim();
    return name;
  }
  return s_displayName;
}

bool officeBambuConfigured() {
  // The UI is created before officeBambuBegin() brings up the connection.
  // Read the stored settings instead of relying on s_enabled at boot.
  Preferences pref;
  if (!pref.begin(NVS, true)) return false;
  const bool enabled = pref.getBool("enabled", false);
  const bool ha = pref.getBool("ha", false);
  const bool hasSerial = pref.getString("serial", "").length() > 0;
  const bool hasAddress = pref.getString("address", "").length() > 0;
  const bool hasCode = pref.getString("code", "").length() > 0;
  pref.end();
  return enabled && hasSerial && (ha || (hasAddress && hasCode));
}

bool officeBambuSave(bool enabled, const String& source, const String& address,
                     const String& serial, const String& code, const String& displayName, String& error) {
  const bool ha = source == "ha";
  if (source != "ha" && source != "lan") {
    error = "source must be lan or ha";
    return false;
  }
  if (!validSecret(serial, 80) || !validSecret(code, 80) ||
      (address.length() && !validAddress(address))) {
    error = "invalid printer address or credentials";
    return false;
  }
  String nextName = displayName;
  nextName.trim();
  if (nextName.length() > 48 || nextName.indexOf('\n') >= 0 ||
      nextName.indexOf('\r') >= 0) {
    error = "invalid printer display name";
    return false;
  }
  String nextSerial = serial.length() ? serial : s_serial;
  String nextCode = code.length() ? code : s_code;
  String nextAddress = address.length() ? address : s_address;
  if (enabled && (!nextSerial.length() || (!ha && (!nextAddress.length() || !nextCode.length())))) {
    error = ha ? "Home Assistant requires the printer serial" :
                 "LAN needs the address, serial and access code";
    return false;
  }
  Preferences pref;
  if (!pref.begin(NVS, false)) {
    error = "could not open printer settings";
    return false;
  }
  bool ok = pref.putBool("enabled", enabled) > 0;
  ok = (pref.putBool("ha", ha) > 0) && ok;
  ok = (pref.putString("address", nextAddress) > 0 || nextAddress.isEmpty()) && ok;
  ok = (pref.putString("serial", nextSerial) > 0 || nextSerial.isEmpty()) && ok;
  ok = (pref.putString("code", nextCode) > 0 || nextCode.isEmpty()) && ok;
  ok = (pref.putString("display_name", nextName) > 0 || nextName.isEmpty()) && ok;
  pref.end();
  if (!ok) { error = "could not save printer settings"; return false; }
  const bool wasEnabled = s_enabled;
  s_enabled = enabled;
  s_ha = ha;
  s_address = nextAddress;
  s_serial = nextSerial;
  s_code = nextCode;
  s_displayName = nextName;
  s_liveHaName = "";
  s_lastNameLookup = 0;
  apply();
  if (enabled) {
    String layoutError;
    if (!deckEnsureBambuStarterPages(layoutError))
      Serial.printf("Bambu: could not persist starter pages: %s\n", layoutError.c_str());
  }
  // Refresh after any enabled save as well: first-time template storage and
  // source/serial edits should appear immediately without a reboot.
  if (wasEnabled != enabled || enabled) deckRefreshPrinterPage();
  return true;
}

bool officeBambuLight(bool on) {
  return s_enabled && printerLight(ID, on);
}

String officeBambuJson() {
  // IMPORTANT: JsonDocument may retain raw char* references instead of copying
  // them. PrinterState is deleted below, before the document is serialized.
  // Always wrap its char arrays in String so ArduinoJson owns their contents.
  JsonDocument doc(psramJson());
  doc["enabled"] = s_enabled;
  doc["source"] = s_ha ? "ha" : "lan";
  doc["address"] = s_address;
  doc["display_name"] = s_displayName;
  doc["serial_set"] = s_serial.length() > 0;
  doc["access_code_set"] = s_code.length() > 0;
  doc["version"] = printersVersion();
  if (!s_enabled) {
    doc["status"] = "disabled";
  } else {
    // This is a large structure (six AMS units); keep it off the loop stack.
    PrinterState* st = new PrinterState();
    if (!st) doc["status"] = "out of memory";
    else {
      if (printerRead(ID, *st)) {
        doc["status"] = st->link == PRN_ONLINE ? "online" :
                        st->link == PRN_CONNECTING ? "connecting" :
                        st->link == PRN_FAILED ? "failed" : "off";
        doc["error"] = String(st->err);
        doc["state"] = String(st->gcodeState);
        doc["job"] = String(st->job);
        doc["progress"] = st->progress;
        doc["remaining_minutes"] = st->remainingMin;
        doc["layer"] = st->layer;
        doc["total_layers"] = st->totalLayers;
        if (isfinite(st->nozzle[0])) doc["nozzle"] = st->nozzle[0];
        if (isfinite(st->nozzleTarget[0])) doc["nozzle_target"] = st->nozzleTarget[0];
        if (isfinite(st->bed)) doc["bed"] = st->bed;
        if (isfinite(st->bedTarget)) doc["bed_target"] = st->bedTarget;
        if (isfinite(st->chamber)) doc["chamber"] = st->chamber;
        doc["light"] = st->light;
        doc["door"] = st->door;
        doc["ams_count"] = st->amsCount;
        JsonArray ams = doc["ams"].to<JsonArray>();
        for (unsigned i = 0; i < st->amsCount && i < PRN_AMS_UNITS; ++i) {
          const PrinterAmsUnit& u = st->ams[i];
          if (!u.present) continue;
          JsonObject obj = ams.add<JsonObject>();
          obj["unit"] = i + 1;
          obj["humidity"] = u.humidity == 255 ? -1 : (int)u.humidity;
          if (isfinite(u.temp)) obj["temperature"] = u.temp;
          obj["dry_minutes"] = u.dryRemainMin;
          JsonArray slots = obj["slots"].to<JsonArray>();
          for (unsigned j = 0; j < u.slots && j < PRN_AMS_SLOTS; ++j) {
            const PrinterTray& tray = u.tray[j];
            JsonObject v = slots.add<JsonObject>();
            v["slot"] = j + 1;
            v["present"] = tray.present;
            if (tray.present) {
              v["type"] = String(tray.type);
              char rgb[8];
              snprintf(rgb, sizeof(rgb), "#%06lX", (unsigned long)tray.rgb);
              v["colour"] = String(rgb);
              v["remaining_percent"] = tray.remain;
            }
          }
        }
      } else doc["status"] = "not configured";
      delete st;
    }
  }
  if (doc.overflowed()) {
    Serial.println("Bambu: status JSON overflow (PSRAM) — response omitted");
    return R"json({"status":"unavailable","error":"Could not allocate status JSON"})json";
  }
  String out;
  serializeJson(doc, out);
  return out;
}
}  // namespace deck
