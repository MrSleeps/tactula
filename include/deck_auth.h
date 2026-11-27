#pragma once

#include <Arduino.h>

// Optional HTTP Basic Auth for the local device portal.
// Username is permanently "admin"; no sessions or extra dependencies.
namespace deckAuth {
  void begin();
  bool enabled();
  bool verifyBasicHeader(const String& authorization);
  bool setPassword(const String& password);
  bool disable();
  // Separate local touchscreen PIN; never returned by a portal API.
  enum class PinResult { Accepted, Incorrect, Locked };
  bool devicePinEnabled();
  bool validDevicePin(const char* pin);
  bool setDevicePin(const char* pin);
  bool clearDevicePin();
  PinResult verifyDevicePin(const char* pin);
  bool widgetPinEnabled();
  bool setWidgetPin(const char* pin);
  bool clearWidgetPin();
  PinResult verifyWidgetPin(const char* pin);
  uint32_t widgetPinRevision();
}
