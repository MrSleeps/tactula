// TouchDeck - firmware entry point.
// Boot: settings -> display (splash) -> WiFi (STA or AP setup) -> web/OTA
// server -> Home Assistant-powered deck pages. No PC companion is needed.
#include "debug_log.h"
#include "p4_trace.h"
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "config.h"
#include "display_ui.h"
#include "tactula_settings.h"
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
#include "tls_psram.h"
#include <esp_ota_ops.h>
#if defined(BOARD_IS_JC8012P4A1C)
#include "p4_board.h"
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#endif
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
#if defined(BOARD_IS_JC8012P4A1C)
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

struct ScanoutSample {
  uint32_t atMs, frames, underruns;
};
static ScanoutSample sampleScanout() {
  return {millis(), p4Board::scanoutFrames(), p4Board::scanoutUnderruns()};
}
static void logScanout(const char* phase, const ScanoutSample& before) {
  if (p4Board::liveReloadActive()) {
    debugSerial.printf("P4 scanout %s: hardware reload active; IRQ frame counter unavailable\n", phase);
    return;
  }
  const ScanoutSample after = sampleScanout();
  debugSerial.printf("P4 scanout %s: %lu frames in %lu ms, %lu underruns\n", phase,
                     (unsigned long)(after.frames - before.frames),
                     (unsigned long)(after.atMs - before.atMs),
                     (unsigned long)(after.underruns - before.underruns));
}
static ScanoutSample sDeckHandoff = {};
static bool sDeckHandoffPending = false;
#else
SET_LOOP_TASK_STACK_SIZE(12 * 1024);
#endif

// Allocation callback runs under heap pressure: no logging, heap queries or allocation.
static std::atomic<uint32_t> sHeapFailures{0};
static std::atomic<uint32_t> sHeapFailedSize{0};
static std::atomic<uint32_t> sHeapFailedCaps{0};

static void heapAllocationFailed(size_t size, uint32_t caps, const char*) {
  sHeapFailedSize.store(static_cast<uint32_t>(size), std::memory_order_relaxed);
  sHeapFailedCaps.store(caps, std::memory_order_relaxed);
  sHeapFailures.fetch_add(1, std::memory_order_relaxed);
}

static void logHeap(const char* phase) {
  constexpr uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  debugSerial.printf("Heap %s: internal free=%u min=%u largest=%u; PSRAM free=%u min=%u largest=%u; failed attempts=%u last_size=%u last_caps=0x%x; loop_stack_hwm=%u\n",
      phase,
      (unsigned)heap_caps_get_free_size(internal),
      (unsigned)heap_caps_get_minimum_free_size(internal),
      (unsigned)heap_caps_get_largest_free_block(internal),
      (unsigned)heap_caps_get_free_size(external),
      (unsigned)heap_caps_get_minimum_free_size(external),
      (unsigned)heap_caps_get_largest_free_block(external),
      (unsigned)sHeapFailures.load(std::memory_order_relaxed),
      (unsigned)sHeapFailedSize.load(std::memory_order_relaxed),
      (unsigned)sHeapFailedCaps.load(std::memory_order_relaxed),
      (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}

void setup() {
#if defined(BOARD_IS_JC8012P4A1C)
  p4Board::prepareBacklight();
#endif
  Serial.begin(115200);
#if defined(BOARD_IS_JC8012P4A1C)
  usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  usb.tx_buffer_size = 2048;
  if (usb_serial_jtag_driver_install(&usb) == ESP_OK) usb_serial_jtag_vfs_use_driver();
  p4Board::begin();
#endif
  debugLogBegin();
  delay(200);
  debugSerial.println();
  debugSerial.println(PRODUCT_NAME " " FW_VERSION " boot");
#if defined(BOARD_IS_JC8012P4A1C)
  debugSerial.println("P4 wallpaper: compressed JPEG staging");
#endif
  debugSerial.printf("Reset reason: %d\n", (int)esp_reset_reason());
  crashLogBegin();
  // Keep scarce internal RAM for DMA and WiFi: small objects alone stay internal.
  heap_caps_malloc_extmem_enable(128);
  if (heap_caps_register_failed_alloc_callback(heapAllocationFailed) != ESP_OK)
    debugSerial.println("Heap: failed to register allocation callback");
  if (debugLogActive()) logHeap("boot");

  // Configure TLS before any network task can start in either edition.
  if (!tlsPsramInstall()) debugSerial.println("TLS: could not install PSRAM allocator");

  loadSettings();
  firmwareUpdate::begin();
  const esp_partition_t* runningOta = esp_ota_get_running_partition();
  esp_ota_img_states_t runningState;
  if (runningOta &&
      esp_ota_get_state_partition(runningOta, &runningState) == ESP_OK &&
      runningState == ESP_OTA_IMG_PENDING_VERIFY) {
    sOtaBootValidationPending = true;
    debugSerial.println("OTA: validating newly booted image");
  }
#if !defined(BOARD_IS_JC8012P4A1C)
  deck::wallStorageBegin();
#endif
  setScreenState(SCREEN_SPLASH);
  initDisplay();

#if defined(BOARD_IS_JC8012P4A1C)
  p4Board::enableRadio();
#endif
  startWiFiDuringSplash();        // kick off a background STA connect
  unsigned long splashStart = millis();
  while (millis() - splashStart < 800) {
    displayTick();
    delay(20);
  }

  initWiFi();                     // blocks until STA connects or AP comes up
#if defined(BOARD_IS_JC8012P4A1C)
  const ScanoutSample beforeServices = sampleScanout();
#endif
  initWebServer();
  initLed();
  initHaptic();
  audioInit();
  mqttBegin();
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  deck::officeBambuBegin();
#endif

  if (isWiFiConnected()) {
#if defined(BOARD_IS_JC8012P4A1C)
    logScanout("connected screen / service startup", beforeServices);
    sDeckHandoff = sampleScanout();
    sDeckHandoffPending = true;
#endif
    setScreenState(SCREEN_DECK);
  }

  // Subscribe only after the blocking boot phases; loop() feeds it every pass.
  // The Arduino core usually arrives with the TWDT already initialized, so
  // fall back to reconfigure when init reports that state.
  const esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_err_t wdt = esp_task_wdt_reconfigure(&wdtConfig);
  if (wdt == ESP_ERR_INVALID_STATE) wdt = esp_task_wdt_init(&wdtConfig);
  if (wdt == ESP_OK && esp_task_wdt_status(nullptr) == ESP_ERR_NOT_FOUND)
    wdt = esp_task_wdt_add(nullptr);
  if (wdt != ESP_OK)
    debugSerial.printf("Watchdog setup failed: %s\n", esp_err_to_name(wdt));
}

void loop() {
  esp_task_wdt_reset();
  static uint32_t lastHeapLog = 0;
  if (debugLogActive() && millis() - lastHeapLog >= 60000) {
    lastHeapLog = millis();
    logHeap("runtime");
  }
  p4Trace::run(p4Trace::Stage::Wifi, [] { handleWiFi(); });
  p4Trace::run(p4Trace::Stage::Web, [] { handleWebServer(); });
  p4Trace::run(p4Trace::Stage::Ota, [] { firmwareUpdate::tick(); });

  // Finish boot/connect transitions.
  ScreenState s = getScreenState();
  if (isWiFiConnected() &&
      (s == SCREEN_SPLASH || s == SCREEN_CONNECTING_WIFI ||
       s == SCREEN_WIFI_CONNECTED)) {
#if defined(BOARD_IS_JC8012P4A1C)
    sDeckHandoff = sampleScanout();
    sDeckHandoffPending = true;
#endif
    setScreenState(SCREEN_DECK);
  }

  p4Trace::run(p4Trace::Stage::Backlight, [] { updateBacklightControl(); });
  // After the backlight: the LED reads the level the panel just settled on.
  ledTick();
  displayTick();
#if defined(BOARD_IS_JC8012P4A1C)
  // Only clear the persistent provisional flag after the web/application
  // loop has survived startup and at least one image was presented.
  if (millis() >= 60000) p4Board::confirmDmaBoot();
  // The panel task presents the first dashboard frame asynchronously.
  if (sDeckHandoffPending && millis() - sDeckHandoff.atMs >= 250) {
    sDeckHandoffPending = false;
    logScanout("first dashboard frames", sDeckHandoff);
  }
#endif
  p4Trace::run(p4Trace::Stage::Daily, [] { deck::dailyPoll(); });
  // If this bootloader supports OTA rollback, keep the just-booted image
  // provisional until the normal application loop remains healthy for 60s.
  if (sOtaBootValidationPending && millis() >= 60000) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      debugSerial.println("OTA: new firmware verified after healthy runtime");
      sOtaBootValidationPending = false;
    }
  }
#if defined(TOUCHDECK_FEATURE_BAMBU) && TOUCHDECK_FEATURE_BAMBU
  p4Trace::run(p4Trace::Stage::PrinterService, [] { deck::officeBambuTick(); });
#endif
}
