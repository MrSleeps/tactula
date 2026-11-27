#include "debug_log.h"
#if defined(BOARD_IS_JC8012P4A1C)
#include <driver/usb_serial_jtag.h>
#endif

#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <atomic>
#include <cstdarg>
#include <cstring>
#include "config.h"

namespace {
constexpr char kSetting[] = "dbglog";
constexpr size_t kCapacity = 16 * 1024;
char* buffer;
bool requested;
std::atomic<bool> active{false};
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t written;
size_t retained;
std::atomic<vprintf_like_t> previousLog{nullptr};

void append(const uint8_t* data, size_t length) {
  if (!length || !debugLogActive() || xPortInIsrContext()) return;
  if (length > kCapacity) {
    data += length - kCapacity;
    length = kCapacity;
  }
  portENTER_CRITICAL(&mux);
  const size_t offset = written % kCapacity;
  const size_t first = length < kCapacity - offset ? length : kCapacity - offset;
  memcpy(buffer + offset, data, first);
  memcpy(buffer, data + first, length - first);
  written += length;
  retained += length;
  if (retained > kCapacity) retained = kCapacity;
  portEXIT_CRITICAL(&mux);
}

void capturePrintf(const char* format, va_list args) {
  if (!debugLogActive() || xPortInIsrContext()) return;
  char message[512];
  va_list copy;
  va_copy(copy, args);
  const int size = vsnprintf(message, sizeof(message), format, copy);
  va_end(copy);
  if (size <= 0) return;
  size_t length = size;
  if (length >= sizeof(message)) {
    length = sizeof(message) - 1;
    message[length - 1] = '\n';
  }
  append(reinterpret_cast<const uint8_t*>(message), length);
}

int captureEspLog(const char* format, va_list args) {
  capturePrintf(format, args);
  const auto output = previousLog.load(std::memory_order_acquire);
  return output ? output(format, args) : vprintf(format, args);
}
}  // namespace

DebugSerial debugSerial;

size_t DebugSerial::write(uint8_t value) {
  return write(&value, 1);
}

size_t DebugSerial::write(const uint8_t* data, size_t length) {
  append(data, length);
#if defined(BOARD_IS_JC8012P4A1C)
  const int written = usb_serial_jtag_write_bytes(data, length, pdMS_TO_TICKS(10));
  return written > 0 ? static_cast<size_t>(written) : 0;
#else
  return Serial.write(data, length);
#endif
}

// Arduino core logs use log_printf rather than the ESP-IDF callback.
extern "C" int log_printfv(const char* format, va_list args);
extern "C" int __wrap_log_printf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  capturePrintf(format, args);
  const int result = log_printfv(format, args);
  va_end(args);
  return result;
}

void debugLogBegin() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) return;
  requested = prefs.isKey(kSetting) && prefs.getBool(kSetting, false);
  prefs.end();
  if (!requested) return;
  buffer = static_cast<char*>(heap_caps_malloc(kCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buffer) return;
  active.store(true, std::memory_order_release);
  previousLog.store(esp_log_set_vprintf(captureEspLog), std::memory_order_release);
}

bool debugLogRequested() { return requested; }
bool debugLogActive() { return active.load(std::memory_order_acquire); }

bool debugLogSave(bool enabled) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) return false;
  const bool saved = prefs.putBool(kSetting, enabled) != 0;
  prefs.end();
  return saved;
}

void debugLogNoteHttpAction(const char* method, int code) {
  if (!debugLogActive()) return;
  char message[64];
  const int count = snprintf(message, sizeof(message), "[http] %.16s action -> %d\n", method, code);
  if (count > 0) append(reinterpret_cast<const uint8_t*>(message), count);
}

size_t debugLogRead(uint32_t& cursor, char* data, size_t capacity) {
  if (!debugLogActive()) return 0;
  portENTER_CRITICAL(&mux);
  const uint32_t distance = written - cursor;
  const size_t available = distance < retained ? distance : retained;
  const uint32_t start = written - available;
  const size_t count = available < capacity ? available : capacity;
  const size_t offset = start % kCapacity;
  const size_t first = count < kCapacity - offset ? count : kCapacity - offset;
  memcpy(data, buffer + offset, first);
  memcpy(data + first, buffer, count - first);
  cursor = start + count;
  portEXIT_CRITICAL(&mux);
  return count;
}
