// Guition JC8012P4A1C JD9365 / GSL3680 display-and-touch bring-up.
// Stage 1: deliberately standalone. Once this works, replace only the panel
// backend and keep the existing Tactula widgets, pages, settings and services.
#include <Arduino.h>
#include <lvgl.h>
#include <lv_setup_JC8012P4A1C.hpp>
#if defined(P4_WIFI_SETUP_TEST) && P4_WIFI_SETUP_TEST
#include "p4_wifi_setup.h"
#endif
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_ldo_regulator.h>
#include <esp_err.h>
#include <esp_cache.h>
#if defined(P4_TOUCH_NVS_INSTALL) && P4_TOUCH_NVS_INSTALL
#include <nvs.h>
#include "gsl3680_firmware_blob.h"  // downloaded locally; GPL-2.0; never checked in
#endif
#include <stdarg.h>
#include <stdio.h>

// The full-size Tactula QR screen plus ESP-Hosted DNS/HTTP handlers use
// substantially more loop-task stack than the minimal P4 touch test. The
// default ~8 KB loopTask stack overflowed as LVGL first rendered the QR.
// Match Tactula's explicit loop stack strategy, with headroom for the P4.
#if defined(P4_WIFI_SETUP_TEST) && P4_WIFI_SETUP_TEST
SET_LOOP_TASK_STACK_SIZE(24 * 1024);
#endif

#if defined(P4_STATIC_SCANOUT_TEST) && P4_STATIC_SCANOUT_TEST && \
    defined(P4_LVGL_NATIVE_SCANOUT_TEST) && P4_LVGL_NATIVE_SCANOUT_TEST
#error "Select only one P4 display test mode"
#endif

#if defined(P4_LVGL_NATIVE_SCANOUT_TEST) && P4_LVGL_NATIVE_SCANOUT_TEST
#if defined(P4_PPA_SINGLE_BUFFER_TEST) && P4_PPA_SINGLE_BUFFER_TEST
#error "Select one test mode only"
#endif
static constexpr const char* kP4TestMode = "LVGL_NATIVE_DIRECT_800X1280";
#elif defined(P4_STATIC_SCANOUT_TEST) && P4_STATIC_SCANOUT_TEST
#if defined(P4_PPA_SINGLE_BUFFER_TEST) && P4_PPA_SINGLE_BUFFER_TEST
#error "Select one test mode only"
#endif
static constexpr const char* kP4TestMode = "STATIC_COLOUR_BARS";
#elif defined(P4_PPA_SINGLE_BUFFER_TEST) && P4_PPA_SINGLE_BUFFER_TEST
static constexpr const char* kP4TestMode = "LVGL_PPA_SINGLE_BUFFER_LANDSCAPE";
#else
static constexpr const char* kP4TestMode = "PPA_LANDSCAPE";
#endif

// ESP-IDF logs normally use UART0, not the USB CDC port on this board.
// Forward normal driver messages to the same Serial connection as our
// checkpoints. This is temporary diagnostic instrumentation.
static int usbLogVprintf(const char* fmt, va_list args) {
    char line[384];
    const int needed = vsnprintf(line, sizeof(line), fmt, args);
    if (needed > 0) {
        const size_t length = (size_t)needed < sizeof(line) ? (size_t)needed : sizeof(line) - 1;
        Serial.write((const uint8_t*)line, length);
    }
    return needed;
}


// Checkpoints live in RTC RAM across software resets, allowing a crash
// before USB serial is attached to be diagnosed on the next boot.
static constexpr uint32_t kTraceMagic = 0x50443431; // "PD41"
RTC_NOINIT_ATTR static uint32_t bootMagic;
RTC_NOINIT_ATTR static uint32_t crashBoots;
RTC_NOINIT_ATTR static uint32_t lastStage;
RTC_NOINIT_ATTR static uint32_t requestedRestartMagic;
static constexpr uint32_t kRequestedRestartMagic = 0x52425354; // RBST


static void traceStage(uint32_t stage, const char* description) {
    lastStage = stage;
    Serial.printf("[P4 STAGE %lu] %s (PSRAM free: %lu)\n",
                   (unsigned long)stage, description,
                   (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    Serial.flush();
}



static lv_display_t* lvDisplay = nullptr;
static lv_indev_t* lvInput = nullptr;
static uint8_t* lvDraw0 = nullptr;
static uint8_t* lvDraw1 = nullptr;
static uint8_t nextFramebuffer = 1;  // DSI framebuffer 0 initially scans; render first into inactive FB 1
static lv_obj_t* touchCoordsLabel = nullptr;
static bool touchWasDown = false;
static uint32_t lastTouchLogMs = 0;
static uint32_t touchSampleCount = 0;

static void flushDisplay(lv_display_t* disp, const lv_area_t*, uint8_t* pixels) {
    if (lv_display_flush_is_last(disp)) {
#if defined(P4_LVGL_NATIVE_SCANOUT_TEST) && P4_LVGL_NATIVE_SCANOUT_TEST
        // LVGL rendered into the actual DSI scanout memory. No PPA copy.
        // Direct render mode supplies the full-frame base pointer here.
        const int fb = pixels == lvDraw0 ? 0 : (pixels == lvDraw1 ? 1 : -1);
        if (fb < 0) {
            Serial.println("[P4 NATIVE ERROR] flush buffer doesn't match a scanout framebuffer");
        } else {
            const esp_err_t err = esp_cache_msync(
                pixels, display.framebufferSize(), ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            if (err == ESP_OK) {
                display.setActiveFramebuffer(static_cast<uint8_t>(fb), true);
            } else {
                Serial.printf("[P4 NATIVE ERROR] cache flush: %s\n", esp_err_to_name(err));
            }
        }
#else
#if defined(P4_PPA_SINGLE_BUFFER_TEST) && P4_PPA_SINGLE_BUFFER_TEST
        // Keep one coherent LVGL source framebuffer, but alternate the two
        // native DSI scanout targets. Never DMA into the buffer currently
        // being scanned; wait until PPA and the vsync swap complete before
        // returning this source buffer to LVGL for another partial redraw.
        if (pixels != lvDraw0) {
            Serial.println("[P4 PPA ERROR] unexpected draw buffer");
        } else {
            const uint8_t target = nextFramebuffer;
            display.flip(0, target);
            if (display.waitFlipComplete(500)) {
                nextFramebuffer = target ^ 1;
            } else {
                Serial.println("[P4 PPA ERROR] rotation or vsync did not complete");
            }
        }
#else
        const uint8_t drawIndex = (pixels == lvDraw0) ? 0 : 1;
        display.flip(drawIndex, nextFramebuffer);
        nextFramebuffer ^= 1;
#endif
#endif
    }
    lv_display_flush_ready(disp);
}

static void readTouch(lv_indev_t*, lv_indev_data_t* data) {
    uint16_t nativeX = 0, nativeY = 0;
    uint8_t fingers = 0;
    bool down = display.touchAvailable() &&
        display.getTouchDriver().getTouch(&nativeX, &nativeY, &fingers);
    int32_t mappedX = 0, mappedY = 0;
    if (down) {
        down = display.mapTouch(nativeX, nativeY, mappedX, mappedY);
    }
    if (!down) {
        data->state = LV_INDEV_STATE_RELEASED;
        if (touchWasDown) {
            Serial.println("[P4 TOUCH] released");
            touchWasDown = false;
        }
        return;
    }

    data->point.x = mappedX;
    data->point.y = mappedY;
    data->state = LV_INDEV_STATE_PRESSED;
    touchSampleCount++;

    const uint32_t now = millis();
    if (!touchWasDown || now - lastTouchLogMs >= 250) {
        Serial.printf("[P4 TOUCH] native=(%u,%u) mapped=(%ld,%ld) fingers=%u samples=%lu\n",
            (unsigned)nativeX, (unsigned)nativeY,
            (long)mappedX, (long)mappedY,
            (unsigned)fingers, (unsigned long)touchSampleCount);
        if (touchCoordsLabel) {
            lv_label_set_text_fmt(touchCoordsLabel,
                "Touch: native %u,%u   landscape %ld,%ld   fingers %u",
                (unsigned)nativeX, (unsigned)nativeY,
                (long)mappedX, (long)mappedY, (unsigned)fingers);
        }
        lastTouchLogMs = now;
    }
    touchWasDown = true;
}

static void initialiseLvgl() {
    lv_init();
    traceStage(31, "lv_init returned");
#if defined(P4_LVGL_NATIVE_SCANOUT_TEST) && P4_LVGL_NATIVE_SCANOUT_TEST
    lvDraw0 = reinterpret_cast<uint8_t*>(display.getFramebuffer(0));
    lvDraw1 = reinterpret_cast<uint8_t*>(display.getFramebuffer(1));
    Serial.println("[P4 NATIVE] LVGL uses native DSI framebuffers; PPA disabled");
#else
    lvDraw0 = reinterpret_cast<uint8_t*>(display.getDrawBuffer(0));
    lvDraw1 = reinterpret_cast<uint8_t*>(display.getDrawBuffer(1));
#endif
#if defined(P4_PPA_SINGLE_BUFFER_TEST) && P4_PPA_SINGLE_BUFFER_TEST
    if (!lvDraw0) {
#else
    if (!lvDraw0 || !lvDraw1) {
#endif
        Serial.println("[P4 ERROR] Display draw buffers are null");
        while (true) delay(1000);
    }
    lvDisplay = lv_display_create(display.width(), display.height());
    traceStage(32, "lv_display_create returned");
    lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(lvDisplay, flushDisplay);
#if defined(P4_PPA_SINGLE_BUFFER_TEST) && P4_PPA_SINGLE_BUFFER_TEST
    // Single full-frame source: LVGL dirty areas do not leave the inactive
    // buffer with old content, and PPA never switches scanout framebuffers.
    lv_display_set_buffers(lvDisplay, lvDraw0, nullptr, display.framebufferSize(),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    Serial.println("[P4 PPA] Single LVGL source; alternating DSI targets; PPA completion + vsync sync");
#else
    lv_display_set_buffers(lvDisplay, lvDraw0, lvDraw1, display.framebufferSize(),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
#endif
    traceStage(33, "LVGL display buffers attached");
    lvInput = lv_indev_create();
    lv_indev_set_type(lvInput, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvInput, readTouch);
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    traceStage(34, "LVGL input/tick attached");
}


#if defined(P4_STATIC_SCANOUT_TEST) && P4_STATIC_SCANOUT_TEST
// Bypass LVGL, draw buffers, and PPA entirely. The native scan-out buffer
// is 800x1280 RGB565, even though the normal Tactula UI is landscape.
static void runStaticScanoutTest() {
    static constexpr uint16_t colors[] = {
        0xFFFF, // white
        0xF800, // red
        0x07E0, // green
        0x001F, // blue
        0x8410, // mid grey
        0x0000, // black
        0xFFFF, // white
        0x0000  // black
    };
    const size_t bytes = 800u * 1280u * sizeof(uint16_t);
    const int fbCount = display.getNumFramebuffers();
    Serial.printf("[P4 STATIC] Native 800x1280 RGB565; framebuffers=%d, bytes=%lu\n",
                  fbCount, (unsigned long)bytes);
    for (int fb = 0; fb < fbCount; ++fb) {
        uint16_t* pixels = display.getFramebuffer(fb);
        if (!pixels) {
            Serial.printf("[P4 STATIC ERROR] framebuffer %d is null\n", fb);
            while (true) delay(1000);
        }
        // Eight full-width horizontal bands in native portrait coordinates.
        // With the panel physically in landscape, the bands run vertically.
        for (int y = 0; y < 1280; ++y) {
            const uint16_t colour = colors[y / 160];
            uint16_t* row = pixels + (size_t)y * 800u;
            for (int x = 0; x < 800; ++x) row[x] = colour;
        }
        esp_err_t err = esp_cache_msync(pixels, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        Serial.printf("[P4 STATIC] framebuffer %d at %p cache sync=%s\n",
                      fb, (void*)pixels, esp_err_to_name(err));
        Serial.flush();
        if (err != ESP_OK) {
            Serial.println("[P4 STATIC ERROR] cache sync failed; stopping");
            while (true) delay(1000);
        }
    }
    display.setActiveFramebuffer(0, false);
    Serial.println("[P4 STATIC] Direct scan-out active. No LVGL/PPA rendering.");
    Serial.flush();
}
#endif

#if defined(P4_TOUCH_NVS_INSTALL) && P4_TOUCH_NVS_INSTALL
// One-time explicit touch firmware install. Only the named NVS key is touched;
// neither the ESP32-C6, partitions, nor the bootloader are modified.
// This build embeds Silead's GPL-2.0 vendor firmware and is not distributable
// as an ordinary Tactula release. The preparation script obtains it separately.
static void installTouchFirmwareIfMissing() {
    const gsl3680_touch::Status before = display.getTouchDriver().status();
    if (before == gsl3680_touch::OK) {
        Serial.println("[P4 TOUCH INSTALL] Already working; no NVS write");
        return;
    }
    if (before != gsl3680_touch::NO_FIRMWARE &&
        before != gsl3680_touch::BAD_FIRMWARE) {
        Serial.printf("[P4 TOUCH INSTALL] Refusing NVS write: controller status=%s\n",
                      gsl3680_touch::statusText(before));
        return;
    }
    static_assert(sizeof(GSL3680_BLOB) == 9878, "Unexpected GSL3680 firmware blob size");
    static_assert(GSL3680_IMAGE_BYTES == 18348, "Unexpected GSL3680 decompressed size");
    Serial.println("[P4 TOUCH INSTALL] Installing compressed GSL3680 firmware into jc8012/gsl3680fw");
    Serial.flush();
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(gsl3680_touch::NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        Serial.printf("[P4 TOUCH INSTALL] nvs_open failed: %s\n", esp_err_to_name(result));
        return;
    }
    result = nvs_set_blob(handle, gsl3680_touch::NVS_KEY, GSL3680_BLOB, sizeof(GSL3680_BLOB));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK) {
        Serial.printf("[P4 TOUCH INSTALL] NVS write/commit failed: %s\n", esp_err_to_name(result));
        Serial.println("[P4 TOUCH INSTALL] Not erasing NVS. Capture this error.");
        return;
    }
    Serial.println("[P4 TOUCH INSTALL] NVS committed; driver will hash-check and upload firmware");
    Serial.flush();
    if (display.restartTouch()) {
        Serial.println("[P4 TOUCH INSTALL] SUCCESS - touch controller is running");
    } else {
        Serial.printf("[P4 TOUCH INSTALL] Driver rejected firmware or touch start: %s\n",
            gsl3680_touch::statusText(display.getTouchDriver().status()));
    }
    Serial.flush();
}
#endif

static lv_obj_t* statusLabel = nullptr;
static uint32_t presses = 0;
static uint32_t lastHeartbeat = 0;

static void buttonClicked(lv_event_t* event) {
    (void)event;
    presses++;
    if (statusLabel) {
        lv_label_set_text_fmt(statusLabel, "Touch working!  Taps: %lu",
                              (unsigned long)presses);
    }
    Serial.printf("P4 touch button pressed (%lu)\n", (unsigned long)presses);
}

static void createUi() {
    lv_obj_t* root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0C1019), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    lv_obj_t* heading = lv_label_create(root);
    lv_label_set_text(heading, "TACTULA");
    lv_obj_set_style_text_color(heading, lv_color_hex(0x927DFF), 0);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_40, 0);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 85);

    lv_obj_t* subtitle = lv_label_create(root);
#if defined(P4_LVGL_NATIVE_SCANOUT_TEST) && P4_LVGL_NATIVE_SCANOUT_TEST
    lv_label_set_text(subtitle, "ESP32-P4 / JC8012P4A1C\n800 x 1280 - native DSI\nLVGL DIRECT / PPA OFF");
#else
    lv_label_set_text(subtitle, "ESP32-P4  /  JC8012P4A1C\n1280 x 800  -  landscape");
#endif
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xD3DAEA), 0);
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(subtitle, heading, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);

    lv_obj_t* button = lv_button_create(root);
    lv_obj_set_size(button, 440, 130);
    lv_obj_set_style_radius(button, 24, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x604BCC), 0);
    lv_obj_center(button);
    lv_obj_add_event_cb(button, buttonClicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* buttonLabel = lv_label_create(button);
    lv_label_set_text(buttonLabel, "TEST TOUCH");
    lv_obj_center(buttonLabel);

    statusLabel = lv_label_create(root);
    lv_label_set_text(statusLabel, display.touchAvailable()
                          ? "Touch detected - tap the button"
                          : "Touch unavailable - check serial log / touch firmware");
#if defined(P4_LVGL_NATIVE_SCANOUT_TEST) && P4_LVGL_NATIVE_SCANOUT_TEST
    lv_obj_set_width(statusLabel, 740);
#else
    lv_obj_set_width(statusLabel, 1050);
#endif
    lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xD3DAEA), 0);
    lv_obj_align(statusLabel, LV_ALIGN_CENTER, 0, 150);
    touchCoordsLabel = lv_label_create(root);
    lv_obj_set_width(touchCoordsLabel, 1050);
    lv_obj_set_style_text_align(touchCoordsLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(touchCoordsLabel, lv_color_hex(0xA5B6DB), 0);
    lv_label_set_text(touchCoordsLabel, display.touchAvailable()
        ? "Tap and drag to verify orientation and coordinate mapping"
        : "Touch firmware missing: install GSL3680 firmware in NVS first");
    lv_obj_align(touchCoordsLabel, LV_ALIGN_CENTER, 0, 215);

}

void setup() {
    Serial.begin(115200); // USB CDC: same working port as p4_probe.cpp
    delay(1500);
    const esp_reset_reason_t why = esp_reset_reason();
    const bool intentionalRestart = why == ESP_RST_SW &&
        requestedRestartMagic == kRequestedRestartMagic;
    requestedRestartMagic = 0;  // never mask a subsequent unexpected reset
    if (bootMagic != kTraceMagic || why == ESP_RST_POWERON || intentionalRestart) {
        bootMagic = kTraceMagic;
        crashBoots = 0;
        lastStage = 0;
    } else if (why != ESP_RST_UNKNOWN) {
        crashBoots++;
    }
    Serial.printf("\n=== P4 BOOT: reset=%d prior-stage=%lu consecutive-resets=%lu ===\n",
                   (int)why, (unsigned long)lastStage, (unsigned long)crashBoots);
    Serial.println("Tactula P4 stage 1: JD9365 / GSL3680 / LVGL");
    Serial.printf("[P4 FIRMWARE] mode=%s, built=%s %s\n", kP4TestMode, __DATE__, __TIME__);
    Serial.flush();
    esp_log_set_vprintf(usbLogVprintf);
    esp_log_level_set("*", ESP_LOG_INFO);
    Serial.println("[P4] ESP-IDF driver logs redirected to USB CDC");
    Serial.flush();
    // Do not get trapped in a black-screen reset loop. Retain last checkpoint.
    if (crashBoots >= 3) {
        Serial.println("[P4 SAFE MODE] Skipping display init after 3 resets.");
        Serial.println("Power-cycle to retry. Capture prior-stage above.");
        Serial.flush();
        return;
    }
    Serial.println("[P4] 8-second startup window; attach serial monitor now.");
    Serial.flush();
    delay(8000);
    traceStage(10, "before board.begin");
    board.begin();
    traceStage(11, "board.begin returned; testing MIPI PHY LDO");
    // Test the JD9365 driver's first hardware dependency, then release it
    // before display.begin() acquires the regulator for actual use.
    traceStage(12, "before esp_ldo_acquire_channel(3, 2500mV)");
    esp_ldo_channel_handle_t testLdo = nullptr;
    esp_ldo_channel_config_t ldoCfg = {};
    ldoCfg.chan_id = 3;
    ldoCfg.voltage_mv = 2500;
    const esp_err_t ldoResult = esp_ldo_acquire_channel(&ldoCfg, &testLdo);
    Serial.printf("[P4 LDO] acquire result=%s (%d), handle=%p\n",
                  esp_err_to_name(ldoResult), (int)ldoResult, (void*)testLdo);
    Serial.flush();
    traceStage(13, "returned from LDO acquire");
    if (ldoResult != ESP_OK || !testLdo) {
        Serial.println("[P4 ERROR] MIPI PHY LDO preflight failed; skipping display.");
        while (true) delay(1000);
    }
    const esp_err_t ldoRelease = esp_ldo_release_channel(testLdo);
    Serial.printf("[P4 LDO] release result=%s (%d)\n",
                  esp_err_to_name(ldoRelease), (int)ldoRelease);
    Serial.flush();
    if (ldoRelease != ESP_OK) {
        Serial.println("[P4 ERROR] MIPI PHY LDO release failed; skipping display.");
        while (true) delay(1000);
    }
    traceStage(14, "LDO released; before display.begin");
    Serial.printf("PSRAM free before display: %lu bytes\n",
                   (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    // Default Tactula display orientation: landscape, 1280x800.\n    // The panel is natively portrait; PPA rotates the rendered image.
    // Maintain the new-panel 80 MHz clock, but extend blanking to ~59.94 Hz.
    // This keeps extra breathing room for PSRAM scanout versus ~68.7 Hz.
    display.setPanelTiming(JC8012PanelTiming{80000000, 20, 20, 162, 4, 10, 38});
    Serial.println("[P4 PANEL] V3 test: 80MHz DPI / 840Mbps lanes / ~59.94Hz");
    Serial.printf("[P4 TEST MODE] %s\n", kP4TestMode);
    Serial.flush();
#if defined(P4_LVGL_NATIVE_SCANOUT_TEST) && P4_LVGL_NATIVE_SCANOUT_TEST
    // Native portrait: no rotation or PPA. Allocate panel buffers only.
    display.setDrawBuffers(false);
    Serial.println("[P4 NATIVE] Display rotation=0; no PPA draw buffers");
    if (!display.begin(0)) {
#else
    if (!display.begin(3)) {
#endif
        Serial.println("[P4 ERROR] display.begin returned false");
        while (true) delay(1000);
    }
    traceStage(20, "display.begin returned; hardware/PSRAM buffers ready");
#if defined(P4_TOUCH_NVS_INSTALL) && P4_TOUCH_NVS_INSTALL
    installTouchFirmwareIfMissing();
#endif
    Serial.printf("[P4 TOUCH] %s; controller status: %s\n",
        display.touchAvailable() ? "READY" : "DISABLED",
        gsl3680_touch::statusText(display.getTouchDriver().status()));
    Serial.flush();
#if defined(P4_STATIC_SCANOUT_TEST) && P4_STATIC_SCANOUT_TEST
    traceStage(25, "before direct framebuffer colour bars");
    runStaticScanoutTest();
    traceStage(26, "direct scanout running");
    display.setBacklight(100);
    return;
#endif
    traceStage(30, "before LVGL initialization");
    initialiseLvgl();
    traceStage(35, "LVGL setup completed");
    Serial.printf("Display started: %d x %d, touch: %s\n",
                   display.width(), display.height(),
                   display.touchAvailable() ? "YES" : "NO");
    traceStage(30, "before creating test widgets");
    createUi();
    traceStage(40, "widgets created");
#if defined(P4_WIFI_SETUP_TEST) && P4_WIFI_SETUP_TEST
    // Start Wi-Fi only after the proven display/touch pipeline is ready.
    // The C6 may have incompatible or missing ESP-Hosted firmware; failure
    // must leave the display and serial diagnostics usable.
    traceStage(42, "before P4 hosted WiFi init");
    // GPIO54 releases the ESP32-C6 CHIP_PU line; never flash C6 here.
    board.setRadioPower(true);
    delay(200);
    p4WifiBegin();
    traceStage(43, "P4 hosted WiFi init returned");
    p4WifiCreateUi(lv_screen_active());
#endif
    // Solid backlight (no PWM duty cycling) for first visual banding comparison.
    display.setBacklight(100);
    traceStage(50, "backlight enabled; setup complete");
#if defined(P4_WIFI_SETUP_TEST) && P4_WIFI_SETUP_TEST
    Serial.printf("[P4 STACK] setup completed, loopTask stack high-water mark: %lu bytes\n",
                  (unsigned long)uxTaskGetStackHighWaterMark(nullptr));
#endif
}

void loop() {
    // P4 diagnostic-only serial CLI. Useful where USB CDC has no working
    // RTS/DTR reset wiring. Accept an exact line "reboot" or "restart".
    // Keep a bounded command buffer; never echo credentials.
    static char serialCommand[16] = {};
    static size_t serialCommandLength = 0;
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r' || c == '\n') {
            serialCommand[serialCommandLength] = 0;
            if (!strcmp(serialCommand, "reboot") ||
                !strcmp(serialCommand, "restart")) {
                Serial.println("[P4 CLI] Reboot requested");
                Serial.flush();
                requestedRestartMagic = kRequestedRestartMagic;
                delay(100);
                ESP.restart();
            }
            serialCommandLength = 0;
            serialCommand[0] = 0;
        } else if (c >= 32 && c <= 126) {
            if (serialCommandLength < sizeof(serialCommand) - 1) {
                serialCommand[serialCommandLength++] = c;
            } else {
                serialCommandLength = 0;
                serialCommand[0] = 0;
            }
        }
    }
    if (crashBoots >= 3) {
        delay(5000);
        Serial.printf("[P4 SAFE MODE] prior-stage=%lu; no display init attempted\n", (unsigned long)lastStage);
        return;
    }
#if defined(P4_WIFI_SETUP_TEST) && P4_WIFI_SETUP_TEST
    p4WifiTick();
#endif
#if !defined(P4_STATIC_SCANOUT_TEST) || !P4_STATIC_SCANOUT_TEST
    lv_timer_handler();
#endif
    if (millis() - lastHeartbeat >= 5000) {
#if defined(P4_WIFI_SETUP_TEST) && P4_WIFI_SETUP_TEST
        Serial.printf("[P4 STACK] loopTask minimum free stack: %lu bytes\n",
                      (unsigned long)uxTaskGetStackHighWaterMark(nullptr));
#endif
        lastHeartbeat = millis();
        Serial.printf("P4 display alive: PSRAM %lu, taps %lu, frames %lu, scanout underruns %lu\n",
                       (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                       (unsigned long)presses,
                       (unsigned long)display.getFrameCount(),
                       (unsigned long)display.getUnderrunCount());
    }
    delay(5);
}
