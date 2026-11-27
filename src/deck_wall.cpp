// Guition deck: Glass theme wallpaper. See deck_wall.h.
#include "debug_log.h"
#include "p4_trace.h"
#include "deck_wall.h"
#include <src/misc/cache/instance/lv_image_cache.h>
#include "deck_config.h"
#include "deck_panel.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <math.h>
#include <Preferences.h>
#include <esp_random.h>
#include <algorithm>
#include <memory>
#include "tactula_settings.h"
#include "config.h"
#include <src/libs/tjpgd/tjpgd.h>

namespace deck {
namespace {

uint16_t* s_sharp = nullptr;  // RGB565, dimmed and dithered
// Keep the full-resolution BGR wallpaper in independently allocated bands.
constexpr int MAX_BLUR_BANDS = (NATIVE_H + WALL_BLUR_BAND_ROWS - 1) / WALL_BLUR_BAND_ROWS;
struct BlurPixels {
  uint8_t* bands[MAX_BLUR_BANDS];
  int stride;

  uint8_t* row(int y) const {
    return bands[y / WALL_BLUR_BAND_ROWS] + (y % WALL_BLUR_BAND_ROWS) * stride;
  }

  void release() {
    for (uint8_t*& band : bands) {
      free(band);
      band = nullptr;
    }
  }
};
BlurPixels s_blur = {};
int s_w = 0, s_h = 0, s_blurR = -1, s_dim = -1;
lv_image_dsc_t s_sharpDsc, s_blurDsc[MAX_BLUR_BANDS];
uint32_t s_generation = 0;
String s_current[2], s_pin[2], s_requested[2], s_loadedName;
bool s_initialized[2] = {}, s_request[2] = {}, s_automatic[2] = {};
uint8_t s_source = 0;
bool s_sourceLoaded = false;
WallVector<uint64_t> s_shown[2];  // shuffle: pictures shown in this round, RAM only
uint32_t s_requestId[2] = {};
uint32_t s_epoch = 0, s_changedMs = 0, s_retryMs = 0, s_scanMs = 0;
portMUX_TYPE s_jobMux = portMUX_INITIALIZER_UNLOCKED;
enum JobState { JOB_IDLE, JOB_RUNNING, JOB_READY };
JobState s_jobState = JOB_IDLE;
struct WallJob {
  int w, h, blur, dim;
  uint64_t settings;
  uint32_t epoch, requestId;
  char name[15], path[64];
  uint16_t* sharp;
  BlurPixels blurred;
  uint8_t* jpeg;
  uint8_t* scratch;
  size_t jpegBytes;
  bool ok, automatic, reuseBuffers;
};
WallJob s_job = {};

uint64_t settingsKey() {
  return (uint64_t)dispSettings.rotation | (uint64_t)dispSettings.theme << 8 |
         (uint64_t)dispSettings.glassBlur << 16 | (uint64_t)dispSettings.glassDim << 24 |
         (uint64_t)dispSettings.glassTint << 32 | (uint64_t)dispSettings.wallEvery << 40 |
         (uint64_t)dispSettings.wallOrder << 48 | (uint64_t)dispSettings.glassOn << 56;
}

JobState jobState() {
  portENTER_CRITICAL(&s_jobMux);
  const JobState state = s_jobState;
  portEXIT_CRITICAL(&s_jobMux);
  return state;
}

void discardJob() {
  free(s_job.sharp);
  free(s_job.jpeg);
  free(s_job.scratch);
  s_job.jpeg = s_job.scratch = nullptr;
  s_job.blurred.release();
  s_job.sharp = nullptr;
  portENTER_CRITICAL(&s_jobMux);
  s_jobState = JOB_IDLE;
  portEXIT_CRITICAL(&s_jobMux);
}

// LittleFS reads disable the PSRAM cache, so they need an internal stack (wallRunWorker).
class WallReader {
 public:
  explicit WallReader(const char* path, bool legacy = false)
      : file_((legacy ? LittleFS : wallFs()).open(path, "r")) {
    // Over 4 KB, so malloc puts the stdio buffer in PSRAM instead of internal RAM.
    if (file_) file_.setBufferSize(8192);
  }
  explicit operator bool() const { return (bool)file_; }
  size_t size() { return file_.size(); }
  size_t position() { return file_.position(); }
  size_t read(uint8_t* data, size_t count) { return file_.read(data, count); }
  int read() { return file_.read(); }
  bool seek(size_t position) { return file_.seek(position); }
  void close() { file_.close(); }

 private:
  File file_;
};

// FASTDECODE 1 needs up to 4 KB for standard tables, input and MCU buffers.
constexpr size_t JPEG_WORK_BYTES = 4096;

struct JpegInput {
  BlurPixels* bgr;
  int w, h;
  WallReader* file = nullptr;
  const uint8_t* data = nullptr;
  size_t bytes = 0, offset = 0;
  uint16_t* rgb565 = nullptr;
  int sourceW = 0, sourceH = 0, cropX = 0, cropY = 0;
};

size_t jpegRead(JDEC* jd, uint8_t* data, size_t size) {
  JpegInput& in = *(JpegInput*)jd->device;
  if (!in.file) {
    const size_t count = std::min(size, in.bytes - in.offset);
    if (data) memcpy(data, in.data + in.offset, count);
    in.offset += count;
    return count;
  }
  if (data) return in.file->read(data, size);
  const size_t pos = in.file->position();
  const size_t count = std::min(size, in.file->size() - pos);
  return in.file->seek(pos + count) ? count : 0;
}

// Map one decoded JPEG block into the display without a second full image.
int jpegScaledOutput(JpegInput& in, const uint8_t* pixels, const JRECT& rect) {
  const int stride = (rect.right - rect.left + 1) * 3;
  const auto edge = [](int src, int crop, int extent, int output) {
    const int relative = std::max(0, std::min(src - crop, extent));
    return (relative * output + extent - 1) / extent;
  };
  const int x0 = edge(rect.left, in.cropX, in.sourceW, in.w);
  const int x1 = edge(rect.right + 1, in.cropX, in.sourceW, in.w);
  const int y0 = edge(rect.top, in.cropY, in.sourceH, in.h);
  const int y1 = edge(rect.bottom + 1, in.cropY, in.sourceH, in.h);
  for (int y = y0; y < y1; ++y) {
    const int sy = in.cropY + y * in.sourceH / in.h - rect.top;
    uint8_t* row = in.bgr ? in.bgr->row(y) : nullptr;
    for (int x = x0; x < x1; ++x) {
      const int sx = in.cropX + x * in.sourceW / in.w - rect.left;
      const uint8_t* src = pixels + sy * stride + sx * 3;
      if (row) memcpy(row + x * 3, src, 3);
      else in.rgb565[y * in.w + x] = wallDither565(src[2], src[1], src[0], x, y);
    }
  }
  return 1;
}

int jpegOutput(JDEC* jd, void* pixels, JRECT* rect) {
  JpegInput& in = *(JpegInput*)jd->device;
  if (!in.bgr && !in.rgb565) return 1;  // Verify without changing the displayed pixels.
  if (in.sourceW) return jpegScaledOutput(in, (const uint8_t*)pixels, *rect);
  if (rect->right >= in.w || rect->bottom >= in.h) return 0;
  const uint8_t* src = (const uint8_t*)pixels;
  for (int y = rect->top; y <= rect->bottom; y++) {
    if (in.bgr) {
      // LVGL's bundled JD_FORMAT 0 already emits B, G, R.
      const size_t bytes = (rect->right - rect->left + 1) * 3;
      memcpy(in.bgr->row(y) + 3 * rect->left, src, bytes);
      src += bytes;
    } else {
      for (int x = rect->left; x <= rect->right; ++x, src += 3)
        in.rgb565[y * in.w + x] = wallDither565(src[2], src[1], src[0], x, y);
    }
  }
  return 1;
}

bool decodeJpeg(JpegInput& in, void* work, bool decompress) {
  JDEC jd;
  if (jd_prepare(&jd, jpegRead, work, JPEG_WORK_BYTES, &in) != JDR_OK) return false;
  if (jd.width != in.w || jd.height != in.h) {
    const bool portrait = in.w < in.h;
    if (!decompress || jd.width != (portrait ? 320 : 480) ||
        jd.height != (portrait ? 480 : 320)) return false;
    in.sourceW = std::min<int>(jd.width, jd.height * in.w / in.h);
    in.sourceH = std::min<int>(jd.height, jd.width * in.h / in.w);
    in.cropX = (jd.width - in.sourceW) / 2;
    in.cropY = (jd.height - in.sourceH) / 2;
  }
  return !decompress || jd_decomp(&jd, jpegOutput, 0) == JDR_OK;
}

bool jpegLoad(const char* path, int w, int h, BlurPixels* bgr, uint16_t* rgb565 = nullptr) {
  WallReader file(path);
  if (!file || file.read() != 0xFF || file.read() != 0xD8 || !file.seek(0)) return false;
  void* work = heap_caps_malloc(JPEG_WORK_BYTES, MALLOC_CAP_SPIRAM);
  if (!work) return false;
  JpegInput in{bgr, w, h, &file};
  in.rgb565 = rgb565;
  const bool ok = decodeJpeg(in, work, bgr || rgb565);
  free(work);
  return ok;
}

bool decodeStagedJpeg(const WallJob& job, BlurPixels* output) {
  JpegInput in{output, job.w, job.h};
  in.data = job.jpeg;
  in.bytes = job.jpegBytes;
  return decodeJpeg(in, job.scratch, true);
}

// Each gallery keeps its own pin.
const char* pinKey(bool portrait) {
  if (s_source == 2) return portrait ? "wpin_bp" : "wpin_bl";
  return s_source == 1 ? (portrait ? "wpin_dp" : "wpin_dl") : (portrait ? "wpin_p" : "wpin_l");
}

void loadSource() {
  if (s_sourceLoaded) return;
  s_source = dispSettings.wallSource == WALL_SOURCE_FEED ? 1 : dispSettings.wallSource;
  s_sourceLoaded = true;
}

void initGallery(bool portrait) {
  if (s_initialized[portrait]) return;
  loadSource();
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    const char* key = pinKey(portrait);
    s_pin[portrait] = prefs.isKey(key) ? prefs.getString(key, "") : String();
    prefs.end();
  }
  if (!wallValidRef(s_pin[portrait])) s_pin[portrait] = "";
  auto files = wallFiles(portrait);
  for (const auto& f : files)
    if (f.name() == s_pin[portrait]) s_current[portrait] = f.name();
  if (!s_current[portrait].length() && !files.empty())
    s_current[portrait] = files[dispSettings.wallOrder ? esp_random() % files.size() : 0].name();
  s_initialized[portrait] = true;
  if (s_pin[portrait].length() && !wallFs().exists(wallGalleryPath(portrait, s_pin[portrait])))
    wallClearPin(portrait);
}

void fillDsc(lv_image_dsc_t& d, const void* px, int w, int h, lv_color_format_t cf, int bpp) {
  memset(&d, 0, sizeof(d));
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.header.cf = cf;
  d.header.w = w;
  d.header.h = h;
  d.header.stride = w * bpp;
  d.data = (const uint8_t*)px;
  d.data_size = (uint32_t)w * h * bpp;
}

uint8_t clamp8(float v) { return v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t)(v + 0.5f); }

// No file yet: a dusk gradient with a warm glow, so Glass works out of the box.
// Written in LVGL's packed BGR byte order, like a decoded JPEG.
void gradient(BlurPixels& bgr, int w, int h) {
  for (int y = 0; y < h; y++) {
    uint8_t* row = bgr.row(y);
    for (int x = 0; x < w; x++) {
      const float t = (x / (float)w + y / (float)h) / 2;
      const float dx = (x - 0.2f * w) / w, dy = (y - 1.05f * h) / h;
      const float glow = fmaxf(0, 1 - sqrtf(dx * dx + dy * dy) / 0.75f);
      const float g2 = glow * glow;
      uint8_t* pixel = row + 3 * x;
      pixel[0] = clamp8(0x5C + (0x66 - 0x5C) * t + 0x10 * g2);
      pixel[1] = clamp8(0x2C + (0x5E - 0x2C) * t + 0x58 * g2);
      pixel[2] = clamp8(0x24 + (0x16 - 0x24) * t + 0xB0 * g2);
    }
  }
}

// The file is RGB888 (r, g, b bytes) or, from older portals, RGB565.
bool loadFile(BlurPixels& bgr, int w, int h) {
  if (!deckFsReady()) return false;
  WallReader f(wallPath(w, h), true);
  if (!f) return false;
  const size_t n = (size_t)w * h, size = f.size();
  bool ok = false;
  if (size == n * 3 || size == n * 2) {
    const int bpp = size == n * 3 ? 3 : 2;
    uint8_t* row = (uint8_t*)heap_caps_malloc(w * bpp, MALLOC_CAP_SPIRAM);
    ok = row != nullptr;
    for (int y = 0; ok && y < h; y++) {
      ok = f.read(row, w * bpp) == (size_t)(w * bpp);
      uint8_t* dst = bgr.row(y);
      for (int x = 0; ok && x < w; x++) {
        uint8_t* pixel = dst + 3 * x;
        if (bpp == 3) {
          pixel[0] = row[x * 3 + 2];
          pixel[1] = row[x * 3 + 1];
          pixel[2] = row[x * 3];
        } else {
          const uint16_t p = row[x * 2] | row[x * 2 + 1] << 8;
          pixel[0] = (p << 3 & 0xF8) | (p >> 2 & 0x07);
          pixel[1] = (p >> 3 & 0xFC) | (p >> 9 & 0x03);
          pixel[2] = (p >> 8 & 0xF8) | p >> 13;
        }
      }
    }
    free(row);
  }
  f.close();
  return ok;
}

// One box pass of radius r along rows or columns, edges clamped, rounded.
// Three passes come close to a gaussian.
void boxPass(const uint8_t* src, uint8_t* dst, int w, int h, bool vertical, int r) {
  const int n = 2 * r + 1;
  const uint32_t inv = 0xFFFFFFFFu / n + 1;  // exact x / n as (x * inv) >> 32 for these sums
  auto avg = [inv](uint32_t s) { return (uint8_t)(((uint64_t)s * inv) >> 32); };
  if (!vertical) {
    for (int l = 0; l < h; l++) {
      const uint8_t* s = src + l * w;
      uint8_t* d = dst + l * w;
      uint32_t sum = n / 2;
      for (int i = -r; i <= r; i++) sum += s[i < 0 ? 0 : i >= w ? w - 1 : i];
      for (int i = 0; i < w; i++) {
        d[i] = avg(sum);
        const int add = i + r + 1, sub = i - r;
        sum += s[add >= w ? w - 1 : add] - s[sub < 0 ? 0 : sub];
      }
    }
    return;
  }
  // Columns in strips walked row by row: PSRAM reads stay in cache lines.
  constexpr int STRIP = 32;
  uint32_t sum[STRIP];
  for (int x0 = 0; x0 < w; x0 += STRIP) {
    const int cols = std::min(STRIP, w - x0);
    for (int c = 0; c < cols; c++) sum[c] = n / 2;
    for (int i = -r; i <= r; i++) {
      const uint8_t* s = src + (i < 0 ? 0 : i >= h ? h - 1 : i) * w + x0;
      for (int c = 0; c < cols; c++) sum[c] += s[c];
    }
    for (int i = 0; i < h; i++) {
      uint8_t* d = dst + i * w + x0;
      const int add = i + r + 1, sub = i - r;
      const uint8_t* a = src + (add >= h ? h - 1 : add) * w + x0;
      const uint8_t* b = src + (sub < 0 ? 0 : sub) * w + x0;
      for (int c = 0; c < cols; c++) {
        d[c] = avg(sum[c]);
        sum[c] += a[c] - b[c];
      }
    }
  }
}

// Blur one plane in place (tmp: scratch of the same size).
void blurPlane(uint8_t* p, uint8_t* tmp, int w, int h, int r) {
  if (r <= 0) return;
  for (int pass = 0; pass < 3; pass++) {
    boxPass(p, tmp, w, h, false, r);
    boxPass(tmp, p, w, h, true, r);
  }
  if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
}

}  // namespace

void wallBlurPlane(uint8_t* p, uint8_t* tmp, int w, int h, int r) { blurPlane(p, tmp, w, h, r); }

const char* wallPath(int w, int h) { return w > h ? "/wall_l.bin" : "/wall_p.bin"; }

namespace {

// Blur one packed row or gathered column before replacing its source bytes.
void blurPackedLine(uint8_t* pixels, uint8_t* line, int length,
                    int radius, int window, uint32_t reciprocal) {
  uint32_t sum[3] = {uint32_t(window / 2), uint32_t(window / 2), uint32_t(window / 2)};
  for (int i = -radius; i <= radius; ++i) {
    const uint8_t* pixel = pixels + std::clamp(i, 0, length - 1) * 3;
    for (int c = 0; c < 3; ++c) sum[c] += pixel[c];
  }
  for (int i = 0; i < length; ++i) {
    const uint8_t* add = pixels + std::min(i + radius + 1, length - 1) * 3;
    const uint8_t* sub = pixels + std::max(i - radius, 0) * 3;
    for (int c = 0; c < 3; ++c) {
      line[3 * i + c] = uint8_t((uint64_t(sum[c]) * reciprocal) >> 32);
      sum[c] += add[c] - sub[c];
    }
  }
  memcpy(pixels, line, 3 * length);
}

void blurPacked(BlurPixels& pixels, uint8_t* line, int w, int h, int radius) {
  if (radius <= 0) return;
  const int window = 2 * radius + 1;
  const uint32_t reciprocal = 0xFFFFFFFFu / window + 1;
  uint8_t* column = line + 3 * std::max(w, h);
  for (int pass = 0; pass < 3; ++pass) {
    for (int y = 0; y < h; ++y)
      blurPackedLine(pixels.row(y), line, w, radius, window, reciprocal);
    for (int x = 0; x < w; ++x) {
      for (int y = 0; y < h; ++y) memcpy(column + 3 * y, pixels.row(y) + 3 * x, 3);
      blurPackedLine(column, line, h, radius, window, reciprocal);
      for (int y = 0; y < h; ++y) memcpy(pixels.row(y) + 3 * x, column + 3 * y, 3);
    }
    if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
  }
}

void finishImage(const WallJob& job, uint16_t* sharp, BlurPixels& pixels, uint8_t* scratch) {
  const int w = job.w, h = job.h;
  // Pre-composite a black overlay with the wallpaper ONCE when it loads.
  // Keep this off the render loop: both sharp and frosted tiles use the same
  // dimming, while labels, icons and controls are drawn afterwards untouched.
  const int keep = (100 - job.dim) * 256 / 100;
  for (int y = 0; y < h; y++) {
    const uint8_t* row = pixels.row(y);
    for (int x = 0; x < w; x++) {
      const uint8_t* pixel = row + 3 * x;
      sharp[y * w + x] = wallDither565(pixel[2] * keep >> 8, pixel[1] * keep >> 8,
                                      pixel[0] * keep >> 8, x, y);
    }
  }
  blurPacked(pixels, scratch, w, h, job.blur);
  for (int y = 0; y < h; ++y) {
    uint8_t* row = pixels.row(y);
    for (int x = 0; x < pixels.stride; ++x) row[x] = row[x] * keep >> 8;
  }
}

bool prepareImage(WallJob& job) {
  const int w = job.w, h = job.h;
  if (w <= 0 || h <= 0 || h > NATIVE_H) return false;
  const size_t n = (size_t)w * h;
  constexpr auto caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const auto allocationFailed = [](size_t bytes) {
    debugSerial.printf("Deck: wallpaper PSRAM allocation failed (requested %u B, free %u B, largest %u B)\n",
                       static_cast<unsigned>(bytes),
                       static_cast<unsigned>(heap_caps_get_free_size(caps)),
                       static_cast<unsigned>(heap_caps_get_largest_free_block(caps)));
    return false;
  };
  // Blur scratch holds one output line and one gathered column.
  const size_t scratchBytes = 3 * (std::max(w, h) + h);
  if (job.reuseBuffers) {
    // Keep just the compressed file. Validate all its JPEG blocks off the UI
    // thread before decoding the same bytes into the existing bands at commit.
    WallReader file(job.path);
    const size_t bytes = file ? file.size() : 0;
    if (bytes < 2) return false;
    job.jpeg = static_cast<uint8_t*>(heap_caps_malloc(bytes, caps));
    if (!job.jpeg) return allocationFailed(bytes);
    if (file.read(job.jpeg, bytes) != bytes || job.jpeg[0] != 0xFF || job.jpeg[1] != 0xD8) {
      debugSerial.printf("Deck: wallpaper read failed: %s\n", job.name);
      return false;
    }
    job.jpegBytes = bytes;
    const size_t workBytes = job.blur ? std::max(scratchBytes, JPEG_WORK_BYTES) : JPEG_WORK_BYTES;
    job.scratch = static_cast<uint8_t*>(heap_caps_malloc(workBytes, caps));
    if (!job.scratch) return allocationFailed(workBytes);
    if (!decodeStagedJpeg(job, nullptr)) {
      debugSerial.printf("Deck: wallpaper decode failed: %s\n", job.name);
      return false;
    }
    return true;
  }
  job.sharp = static_cast<uint16_t*>(heap_caps_malloc(n * 2, caps));
  if (!job.sharp) return allocationFailed(n * 2);
  job.blurred.stride = 3 * w;
  for (int y = 0; y < h; y += WALL_BLUR_BAND_ROWS) {
    const size_t bytes = job.blurred.stride * std::min(WALL_BLUR_BAND_ROWS, h - y);
    uint8_t*& band = job.blurred.bands[y / WALL_BLUR_BAND_ROWS];
    band = static_cast<uint8_t*>(heap_caps_malloc(bytes, caps));
    if (!band) return allocationFailed(bytes);
  }

  std::unique_ptr<uint8_t, decltype(&free)> line(nullptr, free);
  if (job.blur > 0) {
    line.reset(static_cast<uint8_t*>(heap_caps_malloc(scratchBytes, caps)));
    if (!line) return allocationFailed(scratchBytes);
  }
  uint32_t start = millis();
  const bool jpeg = job.name[0];
  const bool loaded = jpeg ? jpegLoad(job.path, w, h, &job.blurred)
                           : loadFile(job.blurred, w, h);
  if (jpeg && !loaded) {
    debugSerial.printf("Deck: wallpaper decode failed: %s\n", job.name);
    return false;
  }
  if (!loaded) gradient(job.blurred, w, h);
  const uint32_t decodeMs = millis() - start;
  start = millis();
  finishImage(job, job.sharp, job.blurred, line.get());
  debugSerial.printf("Deck: wallpaper %s decode %lu ms, dim+blur %lu ms\n",
                jpeg ? job.name : loaded ? "legacy" : "gradient",
                (unsigned long)decodeMs, (unsigned long)(millis() - start));
  return true;
}

void wallJob() {
  portENTER_CRITICAL(&s_jobMux);
  WallJob job = s_job;
  portEXIT_CRITICAL(&s_jobMux);
  {
    p4Trace::WorkerScope trace(p4Trace::Worker::WallpaperDecode);
    job.ok = prepareImage(job);
  }
  portENTER_CRITICAL(&s_jobMux);
  s_job = job;
  s_jobState = JOB_READY;
  portEXIT_CRITICAL(&s_jobMux);
}

QueueHandle_t s_sdJobs = nullptr;

void sdWorker(void*) {
  void (*job)();
  for (;;)
    if (xQueueReceive(s_sdJobs, &job, portMAX_DELAY) == pdTRUE) job();
}

void flashWorker(void* job) {
  ((void (*)())job)();
  vTaskDelete(nullptr);
}

}  // namespace

bool wallRunWorker(void (*job)(), bool onSd, const char* name) {
  if (!onSd) return xTaskCreatePinnedToCore(flashWorker, name, 6144, (void*)job, 1, nullptr, 0) == pdPASS;
  if (!s_sdJobs) {
    s_sdJobs = xQueueCreate(4, sizeof(job));
    if (!s_sdJobs) return false;
    if (xTaskCreatePinnedToCoreWithCaps(sdWorker, "deck_pics", 6144, nullptr, 1, nullptr, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
      vQueueDelete(s_sdJobs);
      s_sdJobs = nullptr;
      return false;
    }
  }
  return xQueueSend(s_sdJobs, &job, 0) == pdTRUE;
}

namespace {

void dropImageCache() {
  lv_image_cache_drop(&s_sharpDsc);
  for (int y = 0; y < s_h; y += WALL_BLUR_BAND_ROWS)
    lv_image_cache_drop(&s_blurDsc[y / WALL_BLUR_BAND_ROWS]);
}

void releaseImages() {
  dropImageCache();
  free(s_sharp);
  s_sharp = nullptr;
  s_blur.release();
}

bool installImage(WallJob& job) {
  if (job.reuseBuffers) {
    dropImageCache();
    // Identical bytes, dimensions and workspace already passed a full decode.
    if (!decodeStagedJpeg(job, &s_blur)) return false;
    finishImage(job, s_sharp, s_blur, job.scratch);
  } else {
    releaseImages();
    s_sharp = job.sharp;
    s_blur = job.blurred;
    job.sharp = nullptr;
    job.blurred = {};
  }
  s_w = job.w;
  s_h = job.h;
  s_blurR = job.blur;
  s_dim = job.dim;
  s_loadedName = job.name;
  s_current[job.w < job.h] = job.name;
  wallProtect(job.w < job.h, 0, wallSource() == 1 && job.name[0] ? "d/" + String(job.name) : String(job.name));
  wallProtect(job.w < job.h, 1, "");
  s_changedMs = millis();
  ++s_generation;
  fillDsc(s_sharpDsc, s_sharp, s_w, s_h, LV_COLOR_FORMAT_RGB565, 2);
  for (int y = 0; y < s_h; y += WALL_BLUR_BAND_ROWS) {
    const int band = y / WALL_BLUR_BAND_ROWS;
    fillDsc(s_blurDsc[band], s_blur.bands[band], s_w,
            std::min(WALL_BLUR_BAND_ROWS, s_h - y), LV_COLOR_FORMAT_RGB888, 3);
  }
  return true;
}

}  // namespace

bool wallLoad(int w, int h, int blurR, int dimPct) {
  const bool p = w < h;
  initGallery(p);
#if defined(BOARD_IS_JC8012P4A1C)
  // Leave the portal/LVGL loop free while the full-size image is prepared.
  // Keep the previous background during a same-size theme or settings change.
  const String name = s_request[p] ? s_requested[p] : s_current[p];
  const bool sameSize = s_sharp && s_w == w && s_h == h;
  if (sameSize && s_blurR == blurR && s_dim == dimPct && s_loadedName == name) {
    s_request[p] = false;
    return true;
  }
  if (!sameSize && s_sharp) wallFree();
  wallRequest(p, name);
  // Queue the requested image during splash so decoding overlaps Wi-Fi startup.
  wallPoll(w, h, false, true);
  return sameSize;
#else
  if (s_request[p]) {
    s_current[p] = s_requested[p];
    s_request[p] = false;
  }
  if (s_sharp && s_w == w && s_h == h && s_blurR == blurR && s_dim == dimPct &&
      s_loadedName == s_current[p]) return true;
  wallInvalidateJobs();
  // Settings rebuilds are synchronous; avoid a second pending PSRAM allocation.
  while (jobState() == JOB_RUNNING) delay(1);
  if (jobState() == JOB_READY) discardJob();
  wallFree();
  WallJob job = {};
  job.w = w;
  job.h = h;
  job.blur = blurR;
  job.dim = dimPct;
  strlcpy(job.name, s_current[p].c_str(), sizeof(job.name));
  strlcpy(job.path, wallGalleryPath(p, job.name).c_str(), sizeof(job.path));
  wallProtect(p, 1, s_source == 1 && job.name[0] ? "d/" + String(job.name) : String(job.name));
  if (!prepareImage(job)) {
    free(job.sharp);
    job.blurred.release();
    return false;
  }
  return installImage(job);
#endif
}

void wallFree() {
  wallInvalidateJobs();
  releaseImages();
  s_generation++;  // also covers reloads that reuse the same allocation addresses
  wallProtect(false, 0, "");
  wallProtect(true, 0, "");
  s_w = s_h = 0;
  s_blurR = s_dim = -1;
}

const lv_image_dsc_t* wallSharp() { return s_sharp ? &s_sharpDsc : nullptr; }
const lv_image_dsc_t& wallBlurBand(int y) { return s_blurDsc[y / WALL_BLUR_BAND_ROWS]; }
const uint8_t* wallBlurRow(int y) { return s_blur.row(y); }
uint32_t wallGeneration() { return s_generation; }

bool wallValidName(const String& name) {
  if (name.length() != 12 || !name.endsWith(".jpg")) return false;
  for (int i = 0; i < 8; i++)
    if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
  return true;
}

bool wallValidRef(const String& name) {
  return wallValidName(name) || (name.length() == 14 && name[1] == '/' &&
         (name[0] == 'm' || name[0] == 'd') && wallValidName(name.substring(2)));
}

uint64_t wallId(const String& name) {
  const bool qualified = name.length() == 14;
  return strtoul(name.c_str() + (qualified ? 2 : 0), nullptr, 16) |
         (uint64_t)(qualified && name[0] == 'd') << 32;
}

String WallFile::name() const {
  char text[15];
  if (qualified) snprintf(text, sizeof(text), "%c/%08lx.jpg", id >> 32 ? 'd' : 'm', (unsigned long)(uint32_t)id);
  else snprintf(text, sizeof(text), "%08lx.jpg", (unsigned long)(uint32_t)id);
  return text;
}

uint8_t wallSource() { loadSource(); return s_source; }
bool wallDaily() { return wallSource() != 0; }

void wallSetSource(uint8_t source) {
  loadSource();
  if (source == WALL_SOURCE_FEED) source = 1;
  if (s_source == source) return;
  s_source = source;
  wallInvalidateJobs();
  for (int p = 0; p < 2; p++) {
    s_initialized[p] = false;
    s_current[p] = "";
    s_shown[p].clear();
    initGallery(p);
    wallRequest(p, s_current[p]);
  }
}

String wallGalleryPath(bool portrait, const String& name) {
  const bool qualified = name.length() == 14;
  const bool daily = qualified ? name[0] == 'd' : wallSource() == 1;
  return String(wallDir(portrait, daily)) + "/" + (qualified ? name.substring(2) : name);
}

WallFiles wallFiles(bool portrait) {
  auto files = wallListDir(wallDir(portrait, wallSource() == 1));
  if (wallSource() == 1)
    for (auto& f : files) f.id = (uint32_t)f.id;
  if (wallSource() == 2) {
    auto daily = wallListDir(wallDir(portrait, true));
    for (auto& f : daily) f.id |= (uint64_t)1 << 32;
    files.insert(files.end(), daily.begin(), daily.end());
    for (auto& f : files) f.qualified = true;
    std::sort(files.begin(), files.end(), [](const WallFile& a, const WallFile& b) {
      return (a.id >> 32) != (b.id >> 32) ? (a.id >> 32) > (b.id >> 32) : a.id < b.id;
    });
  }
  return files;
}

String wallCurrent(bool portrait) { initGallery(portrait); return s_current[portrait]; }
String wallPinned(bool portrait) { initGallery(portrait); return s_pin[portrait]; }

bool wallPin(bool portrait, const String& name) {
  if (!wallValidRef(name)) return false;
  initGallery(portrait);
  const String ref = s_source == 1 ? "d/" + name : name;
  // Protect before checking existence, so eviction cannot race the NVS write.
  wallProtect(portrait, 7, ref);
  bool ok = false;
  Preferences prefs;
  if (wallFs().exists(wallGalleryPath(portrait, name)) && prefs.begin(NVS_NAMESPACE, false)) {
    ok = prefs.putString(pinKey(portrait), name) > 0;
    prefs.end();
  }
  if (ok) {
    s_pin[portrait] = name;
    wallProtect(portrait, 4 + s_source, ref);
  }
  wallProtect(portrait, 7, "");
  return ok;
}

void wallClearPin(bool portrait) {
  initGallery(portrait);
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.remove(pinKey(portrait));
    prefs.end();
  }
  s_pin[portrait] = "";
  wallProtect(portrait, 4 + s_source, "");
}

// Camera-specific entry point: accepts arbitrary baseline JPEG sizes, leaving
// the stricter wallpaper validation path unchanged.
bool wallDecodeCamera(const uint8_t* data, size_t bytes, int w, int h, uint16_t* pixels) {
  if (!data || !bytes || !pixels || w < 1 || h < 1) return false;
  void* work = heap_caps_malloc(JPEG_WORK_BYTES, MALLOC_CAP_SPIRAM);
  if (!work) return false;
  JpegInput in{nullptr, w, h};
  in.data = data; in.bytes = bytes; in.rgb565 = pixels;
  JDEC jd;
  bool ok = jd_prepare(&jd, jpegRead, work, JPEG_WORK_BYTES, &in) == JDR_OK;
  if (ok && jd.width && jd.height && jd.width <= 4096 && jd.height <= 4096) {
    in.sourceW = std::max(1, std::min<int>(jd.width, jd.height * w / h));
    in.sourceH = std::max(1, std::min<int>(jd.height, jd.width * h / w));
    in.cropX = (jd.width - in.sourceW) / 2;
    in.cropY = (jd.height - in.sourceH) / 2;
    ok = jd_decomp(&jd, jpegOutput, 0) == JDR_OK;
  } else ok = false;
  free(work);
  return ok;
}

bool wallLoadPicture(const char* path, int w, int h, uint16_t* pixels) {
  return pixels && jpegLoad(path, w, h, nullptr, pixels);
}

bool wallValidateJpeg(const char* path, bool portrait) {
#if defined(BOARD_IS_JC8012P4A1C)
  if (jpegLoad(path, portrait ? 800 : 1280, portrait ? 1280 : 800, nullptr)) return true;
#endif
  return jpegLoad(path, portrait ? 320 : 480, portrait ? 480 : 320, nullptr);
}

void wallInvalidateJobs() {
  ++s_epoch;
  for (int p = 0; p < 2; p++)
    if (s_automatic[p]) s_request[p] = false;
}

void wallRequest(bool portrait, const String& name) {
  if (name.length() && !wallValidRef(name)) return;
  initGallery(portrait);
  ++s_requestId[portrait];
  s_automatic[portrait] = false;
  s_requested[portrait] = name;
  wallProtect(portrait, 1, s_source == 1 && name.length() ? "d/" + name : name);
  s_request[portrait] = true;
  s_retryMs = 0;
  if (portrait != ((dispSettings.rotation & 1) == 0) || !themeUsesWallpaper(dispSettings.theme))
    s_current[portrait] = name;
}

bool wallNext(bool portrait) {
  initGallery(portrait);
  auto files = wallFiles(portrait);
  if (files.empty()) return false;
  size_t index = 0;
  if (dispSettings.wallOrder && files.size() > 1) {
    // Shuffle: no picture again until every one was shown, and a new round
    // never starts with the one on screen.
    WallVector<uint64_t>& shown = s_shown[portrait];
    const String& cur = s_current[portrait];
    if (cur.length() && std::find(shown.begin(), shown.end(), wallId(cur)) == shown.end())
      shown.push_back(wallId(cur));
    WallVector<size_t> choices;
    for (size_t i = 0; i < files.size(); i++)
      if (std::find(shown.begin(), shown.end(), files[i].id) == shown.end()) choices.push_back(i);
    if (choices.empty()) {
      shown.assign(1, wallId(cur));
      for (size_t i = 0; i < files.size(); i++)
        if (files[i].name() != cur) choices.push_back(i);
    }
    index = choices[esp_random() % choices.size()];
    shown.push_back(files[index].id);
  } else {
    for (size_t i = 0; i < files.size(); i++)
      if (files[i].name() > s_current[portrait]) { index = i; break; }
  }
  wallRequest(portrait, files[index].name());
  return true;
}

void wallGalleryChanged(bool portrait) {
  initGallery(portrait);
  portENTER_CRITICAL(&s_jobMux);
  const WallJob job = s_job;
  const JobState state = s_jobState;
  portEXIT_CRITICAL(&s_jobMux);
  if (state != JOB_IDLE && (job.w < job.h) == portrait && job.name[0] &&
      !wallFs().exists(wallGalleryPath(portrait, job.name))) ++s_requestId[portrait];
  auto files = wallFiles(portrait);
  const String current = s_current[portrait];
  bool found = false, pinned = false;
  for (const auto& f : files) {
    found |= f.name() == current;
    pinned |= f.name() == s_pin[portrait];
  }
  if (!pinned && s_pin[portrait].length()) wallClearPin(portrait);
  if (s_request[portrait] && s_requested[portrait].length() &&
      !wallFs().exists(wallGalleryPath(portrait, s_requested[portrait]))) s_request[portrait] = false;
  if (found || s_request[portrait]) return;
  String next;
  if (!files.empty()) {
    next = files.front().name();
    for (const auto& f : files)
      if (f.name() > current) { next = f.name(); break; }
  }
  wallRequest(portrait, next);
}

bool wallPoll(int w, int h, bool idle, bool canSwitch, bool automatic) {
  const bool p = w < h;
  const uint32_t now = millis();
  if (jobState() == JOB_READY) {
    if (s_job.epoch != s_epoch || s_job.requestId != s_requestId[p] || s_job.settings != settingsKey() ||
        s_job.w != w || s_job.h != h ||
        (s_job.reuseBuffers && (!s_sharp || s_w != w || s_h != h)) || !s_job.ok) {
      if (!s_job.ok) s_retryMs = now;
      discardJob();
    } else {
      if (!canSwitch || (s_job.automatic && !idle)) return false;
      if (!s_job.name[0] || wallFs().exists(wallGalleryPath(p, s_job.name))) return true;
      discardJob();
    }
  }
  if (jobState() != JOB_IDLE || !themeUsesWallpaper(dispSettings.theme) || !canSwitch) return false;
  if (s_retryMs && now - s_retryMs < 30000) return false;
  initGallery(p);
  if (s_request[p] && s_automatic[p] && !idle) return false;
  if (!s_request[p]) {
    if (!automatic) return false;
    if (!idle || !dispSettings.wallEvery || now - s_changedMs < (uint32_t)dispSettings.wallEvery * 60000)
      return false;
    if (now - s_scanMs < 1000) return false;
    s_scanMs = now;
    if (wallFiles(p).size() < 2) return false;
    wallNext(p);
    s_automatic[p] = true;
  }
  WallJob job = {};
  job.w = w;
  job.h = h;
  const bool cinematic = themeUsesModernLayout(dispSettings.theme);
  job.blur = cinematic ? 0 : dispSettings.glassBlur;
  job.dim = cinematic && !themeUsesGlassMode(dispSettings.theme) ? std::max<int>(dispSettings.glassDim, 35) : dispSettings.glassDim;
  job.settings = settingsKey();
  job.epoch = s_epoch;
  job.requestId = s_requestId[p];
  job.automatic = s_automatic[p];
  strlcpy(job.name, s_requested[p].c_str(), sizeof(job.name));
  strlcpy(job.path, wallGalleryPath(p, job.name).c_str(), sizeof(job.path));
#if defined(BOARD_IS_JC8012P4A1C)
  job.reuseBuffers = job.name[0] && s_sharp && s_w == w && s_h == h;
#endif
  portENTER_CRITICAL(&s_jobMux);
  s_job = job;
  s_jobState = JOB_RUNNING;
  portEXIT_CRITICAL(&s_jobMux);
  if (!wallRunWorker(wallJob, wallOnSd() && job.name[0], "deck_wall")) {
    portENTER_CRITICAL(&s_jobMux);
    s_jobState = JOB_IDLE;
    portEXIT_CRITICAL(&s_jobMux);
    s_retryMs = now;
    return false;
  }
  s_request[p] = false;
  return false;
}

void wallCommit() {
  if (jobState() != JOB_READY) return;
  const uint32_t start = millis();
  const bool reused = s_job.reuseBuffers;
  const bool installed = installImage(s_job);
  discardJob();
  if (installed) debugSerial.printf("Deck: wallpaper swap %lu ms%s\n", (unsigned long)(millis() - start),
                                    reused ? " (reused pixel buffers)" : "");
  else {
    s_retryMs = millis();
    debugSerial.println("Deck: wallpaper commit decode failed");
  }
}

void wallRestartTimer() { s_changedMs = millis(); }

}  // namespace deck
