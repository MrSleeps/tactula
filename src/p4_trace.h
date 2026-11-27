#pragma once

#include <stdint.h>

#if defined(BOARD_IS_JC8012P4A1C)
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include "debug_log.h"
#include "p4_board.h"
#endif

namespace p4Trace {

enum class Stage : uint8_t {
    Wifi, Web, Ota, Backlight, Daily, PrinterService, PrinterUi, SystemScreen, Mqtt,
    TileImages, Wallpaper, HomeAssistant, Layout, Render, Snapshots, UiHousekeeping,
    ClimateHttp, Diagnostics, HeapInspect, DiagnosticOutput, FaultOutput, Count
};
enum class Worker : uint8_t { PanelBuild, PanelSend, WallpaperDecode, HaRegistry, Count };
enum class HttpPhase : uint8_t { Client, Auth, Handler, Upload, Write, Cleanup };

#if defined(BOARD_IS_JC8012P4A1C)
struct StageStat {
    uint32_t calls;
    uint32_t maxUs;
    uint32_t startedMs;
};

inline const char* stageName(unsigned i) {
    static constexpr const char* names[] = {
        "wifi", "web", "ota", "backlight", "daily", "printer_svc", "printer_ui", "system", "mqtt",
        "icons", "wallpaper", "ha", "layout", "lvgl", "snapshots", "ui_misc", "climate_http",
        "diagnostics", "heap_inspect", "diag_output", "fault_output"
    };
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<unsigned>(Stage::Count),
                  "Missing P4 trace stage name");
    return names[i];
}
inline const char* workerName(unsigned i) {
    static constexpr const char* names[] = {"panel_build", "panel_send", "wall_decode", "ha_registry"};
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<unsigned>(Worker::Count),
                  "Missing P4 trace worker name");
    return names[i];
}

// Main task owns the interval counters and sampled history. No allocations or
// logging on the normal stage path, and no changes to the scanout ISR.
inline StageStat* stats() {
    static StageStat intervals[static_cast<unsigned>(Stage::Count)] = {};
    return intervals;
}
struct Event {
    uint32_t start, end;  // low 32 bits of esp_timer_get_time(); wrap-safe differences
    uint8_t id, core;
};
constexpr unsigned HistorySize = 256;
struct History {
    Event events[HistorySize]{};
    unsigned next = 0, count = 0;
    uint32_t lastSample = 0;
};
inline History& history() {
    static History h;
    return h;
}
inline void record(Stage stage, uint32_t start, uint32_t end, uint8_t core) {
    auto& h = history();
    // Keep every expensive invocation and sample shorter work at 1 ms.
    // This bounds memory while retaining a useful lead-up to a late frame.
    if (end - start < 100 && h.count && end - h.lastSample < 1000) return;
    h.events[h.next] = {start, end, static_cast<uint8_t>(stage), core};
    h.next = (h.next + 1) % HistorySize;
    if (h.count < HistorySize) ++h.count;
    h.lastSample = end;
}

// Explicit nested main-task scopes do not poll or log on destruction.
class MainScope {
    Stage stage_;
    uint32_t start_;
    uint8_t core_;
public:
    explicit MainScope(Stage stage) : stage_(stage),
        start_(static_cast<uint32_t>(esp_timer_get_time())),
        core_(static_cast<uint8_t>(xPortGetCoreID())) {}
    ~MainScope() {
        record(stage_, start_, static_cast<uint32_t>(esp_timer_get_time()), core_);
    }
    MainScope(const MainScope&) = delete;
    MainScope& operator=(const MainScope&) = delete;
};

// HTTP scopes run only on the main task. Store static route labels, never
// user-supplied URLs, query parameters, headers, credentials or bodies.
// Separate history prevents frequent empty handleClient calls from evicting
// actual requests. No heap allocation, locks, serial writes or ISR changes.
struct HttpEvent {
    const char* route;
    uint32_t start, end;
    HttpPhase phase;
    const char* method;
};
constexpr unsigned HttpHistorySize = 64;
struct HttpHistory {
    HttpEvent events[HttpHistorySize]{};
    unsigned next = 0, count = 0;
    const char* route = "-";
    const char* method = "-";
    unsigned callbacks = 0;
};
inline HttpHistory& httpHistory() {
    static HttpHistory h;
    return h;
}
inline const char* httpPhaseName(HttpPhase phase) {
    switch (phase) {
        case HttpPhase::Client: return "handle_client";
        case HttpPhase::Auth: return "auth";
        case HttpPhase::Handler: return "handler";
        case HttpPhase::Upload: return "upload";
        case HttpPhase::Write: return "socket_write";
        case HttpPhase::Cleanup: return "cleanup";
    }
    return "unknown";
}
class HttpScope {
    HttpPhase phase_;
    uint32_t start_;
    const char* previousRoute_;
    const char* previousMethod_;
    unsigned callbacksAtStart_;
public:
    explicit HttpScope(HttpPhase phase, const char* route = nullptr, const char* method = nullptr)
        : phase_(phase), start_(static_cast<uint32_t>(esp_timer_get_time())) {
        auto& h = httpHistory();
        previousRoute_ = h.route;
        previousMethod_ = h.method;
        callbacksAtStart_ = h.callbacks;
        if (route) h.route = route;
        if (method) h.method = method;
        if (phase == HttpPhase::Auth || phase == HttpPhase::Handler || phase == HttpPhase::Upload)
            ++h.callbacks;
    }
    ~HttpScope() {
        const uint32_t end = static_cast<uint32_t>(esp_timer_get_time());
        auto& h = httpHistory();
        // Keep every registered request phase; only retain slow client/cleanup
        // and socket writes, so idle loops do not fill the ring.
        const bool client = phase_ == HttpPhase::Client;
        const bool registered = phase_ == HttpPhase::Auth || phase_ == HttpPhase::Handler ||
                                phase_ == HttpPhase::Upload;
        if (registered || end - start_ >= 1000U || (client && h.callbacks != callbacksAtStart_)) {
            const char* label = client ?
                (h.callbacks != callbacksAtStart_ ? "(callback dispatched)" : "(no handler dispatched)") :
                h.route;
            h.events[h.next] = {label, start_, end, phase_, h.method};
            h.next = (h.next + 1) % HttpHistorySize;
            if (h.count < HttpHistorySize) ++h.count;
        }
        h.route = previousRoute_;
        h.method = previousMethod_;
    }
    HttpScope(const HttpScope&) = delete;
    HttpScope& operator=(const HttpScope&) = delete;
};

enum class Mode : uint8_t { Verbose, FaultsOnly, Silent };
inline Mode& mode() {
    static Mode value = Mode::FaultsOnly;
    return value;
}
inline const char* modeName() {
    switch (mode()) {
        case Mode::Verbose: return "verbose";
        case Mode::Silent: return "silent";
        default: return "faults";
    }
}
inline bool verbose() { return mode() == Mode::Verbose; }

// Independently instrument selected worker tasks, including the other CPU.
// Each slot has one producer; the short lock is only used at worker boundaries
// and when the main task copies a fault report.
constexpr unsigned WorkerHistorySize = 8;
struct WorkerState {
    Event events[WorkerHistorySize]{};
    unsigned next = 0, count = 0;
    Event active{};
    bool running = false;
};
inline WorkerState* workers() {
    static WorkerState slots[static_cast<unsigned>(Worker::Count)]{};
    return slots;
}
inline portMUX_TYPE& workerMux() {
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    return mux;
}
class WorkerScope {
    unsigned id_;
public:
    explicit WorkerScope(Worker worker) : id_(static_cast<unsigned>(worker)) {
        const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
        const uint8_t core = static_cast<uint8_t>(xPortGetCoreID());
        portENTER_CRITICAL(&workerMux());
        auto& slot = workers()[id_];
        slot.active = {now, now, static_cast<uint8_t>(id_), core};
        slot.running = true;
        portEXIT_CRITICAL(&workerMux());
    }
    ~WorkerScope() {
        const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
        portENTER_CRITICAL(&workerMux());
        auto& slot = workers()[id_];
        slot.active.end = now;
        slot.events[slot.next] = slot.active;
        slot.next = (slot.next + 1) % WorkerHistorySize;
        if (slot.count < WorkerHistorySize) ++slot.count;
        slot.running = false;
        portEXIT_CRITICAL(&workerMux());
    }
    WorkerScope(const WorkerScope&) = delete;
    WorkerScope& operator=(const WorkerScope&) = delete;
};

inline void reportHeap() {
    constexpr uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    constexpr uint32_t psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    unsigned intFree, intLargest, psFree, psLargest;
    {
        MainScope scope(Stage::HeapInspect);
        intFree = heap_caps_get_free_size(internal);
        intLargest = heap_caps_get_largest_free_block(internal);
        psFree = heap_caps_get_free_size(psram);
        psLargest = heap_caps_get_largest_free_block(psram);
    }
    MainScope scope(Stage::DiagnosticOutput);
    debugSerial.printf("Deck: P4 heap internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u\n",
        intFree, intLargest, psFree, psLargest);
}
inline bool overlaps(const Event& e, uint32_t first, uint32_t last) {
    return static_cast<int32_t>(e.end - first) >= 0 &&
           static_cast<int32_t>(last - e.start) >= 0;
}
inline void printEvent(const char* source, const char* name, const Event& e,
                       uint32_t fault, bool active) {
    debugSerial.printf("Deck: P4 fault %s=%s core=%u start_rel_us=%ld end_rel_us=%ld duration_us=%u%s\n",
        source, name, (unsigned)e.core, (long)static_cast<int32_t>(e.start - fault),
        (long)static_cast<int32_t>(e.end - fault), (unsigned)(e.end - e.start),
        active ? " still_running_at_capture" : "");
}
struct FaultPoll {
    p4Board::ScanoutTiming interval{};
    uint32_t lastPoll = 0, lastDump = 0, underruns = 0, suppressed = 0;
    uint32_t totalLate = 0, totalUnderruns = 0, worstGap = 0, lastFaultUs = 0;
    uint32_t modeStartedMs = 0;
    p4Board::ScanoutTiming pending{};
    uint32_t pendingUnderruns = 0;
    bool initialized = false, dumped = false;
};
inline FaultPoll& faultPoll() {
    static FaultPoll p;
    return p;
}
inline void dumpFault(uint32_t now, const p4Board::ScanoutTiming& timing,
                      uint32_t underruns, uint32_t totalUnderruns, uint32_t suppressed) {
    MainScope output(Stage::FaultOutput);
    const uint32_t fault = timing.lateFrames ? timing.lastLateUs : now;
    // The timestamp marks DMA restart, not the beginning of the interruption.
    // If several late frames arrived between polls, the largest gap may not
    // belong to the last event; include that limitation in the summary.
    const uint32_t first = fault - (timing.lateFrames ? timing.worstGapUs : 20000U) - 10000U;
    const uint32_t last = fault + 5000U;
    WorkerState copy[static_cast<unsigned>(Worker::Count)];
    portENTER_CRITICAL(&workerMux());
    for (unsigned i = 0; i < static_cast<unsigned>(Worker::Count); ++i) copy[i] = workers()[i];
    portEXIT_CRITICAL(&workerMux());

    debugSerial.printf("Deck: P4 SCANOUT FAULT t_us32=%u gap_us=%u late_frames=%u underrun_delta=%u underrun_total=%u capture_delay_us=%u suppressed=%u%s\n",
        (unsigned)fault, (unsigned)timing.worstGapUs, (unsigned)timing.lateFrames,
        (unsigned)underruns, (unsigned)totalUnderruns, (unsigned)(now - fault),
        (unsigned)suppressed, timing.lateFrames > 1 ? " multiple_events_window_approximate" : "");
    auto& h = history();
    unsigned printed = 0, matching = 0;
    // Newest first, with a bounded serial burst even under heavy UI activity.
    for (unsigned i = 0; i < h.count; ++i) {
        const Event& e = h.events[(h.next + HistorySize - 1 - i) % HistorySize];
        if (!overlaps(e, first, last)) continue;
        ++matching;
        if (printed < 16) {
            printEvent("main", stageName(e.id), e, fault, false);
            ++printed;
        }
    }
    if (matching > printed)
        debugSerial.printf("Deck: P4 fault main_history_truncated=%u\n", matching - printed);
    if (!matching) debugSerial.println("Deck: P4 fault no sampled main stage in window");
    for (unsigned id = 0; id < static_cast<unsigned>(Worker::Count); ++id) {
        const auto& slot = copy[id];
        if (slot.running) {
            Event e = slot.active;
            e.end = now;
            if (overlaps(e, first, last)) printEvent("worker", workerName(id), e, fault, true);
        }
        for (unsigned i = 0; i < slot.count; ++i) {
            const Event& e = slot.events[(slot.next + WorkerHistorySize - 1 - i) % WorkerHistorySize];
            if (overlaps(e, first, last)) printEvent("worker", workerName(id), e, fault, false);
        }
    }
    const auto& http = httpHistory();
    unsigned httpMatches = 0;
    for (unsigned i = 0; i < http.count; ++i) {
        const auto& e = http.events[(http.next + HttpHistorySize - 1 - i) % HttpHistorySize];
        if (static_cast<int32_t>(e.end - first) < 0 || static_cast<int32_t>(last - e.start) < 0) continue;
        if (httpMatches++ >= 16) continue;
        debugSerial.printf("Deck: P4 fault http=%s method=%s route=%s start_rel_us=%ld end_rel_us=%ld duration_us=%u\n",
            httpPhaseName(e.phase), e.method, e.route,
            (long)static_cast<int32_t>(e.start - fault), (long)static_cast<int32_t>(e.end - fault),
            (unsigned)(e.end - e.start));
    }
    if (httpMatches > 16)
        debugSerial.printf("Deck: P4 fault http_history_truncated=%u\n", httpMatches - 16);
    if (!httpMatches) debugSerial.println("Deck: P4 fault no HTTP phase recorded in window");
    if (verbose()) reportHeap();
}

// Main-task only. Drain the existing ISR counters frequently, then aggregate
// them back into the original ten-second report. Never print in the ISR.
inline void poll(uint32_t now, bool force = false, bool allowOutput = true) {
    auto& p = faultPoll();
    if (!force && p.initialized && now - p.lastPoll < 5000U) return;
    p.lastPoll = now;
    const auto timing = p4Board::takeScanoutTiming();
    const uint32_t total = p4Board::scanoutUnderruns();
    // The ISR may run between the stage-end timestamp and counter collection.
    // Use a fresh timestamp to avoid unsigned capture-delay underflow.
    now = static_cast<uint32_t>(esp_timer_get_time());
    const uint32_t delta = p.initialized ? total - p.underruns : 0;
    p.initialized = true;
    p.underruns = total;
    if (timing.worstGapUs > p.interval.worstGapUs) p.interval.worstGapUs = timing.worstGapUs;
    p.interval.lateFrames += timing.lateFrames;
    if (timing.lateFrames) p.interval.lastLateUs = timing.lastLateUs;
    p.totalLate += timing.lateFrames;
    p.totalUnderruns += delta;
    if (timing.worstGapUs > p.worstGap) p.worstGap = timing.worstGapUs;
    if (timing.lateFrames || delta) {
        p.lastFaultUs = timing.lateFrames ? timing.lastLateUs : now;
        if (mode() != Mode::Silent) {
            if (timing.worstGapUs > p.pending.worstGapUs) p.pending.worstGapUs = timing.worstGapUs;
            p.pending.lateFrames += timing.lateFrames;
            if (timing.lateFrames) p.pending.lastLateUs = timing.lastLateUs;
            p.pendingUnderruns += delta;
        }
    }
    if (!allowOutput || mode() == Mode::Silent ||
        (!p.pending.lateFrames && !p.pendingUnderruns)) return;
    const auto pending = p.pending;
    const auto pendingUnderruns = p.pendingUnderruns;
    p.pending = {};
    p.pendingUnderruns = 0;
    // Avoid serial-flood feedback if the display is repeatedly failing.
    if (p.dumped && now - p.lastDump < 1000000U) {
        ++p.suppressed;
        return;
    }
    p.dumped = true;
    p.lastDump = now;
    const auto suppressed = p.suppressed;
    p.suppressed = 0;
    dumpFault(now, pending, pendingUnderruns, total, suppressed);
}
inline p4Board::ScanoutTiming takeInterval() {
    poll(static_cast<uint32_t>(esp_timer_get_time()), true, false);
    auto& p = faultPoll();
    const auto result = p.interval;
    p.interval = {};
    if (p.suppressed) {
        if (verbose()) {
            MainScope output(Stage::DiagnosticOutput);
            debugSerial.printf("Deck: P4 fault reports rate_limited=%u\n", (unsigned)p.suppressed);
        }
        p.suppressed = 0;
    }
    return result;
}
// Main-task only, no flash writes or reboot. Reset test counters on mode
// changes so equal-duration A/B runs can be compared in Maintenance.
inline void resetCounters() {
    poll(static_cast<uint32_t>(esp_timer_get_time()), true, false);
    auto& p = faultPoll();
    p.totalLate = p.totalUnderruns = p.worstGap = p.lastFaultUs = p.suppressed = 0;
    p.interval = {};
    p.pending = {};
    p.pendingUnderruns = 0;
    p.dumped = false;
    p.modeStartedMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
}
inline void setMode(Mode next) {
    if (mode() == next) return;
    mode() = next;
    resetCounters();
}
inline void clearStats() {
    for (unsigned i = 0; i < static_cast<unsigned>(Stage::Count); ++i) stats()[i] = {};
}
#else
class HttpScope {
public:
    explicit HttpScope(HttpPhase, const char* = nullptr, const char* = nullptr) {}
};
class WorkerScope {
public:
    explicit WorkerScope(Worker) {}
};
#endif

template<typename Work>
inline void run(Stage stage, Work&& work) {
#if defined(BOARD_IS_JC8012P4A1C)
    const int64_t start = esp_timer_get_time();
    const uint8_t core = static_cast<uint8_t>(xPortGetCoreID());
    work();
    const int64_t end = esp_timer_get_time();
    auto& stat = stats()[static_cast<unsigned>(stage)];
    ++stat.calls;
    const auto elapsed = static_cast<uint32_t>(end - start);
    if (elapsed > stat.maxUs) {
        stat.maxUs = elapsed;
        stat.startedMs = static_cast<uint32_t>(start / 1000);
    }
    record(stage, static_cast<uint32_t>(start), static_cast<uint32_t>(end), core);
    poll(static_cast<uint32_t>(end));
#else
    (void)stage;
    work();
#endif
}

#if defined(BOARD_IS_JC8012P4A1C)
inline void report(uint32_t now) {
    char line[640];
    size_t used = snprintf(line, sizeof(line), "Deck: P4 stages t=%ums (calls/max_us@start_ms)",
                           (unsigned)now);
    for (unsigned i = 0; i < static_cast<unsigned>(Stage::Count); ++i) {
        const auto stat = stats()[i];
        stats()[i] = {};
        if (!stat.calls || used >= sizeof(line) - 1) continue;
        used += snprintf(line + used, sizeof(line) - used, " %s=%u/%u@%u", stageName(i),
                         (unsigned)stat.calls, (unsigned)stat.maxUs, (unsigned)stat.startedMs);
    }
    {
        MainScope scope(Stage::DiagnosticOutput);
        debugSerial.println(line);
    }
    reportHeap();
}
#endif

}  // namespace p4Trace
