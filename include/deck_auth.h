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
}
