#pragma once

#include <stddef.h>
#include <stdint.h>
#include <Print.h>

void debugLogBegin();
bool debugLogRequested();
bool debugLogActive();
bool debugLogSave(bool enabled);
void debugLogNoteHttpAction(const char* method, int code);

// Copy up to capacity bytes after the reader's cursor. Use zero to start
// with the oldest retained bytes. Each reader has its own cursor.
size_t debugLogRead(uint32_t& cursor, char* data, size_t capacity);

class DebugSerial : public Print {
 public:
  size_t write(uint8_t value) override;
  size_t write(const uint8_t* data, size_t length) override;
};

extern DebugSerial debugSerial;
