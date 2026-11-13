#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
//  Firmware version
// =============================================================================
#define FW_VERSION          "1.0.2"
#define PRODUCT_NAME        "tactula"

// Board variant - reported to the portal and the OTA page.
#ifndef BOARD_VARIANT
#define BOARD_VARIANT       "jc3248w535"
#endif

// =============================================================================
//  WiFi
// =============================================================================
#define WIFI_AP_PREFIX      "tactula-"
#define WIFI_AP_PASSWORD    "touchdeck1234"
#define WIFI_CONNECT_TIMEOUT 15000  // 15s STA connect timeout
#define WIFI_RECONNECT_TIMEOUT 60000 // 60s before re-entering AP mode
#define WIFI_BACKOFF_PHASE1_MS    10000   // 10s between attempts in phase 1
#define WIFI_BACKOFF_PHASE2_MS    30000   // 30s between attempts after phase 1
#define WIFI_BACKOFF_PHASE3_MS    60000   // 60s between attempts after phase 2
#define WIFI_BACKOFF_PHASE2_START 5       // start phase 2 after this many attempts
#define WIFI_BACKOFF_PHASE3_START 10      // start phase 3 after this many attempts
#define WIFI_STA_PROBE_INTERVAL   120000  // 2 min between STA probes while in AP mode
#define WIFI_STA_PROBE_CHECK_MS    10000  // 10s after probe start before checking result
#define WIFI_AP_FALLBACK_MS       900000  // 15 min in phase 3 before falling back to AP

// Improv WiFi serial setup window on first boot (no stored credentials).
#define IMPROV_SETUP_WINDOW_MS    180000  // 3 min window for Improv setup at first boot

// =============================================================================
//  NVS
// =============================================================================
// The original "bedroomdeck" NVS settings are migrated once on boot.
// The old namespace is retained as a fallback until migration succeeds.
#define NVS_NAMESPACE       "touchdeck"
#define NVS_NAMESPACE_LEGACY "bedroomdeck"

// =============================================================================
//  Gauge display defaults (shared with Home Assistant sensors)
// =============================================================================
#define GAUGE_TEMP_SCALE_DEFAULT   100   // C
#define GAUGE_POWER_SCALE_DEFAULT  300   // W
#define GAUGE_FAN_SCALE_DEFAULT   3000   // RPM and anything without a unit rule

// =============================================================================
//  External LED (optional, PWM dimmable via NPN/MOSFET)
// =============================================================================
#define LED_PWM_FREQ    5000  // PWM frequency (Hz)
#define LED_PWM_RES     8     // PWM resolution (bits) -> 0..255 duty
#define LED_DEFAULT_PIN 17    // extended IO header (meter it before wiring)

// LED effect tuning (ms periods)
#define LED_TICK_MIN_INTERVAL_MS    16   // throttle ledTick() to ~60Hz

#endif // CONFIG_H
