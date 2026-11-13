#pragma once
#include <Arduino.h>

namespace deck {
void officeBambuBegin();
void officeBambuTick();
String officeBambuJson();
bool officeBambuSave(bool enabled, const String& source, const String& address,
                     const String& serial, const String& code, const String& displayName, String& error);
bool officeBambuLight(bool on);
bool officeBambuEnabled();
// Human-readable printer name, stored separately from the editable page layout.
String officeBambuDisplayName();
// Cached Home Assistant name for LAN-connected printers, when available.
String officeBambuLiveName();
// True only after an enabled printer has complete saved credentials.
// Reads NVS directly: safe to call while the deck loads before the Bambu
// network controller starts (and on reboot).
bool officeBambuConfigured();
}
