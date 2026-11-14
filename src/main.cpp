// TouchDeck - firmware entry point.
// Boot: settings -> display (splash) -> WiFi (STA or AP setup) -> web/OTA
// server -> Home Assistant-powered deck pages. No PC companion is needed.
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "display_ui.h"
#include "settings.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "led.h"
#include "haptic.h"
#include "audio.h"
#include "crash_log.h"
#include "deck_mqtt.h"
#include "deck_daily.h"
#include "deck_sd.h"
#include "deck_updates.h"
#include <esp_ota_ops.h>
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
#include "deck_office_bambu.h"
#endif

// Loop-task watchdog: an indefinite block in loop() panics after
// WDT_TIMEOUT_S with a backtrace and reboots (/api/status then reports
// reset_reason "task_wdt"). Every legitimate blocking path finishes far
// below this bound.
static const uint32_t WDT_TIMEOUT_S = 30;
static bool sOtaBootValidationPending = false;

// LVGL rendering and the Glass bake left under 1 KB of the default 8 KB.
SET_LOOP_TASK_STACK_SIZE(12 * 1024);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(PRODUCT_NAME " " FW_VERSION " boot");
  Serial.printf("Reset reason: %d\n", (int)esp_reset_reason());
  crashLogBegin();
  // Keep scarce internal RAM for DMA and WiFi: small objects alone stay internal.
  heap_caps_malloc_extmem_enable(128);

  loadSettings();
  firmwareUpdate::begin();
  const esp_partition_t* runningOta = esp_ota_get_running_partition();
  esp_ota_img_states_t runningState;
  if (runningOta &&
      esp_ota_get_state_partition(runningOta, &runningState) == ESP_OK &&
      runningState == ESP_OTA_IMG_PENDING_VERIFY) {
    sOtaBootValidationPending = true;
    Serial.println("OTA: validating newly booted image");
  }
  deck::wallStorageBegin();
  setScreenState(SCREEN_SPLASH);
  initDisplay();

  startWiFiDuringSplash();        // kick off a background STA connect
  unsigned long splashStart = millis();
  while (millis() - splashStart < 800) {
    displayTick();
    delay(20);
  }

  initWiFi();                     // blocks until STA connects or AP comes up
  initWebServer();
  initLed();
  initHaptic();
  audioInit();
  mqttBegin();
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  deck::officeBambuBegin();
#endif

  if (isWiFiConnected()) setScreenState(SCREEN_DECK);

  // Subscribe only after the blocking boot phases; loop() feeds it every pass.
  // The Arduino core usually arrives with the TWDT already initialized, so
  // fall back to reconfigure when init reports that state.
  const esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  if (esp_task_wdt_init(&wdtConfig) != ESP_OK)
    esp_task_wdt_reconfigure(&wdtConfig);
  esp_task_wdt_add(NULL);
}

void loop() {
  esp_task_wdt_reset();
  handleWiFi();
  handleWebServer();
  firmwareUpdate::tick();

  // Finish boot/connect transitions.
  ScreenState s = getScreenState();
  if (isWiFiConnected() &&
      (s == SCREEN_SPLASH || s == SCREEN_CONNECTING_WIFI ||
       s == SCREEN_WIFI_CONNECTED)) {
    setScreenState(SCREEN_DECK);
  }

  updateBacklightControl();
  // After the backlight: the LED reads the level the panel just settled on.
  ledTick();
  displayTick();
  deck::dailyPoll();
  // If this bootloader supports OTA rollback, keep the just-booted image
  // provisional until the normal application loop remains healthy for 60s.
  if (sOtaBootValidationPending && millis() >= 60000) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      Serial.println("OTA: new firmware verified after healthy runtime");
      sOtaBootValidationPending = false;
    }
  }
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  deck::officeBambuTick();
#endif
}
