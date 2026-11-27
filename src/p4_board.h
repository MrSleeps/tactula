#pragma once

#include <stddef.h>
#include <stdint.h>

class chipguy_JC8012P4A1C_display;

namespace p4Board {

// All coordinates returned here match the configured display orientation.
struct TouchSample {
    uint16_t nativeX, nativeY;
    int32_t x, y;
    uint8_t fingers;
};

chipguy_JC8012P4A1C_display& panel();
void begin();
bool beginPanel(uint8_t rotation, bool allocateDrawBuffers);
bool testStaticReload();
bool testLiveReload();
bool liveReloadActive();
void confirmDmaBoot();
bool setDmaBootEnabled(bool enabled);
void enableRadio();
void prepareBacklight();
void blankBacklightForRestart();
bool hasPresentedFrame();
void setBacklight(uint8_t level);  // Tactula brightness, 0–255
struct BacklightStats {
    uint32_t duty, frequencyHz, writes;
};
BacklightStats backlightStats();
uint16_t* drawBuffer();
bool readyToDrawFrame();
bool presentFrame();
struct ScanoutTarget {
    uint16_t* pixels;
    uint8_t index;
};
struct NativeScanoutWrite {
    static constexpr size_t MaxRanges = 16;
    struct Range { size_t start, end; };
    Range ranges[MaxRanges];
    size_t count;
};
// Flush task only, with the PPA idle. A pending page flip has no writable target.
ScanoutTarget idleScanoutTarget();
bool presentNativeScanout(const ScanoutTarget& target, const NativeScanoutWrite& write);
const uint16_t* lastPresentedFramebuffer();
uint32_t scanoutFrames();
uint32_t scanoutUnderruns();
struct ScanoutTiming {
    uint32_t worstGapUs, lateFrames, lastLateUs;
};
ScanoutTiming takeScanoutTiming();
bool readTouch(TouchSample& sample);

} // namespace p4Board
