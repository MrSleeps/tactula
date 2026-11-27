#include "p4_board.h"

#include <chipguy_JC8012P4A1C_display.h>
#include <driver/gpio.h>
#include <esp_cache.h>
#include <atomic>
#include <Preferences.h>
#include "debug_log.h"

#if defined(TACTULA_P4_CACHE_SAFE_SCANOUT)
#include "esp_memory_utils.h"
#include "esp_private/dw_gdma.h"
#include "esp_timer.h"

#if !CONFIG_LCD_DSI_ISR_CACHE_SAFE || !CONFIG_DW_GDMA_ISR_IRAM_SAFE
#error "P4 cache-safe scanout requires rebuilt ESP-IDF libraries"
#endif
#endif

#if defined(TACTULA_P4_MANAGED_BACKLIGHT)
extern "C" void tactula_p4_scanout_timing(uint32_t*, uint32_t*, uint32_t*);
#endif

namespace p4Board {
namespace {

JC8012P4A1C_Board board;
chipguy_JC8012P4A1C_display display(board);

// DSI starts scanning buffer 0. Render into the inactive buffer first.
uint8_t nextFramebuffer = 1;
bool flipPending = false;
const uint16_t* lastPresented = nullptr;
uint8_t* pendingNativeWrite = nullptr;
NativeScanoutWrite pendingNativeRanges = {};
size_t nextNativeRange = 0;
uint32_t backlightWrites = 0;
std::atomic<bool> firstFramePresented{false};
std::atomic<bool> liveReload{false};
// Persist the provisional boot state before touching DMA. A reset before
// the application proves healthy forces the original driver next time.
constexpr uint8_t DMA_AUTO = 0, DMA_PENDING = 1, DMA_LEGACY = 2;
bool bootReloadPending = false;
bool storeDmaBootMode(uint8_t mode) {
    Preferences prefs;
    if (!prefs.begin("p4dma", false)) return false;
    const bool ok = prefs.putUChar("bootmode", mode) == 1;
    prefs.end();
    return ok;
}
bool armDmaBoot() {
    Preferences prefs;
    if (!prefs.begin("p4dma", false)) return false;
    const uint8_t mode = prefs.getUChar("bootmode", DMA_AUTO);
    if (mode != DMA_AUTO) {
        if (mode == DMA_PENDING) prefs.putUChar("bootmode", DMA_LEGACY);
        prefs.end();
        debugSerial.println("Deck: P4 boot DMA legacy (recovery or disabled)");
        return false;
    }
    const bool ok = prefs.putUChar("bootmode", DMA_PENDING) == 1;
    prefs.end();
    return ok;
}
constexpr gpio_num_t backlightPin = static_cast<gpio_num_t>(JC8012P4A1C_Board::LCD_BACKLIGHT_PIN);

void driveBacklightLow() {
    gpio_set_level(backlightPin, 0);
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << backlightPin;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_config(&config);
}

bool flushNativeWrite() {
    if (!pendingNativeWrite) return true;
    while (nextNativeRange < pendingNativeRanges.count) {
        const auto& range = pendingNativeRanges.ranges[nextNativeRange];
        if (esp_cache_msync(pendingNativeWrite + range.start, range.end - range.start,
                           ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK)
            return false;
        ++nextNativeRange;
    }
    pendingNativeWrite = nullptr;
    return true;
}

} // namespace

chipguy_JC8012P4A1C_display& panel() {
    return display;
}

void begin() {
    board.begin();
}

bool beginPanel(uint8_t rotation, bool allocateDrawBuffers) {
#if defined(TACTULA_P4_CACHE_SAFE_SCANOUT)
    // The Guition driver restarts its prebuilt DMA list at every frame boundary.
    // Check the linked SDK functions before starting a cache-safe interrupt.
    if (!esp_ptr_in_iram(reinterpret_cast<const void*>(dw_gdma_link_list_get_item)) ||
        !esp_ptr_in_iram(reinterpret_cast<const void*>(dw_gdma_channel_use_link_list)) ||
        !esp_ptr_in_iram(reinterpret_cast<const void*>(dw_gdma_channel_enable_ctrl)) ||
        !esp_ptr_in_iram(reinterpret_cast<const void*>(esp_timer_get_time))) {
        Serial.println("P4: cache-safe scanout functions are outside IRAM");
        return false;
    }
    Serial.println("P4: cache-safe scanout IRAM checks passed");
#endif
    // Proven timing for the newer JD9365 panel: 80 MHz DPI at ~59.94 Hz.
    display.setPanelTiming(JC8012PanelTiming{80000000, 20, 20, 162, 4, 10, 38});
    if (!allocateDrawBuffers) display.setDrawBuffers(false);
    nextFramebuffer = 1;
    flipPending = false;
    lastPresented = nullptr;
    pendingNativeWrite = nullptr;
    firstFramePresented.store(false, std::memory_order_relaxed);
    if (!display.begin(rotation)) return false;
    display.setBacklight(0);
    if (allocateDrawBuffers && armDmaBoot()) {
        if (testLiveReload()) {
            bootReloadPending = true;
            debugSerial.println("Deck: P4 boot live DMA reload active before splash");
        } else {
            storeDmaBootMode(DMA_LEGACY);
            debugSerial.println("Deck: P4 boot reload refused; original driver retained");
        }
    }
    return true;
}

bool testStaticReload() {
    return display.testStaticReload();
}

bool testLiveReload() {
    if (!readyToDrawFrame() || !display.testLiveReload()) return false;
    liveReload.store(true, std::memory_order_release);
    return true;
}

bool liveReloadActive() {
    return liveReload.load(std::memory_order_acquire);
}

void confirmDmaBoot() {
    if (!bootReloadPending || !hasPresentedFrame()) return;
    if (storeDmaBootMode(DMA_AUTO)) {
        bootReloadPending = false;
        debugSerial.println("Deck: P4 boot live DMA validated (startup guard cleared)");
    }
}

bool setDmaBootEnabled(bool enabled) {
    if (!storeDmaBootMode(enabled ? DMA_AUTO : DMA_LEGACY)) return false;
    bootReloadPending = false;
    return true;
}

void enableRadio() {
    board.setRadioPower(true);
    delay(200);
}

void prepareBacklight() {
    driveBacklightLow();
    // Release the restart hold after configuring the new output to the same level.
    gpio_hold_dis(backlightPin);
}

void blankBacklightForRestart() {
    display.setBacklight(0);
    ledcDetach(backlightPin);
    driveBacklightLow();
    gpio_hold_en(backlightPin);
}

bool hasPresentedFrame() {
    return firstFramePresented.load(std::memory_order_acquire);
}

void setBacklight(uint8_t level) {
    display.setBacklight((int(level) * 100 + 127) / 255);
    ++backlightWrites;
}

BacklightStats backlightStats() {
    return {ledcRead(backlightPin), ledcReadFreq(backlightPin), backlightWrites};
}

uint16_t* drawBuffer() {
    return display.getDrawBuffer(0);
}

bool readyToDrawFrame() {
    // A timed-out PPA worker can still be reading the staging buffer.
    if (flipPending) {
        if (!display.waitFlipComplete(500)) return false;
        flipPending = false;
    }
    if (flushNativeWrite()) return true;
    delay(1);
    return false;
}

bool presentFrame() {
    // Also used immediately after a failed native update. Write back any CPU
    // changes before allowing DMA to replace that framebuffer.
    if (!flushNativeWrite()) return false;
    // The managed driver selects the buffer not in use by scanout.
    display.flip(0, nextFramebuffer);
    flipPending = true;
    bool presented = false;
    if (!display.waitFlipComplete(500, &presented)) return false;
    flipPending = false;
    if (!presented) return false;
    nextFramebuffer ^= 1;
    lastPresented = display.lastPresentedFramebuffer();
    firstFramePresented.store(true, std::memory_order_release);
    return true;
}

ScanoutTarget idleScanoutTarget() {
    if (liveReloadActive() || flipPending || pendingNativeWrite) return {nullptr, 0};
    const int index = display.idleFramebufferIndex();
    if (index < 0) return {nullptr, 0};
    return {display.getFramebuffer(index), static_cast<uint8_t>(index)};
}

bool presentNativeScanout(const ScanoutTarget& target, const NativeScanoutWrite& write) {
    if (liveReloadActive()) return false;
    pendingNativeWrite = reinterpret_cast<uint8_t*>(target.pixels);
    pendingNativeRanges = write;
    nextNativeRange = 0;
    if (!flushNativeWrite()) {
        Serial.println("P4: native scanout cache writeback failed");
        return false;
    }
    if (display.idleFramebufferIndex() != target.index) return false;
    display.setActiveFramebuffer(target.index, true);
    if (!display.isFramebufferScanning(target.index)) return false;
    lastPresented = target.pixels;
    nextFramebuffer = target.index ^ 1;
    return true;
}

const uint16_t* lastPresentedFramebuffer() {
    return lastPresented;
}

uint32_t scanoutFrames() {
    return display.getFrameCount();
}

uint32_t scanoutUnderruns() {
    return display.getUnderrunCount();
}

ScanoutTiming takeScanoutTiming() {
    ScanoutTiming timing{};
#if defined(TACTULA_P4_MANAGED_BACKLIGHT)
    tactula_p4_scanout_timing(&timing.worstGapUs, &timing.lateFrames, &timing.lastLateUs);
#endif
    return timing;
}

bool readTouch(TouchSample& sample) {
    return display.touchAvailable() &&
           display.getTouchDriver().getTouch(&sample.nativeX, &sample.nativeY, &sample.fingers) &&
           display.mapTouch(sample.nativeX, sample.nativeY, sample.x, sample.y);
}

} // namespace p4Board
