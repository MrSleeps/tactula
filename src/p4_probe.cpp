// JC8012P4A1C first-stage bring-up (ESP32-P4).
// Intentionally isolated from the ESP32-S3 Tactula runtime.
// This checks the processor, flash, PSRAM and USB serial before we enable
// the MIPI-DSI screen, touch or the ESP32-C6 host connection.
#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

static void report() {
  esp_chip_info_t chip{};
  esp_chip_info(&chip);
  Serial.println();
  Serial.println("=== Tactula / Guition JC8012P4A1C P4 probe ===");
  Serial.printf("Chip: ESP32-P4 revision %d, cores %d\n",
                chip.revision, chip.cores);
  Serial.printf("CPU frequency: %lu MHz\n", (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("Flash: %lu bytes\n", (unsigned long)ESP.getFlashChipSize());
  Serial.printf("PSRAM detected: %s\n", psramFound() ? "yes" : "NO");
  Serial.printf("PSRAM total: %lu bytes\n", (unsigned long)ESP.getPsramSize());
  Serial.printf("PSRAM free: %lu bytes\n",
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  Serial.printf("Internal free: %lu bytes\n",
                (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.printf("Reset reason: %d\n", (int)esp_reset_reason());
  Serial.println("Display/touch/Wi-Fi are NOT enabled in this probe.");
  Serial.println("=============================================");
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  report();
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 5000) {
    last = millis();
    Serial.printf("P4 alive: %lu ms, free PSRAM %lu bytes\n",
                  (unsigned long)last,
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  }
  delay(50);
}
