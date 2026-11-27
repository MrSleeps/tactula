(function () {
  "use strict";
  const $ = (id) => document.getElementById(id),
    qa = (s, r = document) => Array.from(r.querySelectorAll(s));
  let printerAvailable = false;
  let printerConfigLoaded = false;
  let config = null,
    dirty = false,
    previewBusy = false,
    toastTimer = 0,
    populating = false,
    dragPayload = null;
  function esc(v) {
    return String(v == null ? "" : v).replace(
      /[&<>"']/g,
      (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c],
    );
  }
  function fmtBytes(n) {
    n = Number(n) || 0;
    return n > 1048576 ? (n / 1048576).toFixed(1) + " MB" : Math.round(n / 1024) + " KB";
  }
  function fmtUptime(s) {
    s = Number(s) || 0;
    const d = Math.floor(s / 86400);
    s %= 86400;
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    return (d ? d + "d " : "") + h + "h " + m + "m";
  }
  function showToast(message, error) {
    const t = $("toast");
    t.textContent = message;
    t.className = "toast show" + (error ? " error" : "");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => (t.className = "toast"), 4200);
  }
  async function getJson(url, options) {
    const r = await fetch(url, options);
    let data = {};
    try {
      data = await r.json();
    } catch (e) {}
    if (!r.ok) throw new Error(data.message || data.error || "Request failed: " + r.status);
    return data;
  }

  const scanoutHold = $("scanoutHold");
  const scanoutReplay = $("scanoutReplay");
  const renderOnly = $("renderOnly");
  const partialScanout = $("partialScanout");
  const cpuScanout = $("cpuScanout");
  const pauseSnapshots = $("pauseSnapshots");
  const scanoutTestButtons = [scanoutHold, scanoutReplay, renderOnly, partialScanout, cpuScanout, pauseSnapshots].filter(Boolean);
  function setScanoutTestsDisabled(disabled) {
    scanoutTestButtons.forEach((button) => { button.disabled = disabled; });
  }
  const scanoutStop = $("scanoutStop");
  let scanoutTimer = null, scanoutRequestBusy = false;
  async function runScanoutTest(mode) {
    if (scanoutRequestBusy) return;
    scanoutRequestBusy = true;
    scanoutStop.disabled = true;
    const status = $("scanoutHoldStatus");
    const snapshots = mode === "snapshots";
    setScanoutTestsDisabled(true);
    try {
      await getJson("/api/deck/scanout-hold", {
        method: "POST",
        body: new URLSearchParams({ mode, replay: mode === "replay" ? "1" : "0" }),
      });
      status.textContent = snapshots
        ? "Leave the dashboard idle and watch for flashes. Live updates continue."
        : mode === "render-only"
        ? "Watch for flashes while the displayed picture stays still and the dashboard keeps rendering."
        : mode === "cpu-scanout"
        ? "Watch for flashes while the dashboard stays live. Normal scanout resumes in 2 minutes."
        : mode === "partial-scanout"
        ? "Watch for flashes on the same page. Full-frame scanout resumes automatically."
        : "Watch the screen for a flash. The dashboard will resume automatically.";
      clearTimeout(scanoutTimer);
      scanoutTimer = setTimeout(() => {
        scanoutTimer = null;
        status.textContent = "Display test finished.";
        setScanoutTestsDisabled(false);
      }, snapshots || mode === "cpu-scanout" ? 120000 : 60000);
    } catch (error) {
      status.textContent = error.message;
      setScanoutTestsDisabled(false);
    } finally {
      scanoutRequestBusy = false;
      scanoutStop.disabled = false;
    }
  }
  scanoutStop.addEventListener("click", async () => {
    if (scanoutRequestBusy) return;
    scanoutRequestBusy = true;
    scanoutStop.disabled = true;
    setScanoutTestsDisabled(true);
    try {
      await getJson("/api/deck/scanout-hold", {
        method: "POST", body: new URLSearchParams({mode: "stop"})
      });
      clearTimeout(scanoutTimer);
      scanoutTimer = null;
      $("scanoutHoldStatus").textContent = "Normal display operation requested.";
    } catch (error) {
      $("scanoutHoldStatus").textContent = error.message;
    } finally {
      scanoutRequestBusy = false;
      scanoutStop.disabled = false;
      setScanoutTestsDisabled(scanoutTimer !== null);
    }
  });
  scanoutHold.addEventListener("click", () => runScanoutTest("hold"));
  scanoutReplay.addEventListener("click", () => runScanoutTest("replay"));
  renderOnly.addEventListener("click", () => runScanoutTest("render-only"));
  cpuScanout?.addEventListener("click", () => runScanoutTest("cpu-scanout"));
  partialScanout?.addEventListener("click", () => runScanoutTest("partial-scanout"));
  pauseSnapshots.addEventListener("click", () => runScanoutTest("snapshots"));

  let p4DiagnosticsAvailable = false;
  let p4DiagnosticsBusy = false;
  async function refreshP4Diagnostics(body) {
    if (!debugModeEnabled || !p4DiagnosticsAvailable || p4DiagnosticsBusy) return;
    p4DiagnosticsBusy = true;
    const select = $("p4DiagnosticsMode");
    select.disabled = $("p4DiagRefresh").disabled = $("p4DiagReset").disabled = true;
    try {
      const data = await getJson("/api/p4/diagnostics", body ? {
        method: "POST", body: new URLSearchParams(body)
      } : { cache: "no-store" });
      select.value = data.mode;
      $("p4DiagLate").textContent = data.lateFrames;
      $("p4DiagUnderruns").textContent = data.underruns;
      $("p4DiagGap").textContent = (data.worstGapUs / 1000).toFixed(3) + " ms";
      const seconds = Math.floor(data.elapsedMs / 1000);
      $("p4DiagElapsed").textContent = Math.floor(seconds / 60) + "m " + seconds % 60 + "s";
      $("p4DiagStatus").textContent = "Counters reset on mode change. Refresh manually after the test.";
    } catch (error) {
      $("p4DiagStatus").textContent = error.message;
    } finally {
      select.disabled = $("p4DiagRefresh").disabled = $("p4DiagReset").disabled = false;
      p4DiagnosticsBusy = false;
    }
  }
  $("p4DiagnosticsMode").addEventListener("change", (e) => refreshP4Diagnostics({mode: e.target.value}));
  $("p4DiagRefresh").addEventListener("click", () => refreshP4Diagnostics());
  $("p4DiagReset").addEventListener("click", () => refreshP4Diagnostics({reset: "1"}));

  let debugModeEnabled = false;
  let debugLogsAvailable = false;
  let debugLogsPaused = false;
  let debugLogsReading = false;
  let debugLogCursor = 0;
  let debugLogDecoder = new TextDecoder();
  const debugMode = $("debugMode");
  const debugModeHint = $("debugModeHint");
  const debugModeRetry = $("debugModeRetry");
  const debugLogStatus = $("debugLogsStatus");
  const debugLogOutput = $("debugLogsOutput");

  function updateDebugVisibility() {
    $("debugLogViewer").classList.toggle("hidden", !debugModeEnabled || !debugLogsAvailable);
    $("p4DiagnosticsCard").classList.toggle("hidden", !debugModeEnabled || !p4DiagnosticsAvailable);
    if (debugModeEnabled && p4DiagnosticsAvailable &&
        document.querySelector(".page.active")?.dataset.page === "debug") refreshP4Diagnostics();
  }

  async function loadDebugStatus() {
    debugMode.disabled = true;
    debugModeRetry.classList.add("hidden");
    debugModeHint.textContent = "Checking debug mode…";
    try {
      const state = await getJson("/api/debug", {
        cache: "no-store", signal: AbortSignal.timeout(5000),
      });
      debugModeEnabled = state.enabled;
      debugLogsAvailable = state.available;
      debugMode.checked = debugModeEnabled;
      debugMode.disabled = false;
      updateDebugVisibility();
      debugModeHint.textContent = state.enabled && !state.available
        ? "Running without log capture. The device could not allocate the log buffer."
        : state.enabled
          ? "Log capture is on. Turning it off restarts the device."
          : "Turn on Debug mode to capture live logs. The device will restart.";
      if (debugLogsAvailable) readDebugLogs();
    } catch {
      debugModeHint.textContent = "Could not read debug mode.";
      debugModeRetry.classList.remove("hidden");
    }
  }
  debugModeRetry.addEventListener("click", loadDebugStatus);

  debugMode.addEventListener("change", async () => {
    const enabled = debugMode.checked;
    debugMode.disabled = true;
    try {
      const response = await getJson("/api/debug", {
        method: "POST",
        body: new URLSearchParams({ enabled: enabled ? "1" : "0" }),
      });
      if (!response.restarting) {
        await loadDebugStatus();
        return;
      }
      debugModeHint.textContent = "Restarting…";
      for (let attempt = 0; attempt < 20; ++attempt) {
        await new Promise((resolve) => setTimeout(resolve, 1500));
        try {
          const state = await getJson("/api/debug", {
            cache: "no-store", signal: AbortSignal.timeout(1500),
          });
          if (state.enabled === enabled) {
            try { localStorage.setItem("sn_page", "logs"); } catch {}
            location.reload();
            return;
          }
        } catch {}
      }
      debugModeHint.textContent = "Waiting for the device. Retry when it is online.";
      debugModeRetry.classList.remove("hidden");
    } catch (e) {
      debugMode.checked = debugModeEnabled;
      debugMode.disabled = false;
      showToast(e.message, true);
    }
  });

  async function readDebugLogs() {
    if (!debugModeEnabled || !debugLogsAvailable || debugLogsPaused || debugLogsReading || document.hidden ||
        document.querySelector(".page.active")?.dataset.page !== "debug") return;
    debugLogsReading = true;
    try {
      const r = await fetch("/api/debug/logs?after=" + debugLogCursor, { cache: "no-store" });
      if (!r.ok) throw new Error("Logs unavailable");
      const cursor = Number(r.headers.get("X-Log-Cursor"));
      const bytes = await r.arrayBuffer();
      if (cursor < debugLogCursor) {
        debugLogOutput.textContent = "";
        debugLogDecoder = new TextDecoder();
      }
      debugLogCursor = cursor;
      if (bytes.byteLength) {
        const nearBottom = debugLogOutput.scrollHeight - debugLogOutput.scrollTop -
            debugLogOutput.clientHeight < 24;
        const text = debugLogDecoder.decode(bytes, { stream: true });
        debugLogOutput.textContent = (debugLogOutput.textContent + text).slice(-24 * 1024);
        if (nearBottom) debugLogOutput.scrollTop = debugLogOutput.scrollHeight;
      }
      debugLogStatus.textContent = debugLogsPaused ? "Paused" : "Live";
    } catch {
      debugLogStatus.textContent = "Connection lost. Retrying…";
    } finally {
      debugLogsReading = false;
    }
  }

  $("debugLogsPause").addEventListener("click", (e) => {
    debugLogsPaused = !debugLogsPaused;
    e.currentTarget.textContent = debugLogsPaused ? "Resume" : "Pause";
    e.currentTarget.setAttribute("aria-pressed", String(debugLogsPaused));
    debugLogStatus.textContent = debugLogsPaused ? "Paused" : "Connecting…";
    if (!debugLogsPaused) readDebugLogs();
  });
  setInterval(readDebugLogs, 2000);

  // Firmware changes are detected through the existing 2-second status poll.
  // Allow 30 seconds for OTA reboot, then reload only while the API is online.
  let observedFirmwareVersion = null;
  let previousUptime = null;
  let awaitingOtaRestart = false;
  let otaReloadNotBefore = 0;
  function waitForOtaRestart() {
    if (awaitingOtaRestart) return;
    awaitingOtaRestart = true;
    otaReloadNotBefore = Date.now() + 30000;
  }

  // Native browser HTTP Basic Auth; no cookies or custom login flow.
  async function loadAuthStatus() {
    try {
      const info = await getJson("/api/auth");
      $("authState").textContent = info.enabled
        ? "Password protection is enabled."
        : "Password protection is disabled (default).";
      $("authSet").textContent = info.enabled ? "Change password" : "Enable authentication";
      $("authDisable").classList.toggle("hidden", !info.enabled);
    } catch (e) {
      showToast("Could not load security settings: " + e.message, true);
    }
  }

  $("authSet").addEventListener("click", async () => {
    const password = $("authPassword").value;
    if (password.length < 8 || password.length > 64) {
      showToast("Password must be 8 to 64 characters.", true);
      return;
    }
    $("authSet").disabled = true;
    try {
      await getJson("/api/auth", {
        method: "POST",
        body: new URLSearchParams({ action: "set", password }),
      });
      $("authPassword").value = "";
      // A full-page navigation reliably triggers the browser's native Basic
      // Auth prompt after enabling or changing the password.
      location.reload();
    } catch (e) {
      showToast(e.message, true);
    } finally {
      $("authSet").disabled = false;
    }
  });

  $("authDisable").addEventListener("click", async () => {
    if (!confirm("Disable password protection for the Tactula web portal?")) return;
    $("authDisable").disabled = true;
    try {
      await getJson("/api/auth", {
        method: "POST",
        body: new URLSearchParams({ action: "disable" }),
      });
      $("authPassword").value = "";
      await loadAuthStatus();
      showToast("Authentication disabled.");
    } catch (e) {
      showToast(e.message, true);
    } finally {
      $("authDisable").disabled = false;
    }
  });

  function applyTheme() {
    const mode = document.documentElement.dataset.mode || "dark",
      accent = document.documentElement.dataset.accent || "purple";
    qa("[data-mode-choice]").forEach((b) => b.classList.toggle("on", b.dataset.modeChoice === mode));
    qa("[data-accent-choice]").forEach((b) => b.classList.toggle("on", b.dataset.accentChoice === accent));
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.content = mode === "dark" ? "#0c1019" : "#f5f6fb";
  }
  qa("[data-mode-choice]").forEach((b) =>
    b.addEventListener("click", () => {
      document.documentElement.dataset.mode = b.dataset.modeChoice;
      localStorage.setItem("sn_mode", b.dataset.modeChoice);
      applyTheme();
    }),
  );
  qa("[data-accent-choice]").forEach((b) =>
    b.addEventListener("click", () => {
      document.documentElement.dataset.accent = b.dataset.accentChoice;
      localStorage.setItem("sn_accent", b.dataset.accentChoice);
      applyTheme();
    }),
  );
  applyTheme();

  function activeForm() {
    const page = document.querySelector(".page.active");
    return page ? page.querySelector(".portal-form") : null;
  }
  function updateSaveBar() {
    const f = activeForm();
    const onHaPage = document.querySelector(".page.active")?.dataset.page === "homeassistant";
    $("saveBar").classList.toggle("hidden", !f || onHaPage);
    const mqttHint = $("mqttUnsavedHint");
    if (mqttHint) mqttHint.textContent = dirty && onHaPage ? "Unsaved MQTT changes" : "Settings saved";
    if (!f) return;
    $("saveBtnText").textContent = f.dataset.saveLabel || "Save settings";
    $("saveMeta").classList.toggle("clean", !dirty);
    $("saveMeta").querySelector(".txt").textContent = dirty ? "Unsaved changes" : "All saved";
  }
  async function showPage(name) {
    const current = document.querySelector(".page.active")?.dataset.page;
    if (current === name) {
      document.documentElement.classList.remove("nav-open");
      $("navToggle").setAttribute("aria-expanded", "false");
      return true;
    }
    if (dirty) {
      if (!confirm("Discard unsaved changes on this page?")) return false;
      if (!(await loadConfig())) return false;
    }
    if (name !== "hardware") stopLedPreview();
    // Entity suggestions load only when the user focuses a relevant input.
    if (name === "hardware") loadAudio();
    if (name === "printer") {
      if (!printerAvailable) return false;
      refreshPrinter();
    }
    if (name === "storage") loadStorageStatus();
    if (name === "icons") loadBedroomIcons();
    if (name === "security") loadAuthStatus();
    qa(".page").forEach((p) => p.classList.toggle("active", p.dataset.page === name));
    if (name === "overview") refreshOverviewBambuConnection();
    if (name === "debug") {
      readDebugLogs();
      updateDebugVisibility();
    }
    qa(".nav-item").forEach((b) => b.classList.toggle("active", b.dataset.nav === name));
    const button = document.querySelector('.nav-item[data-nav="' + name + '"]');
    $("crumb").textContent = button ? button.textContent.replace(/\s+/g, " ").trim() : "tactula";
    document.documentElement.classList.remove("nav-open");
    $("navToggle").setAttribute("aria-expanded", "false");
    updateSaveBar();
    try {
      localStorage.setItem("sn_page", name);
    } catch (e) {}
    return true;
  }
  qa(".nav-item").forEach((b) =>
    b.addEventListener("click", () => showPage(b.dataset.nav)),
  ); /* cross links inside card text, such as "see the Display page". Nothing listens for the hash, so without this they only changed the URL. */
  document.addEventListener("click", (e) => {
    const a = e.target.closest("a[data-page]");
    if (!a) return;
    e.preventDefault();
    showPage(a.dataset.page);
  });
  $("navToggle").addEventListener("click", () => {
    const open = document.documentElement.classList.toggle("nav-open");
    $("navToggle").setAttribute("aria-expanded", open ? "true" : "false");
  });
  $("navScrim").addEventListener("click", () =>
    showPage(document.querySelector(".nav-item.active").dataset.nav),
  );

  function markDirty() {
    if (populating) return;
    dirty = true;
    updateSaveBar();
  }
  qa(".portal-form").forEach((f) => {
    f.addEventListener("input", markDirty);
    f.addEventListener("change", markDirty);
    f.addEventListener("submit", (e) => e.preventDefault());
  });
  window.addEventListener("beforeunload", (e) => {
    if (!dirty) return;
    e.preventDefault();
    e.returnValue = "";
  });
  function setVal(id, value) {
    const e = $(id);
    if (e) e.value = value == null ? "" : value;
  }
  function setCheck(id, value) {
    const e = $(id);
    if (e) e.checked = !!value;
  }
  function updateRangeLabels() {
    const pct = (id) => Math.round((Number($(id).value) * 100) / 255) + "%";
    $("brightnessVal").textContent = pct("brightness");
    $("nightBrightnessVal").textContent = pct("nightBrightness");
    $("nightWakeLevelVal").textContent = pct("nightWakeLevel");
    $("ledBrightnessVal").textContent = pct("ledBrightness");
    $("ledNightBrightnessVal").textContent = pct("ledNightBrightness");
    $("tactulaRadiusVal").textContent = $("tactulaRadius").value + " px";
    $("tactulaPaddingVal").textContent = $("tactulaPadding").value + " px";
    $("glassTintVal").textContent = $("glassTint").value + "%";
    $("glassBlurVal").textContent = $("glassBlur").value === "0" ? "Off" : $("glassBlur").value + " px";
    $("glassDimVal").textContent = $("glassDim").value + "%";
    $("glassOnVal").textContent = $("glassOn").value + "%";
    $("sleepDimVal").textContent = $("sleepDim").value + "%";
    $("hapticStrengthVal").textContent = $("hapticStrength").value + "%";
    $("hapticMsVal").textContent = $("hapticMs").value + " ms";
    $("audioVolumeVal").textContent = $("audioVolume").value + "%";
  }
  [
    "brightness",
    "nightBrightness",
    "nightWakeLevel",
    "ledBrightness",
    "ledNightBrightness",
    "tactulaRadius",
    "tactulaPadding",
    "glassTint",
    "glassBlur",
    "glassDim",
    "glassOn",
    "sleepDim",
    "hapticStrength",
    "hapticMs",
    "audioVolume",
  ].forEach((id) => $(id).addEventListener("input", updateRangeLabels));
  const tactulaPaletteIds = { ocean: [6, 8], ember: [7, 9], amethyst: [10, 13], forest: [11, 14], graphite: [12, 15] };
  const modernThemeIds = Object.values(tactulaPaletteIds).flat();
  const glassThemeIds = Object.values(tactulaPaletteIds).map(ids => ids[1]);
  function updateGlassFields() {
    const theme = $("theme").value;
    const tactula = theme === "3";
    const modern = modernThemeIds.includes(Number(theme));
    const glass = glassThemeIds.includes(Number(theme));
    $("glassLook").classList.toggle("hidden", theme !== "2" && !tactula && !glass);
    $("tactulaLook").classList.toggle("hidden", !tactula);
    // P4 modern Glass is live transparency, not the legacy full-page frost bake.
    $("glassBlurField").classList.toggle("hidden", tactula || modern);
    $("glassAutoTextField").classList.toggle("hidden", modern);
    $("glassLookTitle").textContent = tactula ? "Tactula glass look" : "Glass look";
  }
  $("theme").addEventListener("change", updateGlassFields);
  function updateModernTheme() {
    const ids = tactulaPaletteIds[$("tactulaPalette").value] || tactulaPaletteIds.ocean;
    const glass = $("tactulaGlass").checked;
    $("theme").value = String(ids[glass ? 1 : 0]);
    $("theme").dispatchEvent(new Event("change", { bubbles: true }));
  }
  $("tactulaPalette").addEventListener("change", updateModernTheme);
  $("tactulaGlass").addEventListener("change", updateModernTheme);
  // Animations: the presets as touch, on/off, page, card (as on the deck);
  // switching to Custom starts from the preset that was chosen.
  const FX_SETS = [[0, 0, 0, 0], [1, 1, 1, 1], [2, 2, 2, 2], [3, 3, 3, 3], [4, 4, 4, 1]];
  const FX_IDS = ["fxTouch", "fxToggle", "fxPage", "fxCard"];
  let fxPreset = 0;
  function updateFxFields() {
    const v = Number($("tileFx").value);
    if (v === 5 && !populating) FX_SETS[fxPreset].forEach((x, i) => setVal(FX_IDS[i], x));
    if (v < 5) fxPreset = v;
    $("fxCustom").classList.toggle("hidden", v !== 5);
  }
  $("tileFx").addEventListener("change", updateFxFields);
  for (let h = 0; h < 24; h++) $("dailyHour").add(new Option(String(h).padStart(2, "0") + ":00", h));
  function updateDailyFields() {
    const source = $("wallSource").value;
    const curated = source === "3";
    $("dailyFields").classList.toggle("hidden", source === "0");
    $("spotlightSourceField").classList.toggle("hidden", curated);
    $("dailyMirror").disabled = curated || source === "0";
    for (const id of ["dailyFeedId", "dailyFeedBase"]) {
      $(id + "Field").classList.toggle("hidden", !curated);
      $(id).disabled = !curated;
    }
  }
  $("wallSource").addEventListener("change", updateDailyFields);
  function updateNightFields() {
    $("nightScheduleFields").classList.toggle("hidden", !$("nightEnabled").checked);
  }
  $("nightEnabled").addEventListener("change", updateNightFields);
  function updateSleepFields() {
    $("sleepDimField").classList.toggle("hidden", $("sleepMode").value !== "dim");
    $("sleepFrameFields").classList.toggle("hidden", $("sleepMode").value !== "frame");
  }
  $("sleepMode").addEventListener("change", updateSleepFields);
  function updateNightWakeFields() {
    $("nightWakeFields").classList.toggle("hidden", !$("nightWake").checked);
  }
  $("nightWake").addEventListener("change", updateNightWakeFields);
  /* Entity suggestions from the list cached on the device ("Update from HA" in Pages & tiles). */
  document.addEventListener("focusin", (e) => {
    if (e.target.matches?.("input[list=presenceList], input[list=ledEntityList], input[list=frameWeatherList]")) loadEntityLists();
  });
  let entityListsLoaded = false;
  async function loadEntityLists() {
    if (entityListsLoaded) return;
    entityListsLoaded = true;
    try {
      const r = await getJson("/api/ha/entities");
      const fill = (id, domains) =>
        ($(id).innerHTML = (r.entities || [])
          .filter((e) => domains.includes(e.id.split(".")[0]))
          .map((e) => '<option value="' + e.id + '">' + (e.name || "").replace(/[<&"]/g, "") + "</option>")
          .join(""));
      fill("presenceList", ["binary_sensor", "person", "device_tracker", "input_boolean", "switch"]);
      fill("ledEntityList", ["input_boolean", "input_number", "light"]);
      fill("frameWeatherList", ["weather"]);
    } catch (e) {
      entityListsLoaded = false;
    }
  }
  function updateLedFields() {
    $("ledFields").classList.toggle("hidden", !$("ledEnabled").checked);
    $("ledNightFields").classList.toggle("hidden", !$("ledNightEnabled").checked);
  }
  /* Live LED preview: the slider drives the real LED while the page is open, so
   a brightness is chosen by looking at the LED rather than by saving and
   re-saving. The device forgets the preview on the next save or on stop. */
  let ledPreviewTimer = null,
    ledPreviewOn = false;
  function sendLedPreview() {
    const fd = new FormData();
    if ($("ledEnabled").checked) fd.append("ledEnabled", "1");
    fd.append("ledPin", $("ledPin").value);
    fd.append("ledBrightness", $("ledBrightness").value);
    ledPreviewOn = true;
    fetch("/led/preview", { method: "POST", body: fd }).catch(() => {});
  }
  function queueLedPreview() {
    if (populating) return;
    clearTimeout(ledPreviewTimer);
    ledPreviewTimer = setTimeout(sendLedPreview, 120);
  }
  function stopLedPreview() {
    clearTimeout(ledPreviewTimer);
    if (!ledPreviewOn) return;
    ledPreviewOn = false;
    const fd = new FormData();
    fd.append("stop", "1");
    fetch("/led/preview", { method: "POST", body: fd }).catch(() => {});
  }
  $("ledEnabled").addEventListener("change", () => {
    updateLedFields();
    queueLedPreview();
  });
  $("ledNightEnabled").addEventListener("change", updateLedFields);
  function updateHapticFields() {
    $("hapticFields").classList.toggle("hidden", !$("hapticEnabled").checked);
  }
  $("hapticEnabled").addEventListener("change", updateHapticFields);
  $("hapticTest").addEventListener("click", async () => {
    const fd = new URLSearchParams();
    ["hapticPin", "hapticStrength", "hapticMs"].forEach((id) => fd.append(id, $(id).value));
    try {
      await getJson("/haptic/test", { method: "POST", body: fd });
    } catch (e) {
      showToast(e.message, true);
    }
  });
  ["ledBrightness", "ledPin"].forEach((id) => $(id).addEventListener("input", queueLedPreview));

  // Speaker (Hardware page): sound pickers, Test buttons, WAV files.
  const AUDIO_PICKS = [];
  let audioFiles = [];
  function updateAudioFields() {
    $("audioFields").classList.toggle("hidden", !$("audioEnabled").checked);
  }
  $("audioEnabled").addEventListener("change", updateAudioFields);
  const audioPickValues = () => AUDIO_PICKS.map((id) => $(id).value);
  function fillAudioPicks(values) {
    AUDIO_PICKS.forEach((id, i) => {
      const sel = $(id),
        v = values[i] || "";
      sel.innerHTML = "";
      sel.add(new Option("Built-in", ""));
      audioFiles.filter((f) => !f.error).forEach((f) => sel.add(new Option(f.name, f.name)));
      if (v && !audioFiles.some((f) => f.name === v && !f.error)) sel.add(new Option(v + " (missing)", v));
      sel.value = v;
    });
  }
  function audioFileText(f) {
    if (f.error) return f.error;
    return (
      (f.ms / 1000).toFixed(1) +
      " s, " +
      f.rate / 1000 +
      " kHz " +
      (f.channels === 2 ? "stereo" : "mono") +
      ", " +
      fmtBytes(f.size)
    );
  }
  function renderAudioFiles() {
    $("audioFiles").innerHTML = audioFiles.length
      ? audioFiles
          .map(
            (f) =>
              '<div class="audio-file"><span class="audio-file-name"><b>' +
              esc(f.name) +
              "</b><small>" +
              esc(audioFileText(f)) +
              '</small></span><span class="audio-file-btns">' +
              (f.error ? "" : '<button type="button" class="btn" data-audio-play="' + esc(f.name) + '">Play</button>') +
              '<button type="button" class="btn" data-audio-del="' +
              esc(f.name) +
              '">Delete</button></span></div>',
          )
          .join("")
      : '<p class="field-hint">No sound files yet. The built-in sounds are used.</p>';
  }
  async function loadAudio(values) {
    try {
      const d = await getJson("/api/audio?t=" + Date.now());
      audioFiles = d.files || [];
      renderAudioFiles();
      const was = populating;
      populating = true;
      fillAudioPicks(values || audioPickValues());
      populating = was;
      const st = d.status || {};
      $("audioState").textContent = !(config && config.audio && config.audio.enabled)
        ? "disabled"
        : st.night_mute
          ? "quiet (night)"
          : "on";
    } catch (e) {
      showToast(e.message, true);
    }
  }
  async function audioTest(kind, file) {
    const fd = new URLSearchParams({ kind: kind, vol: $("audioVolume").value });
    if (file) fd.append("file", file);
    try {
      await getJson("/api/audio/test", { method: "POST", body: fd });
    } catch (e) {
      showToast(e.message, true);
    }
  }
  document.addEventListener("click", async (e) => {
    const t = e.target.closest("[data-audio-test],[data-audio-play],[data-audio-del]");
    if (!t) return;
    if (t.dataset.audioTest) {
      const k = t.dataset.audioTest,
        pick = null;
      audioTest(k, pick ? $(pick).value : "");
    } else if (t.dataset.audioPlay) {
      audioTest("click", t.dataset.audioPlay);
    } else {
      const name = t.dataset.audioDel;
      if (!confirm("Delete " + name + "?")) return;
      try {
        const d = await getJson("/api/audio/delete", { method: "POST", body: new URLSearchParams({ n: name }) });
        showToast(d.message || "Deleted", false);
        if (config && config.audio)
          ["fileFinished", "fileError", "fileCooled"].forEach((k) => {
            if (config.audio[k] === name) config.audio[k] = "";
          });
        await loadAudio(audioPickValues().map((v) => (v === name ? "" : v)));
      } catch (err) {
        showToast(err.message, true);
      }
    }
  });
  $("audioPick").addEventListener("click", () => $("audioFile").click());
  $("audioFile").addEventListener("change", async (e) => {
    e.stopPropagation(); /* a picked file is an upload, not an unsaved form change */
    const f = e.target.files[0];
    e.target.value = "";
    if (!f) return;
    const base = f.name
      .toLowerCase()
      .replace(/\.wave?$/, "")
      .replace(/[^a-z0-9._-]+/g, "-")
      .slice(0, 35);
    const name = (base || "sound") + ".wav";
    const fd = new FormData();
    fd.append("sound", f, name);
    $("audioPick").disabled = true;
    try {
      const d = await getJson("/api/audio/upload?n=" + encodeURIComponent(name), { method: "POST", body: fd });
      showToast(d.message || "Sound stored", false);
      await loadAudio();
    } catch (err) {
      showToast(err.message, true);
    } finally {
      $("audioPick").disabled = false;
    }
  });

  function fillConfig(c) {
    config = c;
    populating = true;
    $("topVersion").textContent = "v" + c.version;
    $("aboutVersion").textContent = "v" + c.version;
    $("aboutBoard").textContent = c.board;
    $("maintVersion").textContent = c.version;
    $("maintBoard").textContent = c.board;
    $("maintIp").textContent = c.ip || "--";
    $("diagBoard").textContent = c.board;
    $("scanoutHoldControls").classList.toggle("hidden", !c.display.cinematicAvailable);
    p4DiagnosticsAvailable = !!c.display.cinematicAvailable;
    updateDebugVisibility();
    setVal("rotation", c.display.rotation);
    $("legacyThemeField").classList.add("hidden");
    $("modernPaletteField").classList.remove("hidden");
    $("modernGlassField").classList.remove("hidden");
    for (const option of $("theme").options) {
      const modern = modernThemeIds.includes(Number(option.value));
      option.disabled = option.hidden = !modern;
    }
    const selectedTheme = Number(c.display.theme ?? 2);
    const migrated = modernThemeIds.includes(selectedTheme) ? selectedTheme :
      (selectedTheme === 0 || selectedTheme === 1 || selectedTheme === 3) ? 6 : 8;
    setVal("theme", migrated);
    setVal("tactulaPalette", Object.keys(tactulaPaletteIds).find(key => tactulaPaletteIds[key].includes(migrated)) || "ocean");
    setCheck("tactulaGlass", glassThemeIds.includes(migrated));
    setVal("tileFx", c.display.tileFx || 0);
    FX_IDS.forEach((id) => setVal(id, c.display[id] || 0));
    setVal("brightness", c.display.brightness);
    setVal("wallEvery", c.display.wallEvery ?? 5);
    setVal("wallOrder", c.display.wallOrder ?? 0);
    const pictureSource = c.display.wallSource ?? (c.display.wallDaily ? 1 : 0);
    setVal("wallSource", pictureSource);
    setCheck("sdCard", c.display.sdCard ?? false);
    $("sdSave").disabled = true;
    setVal("dailyMax", c.display.dailyMax ?? 100);
    setWallSource(pictureSource === 3 ? 1 : pictureSource);
    setVal("dailyMirror", c.display.dailyMirror ?? 0);
    setVal("dailyFeedId", c.display.dailyFeedId ?? "");
    setVal("dailyFeedBase", c.display.dailyFeedBase ?? "");
    setVal("dailyOrient", c.display.dailyOrient ?? 0);
    setVal("dailyCount", c.display.dailyCount ?? 12);
    setVal("dailyHour", c.display.dailyHour ?? 3);
    updateDailyFields();
    dailyStatus();
    setVal("tactulaRadius", c.display.tactulaRadius ?? 22);
    setVal("tactulaPadding", c.display.tactulaPadding ?? 9);
    setVal("glassTint", c.display.glassTint);
    setVal("glassBlur", c.display.glassBlur);
    setVal("glassDim", c.display.glassDim);
    setVal("glassOn", c.display.glassOn || 80);
    setCheck("glassAutoText", c.display.glassAutoText === true);
    updateGlassFields();
    updateFxFields();
    {
      const nd = $("navDeck");
      if (nd) nd.classList.toggle("hidden", !c.deck);
      const ni = $("navIcons");
      if (ni) ni.classList.toggle("hidden", !c.deck);
      if (c.deck && !window.snDeck) {
        window.snDeck = 1;
        const s = document.createElement("script");
        s.src = "/deck.js";
        document.head.appendChild(s);
      }
    }
    const b = c.backlight || {};
    setCheck("nightEnabled", b.nightEnabled);
    setVal("nightBrightness", b.nightBrightness == null ? 32 : b.nightBrightness);
    setVal("nightStart", b.nightStart || "22:00");
    setVal("nightEnd", b.nightEnd || "07:00");
    updateNightFields();
    const l = c.led || {};
    setCheck("ledEnabled", l.enabled);
    setVal("ledPin", l.pin == null ? 0 : l.pin);
    setVal("ledBrightness", l.brightness == null ? 160 : l.brightness);
    setCheck("ledNightEnabled", l.nightEnabled);
    setVal("ledNightBrightness", l.nightBrightness == null ? 24 : l.nightBrightness);
    setVal("ledNightStart", l.nightStart || "22:00");
    setVal("ledNightEnd", l.nightEnd || "07:00");
    setCheck("ledFollowDisplay", l.followDisplay);
    $("ledAllowedPins").textContent = (l.allowedPins || []).join(", ") || "none";
    const hp = c.haptic || {};
    setCheck("hapticEnabled", hp.enabled);
    setVal("hapticPin", hp.pin == null ? 0 : hp.pin);
    setVal("hapticStrength", hp.strength == null ? 100 : hp.strength);
    setVal("hapticMs", hp.pulseMs == null ? 40 : hp.pulseMs);
    $("hapticAllowedPins").textContent = $("ledAllowedPins").textContent;
    updateHapticFields();
    const au = c.audio || {};
    setCheck("audioEnabled", au.enabled);
    setVal("audioVolume", au.volume == null ? 70 : au.volume);
    setCheck("audioNight", au.nightMute !== false);
    setCheck("audioClick", au.click);

    updateAudioFields();
    $("audioState").textContent = au.enabled ? "on" : "disabled";
    setVal("ledEntity", l.entity || "");
    updateLedFields();
    window.snLedEntity = l.entity || "";
    if (window.dkLedSync) window.dkLedSync();
    const p = c.presence || {};
    setVal("sleepMin", p.sleep || 0);
    setVal("sleepMode", p.frame ? "frame" : p.dim ? "dim" : "off");
    setVal("frameEvery", p.frameEvery || 5);
    setCheck("frameClock", p.frameClock !== false);
    setVal("frameClockSize", p.frameClockLarge ? "large" : "small");
    setCheck("frameWeatherIcon", p.frameWeatherIcon !== false);
    setVal("frameWeatherEntity", p.frameWeatherEntity || "");
    setVal("frameWeatherTemperature", String(p.frameWeatherTemperature || 0));
    setCheck("frameClockBackground", p.frameClockBackground !== false);
    setCheck("frameAnim", p.frameAnim !== false);
    setVal("sleepDim", p.dim || 10);
    for (let i = 0; i < 4; i++) setVal("presence" + i, (p.entities || [])[i] || "");
    setCheck("nightWake", p.nightWake);
    setVal("nightWakeLevel", p.nightWakeLevel == null ? 128 : p.nightWakeLevel);
    updateSleepFields();
    updateNightWakeFields();
    const adv = c.advanced || {};
    const mqtt = c.mqtt || {};
    setCheck("mqttEnabled", mqtt.enabled);
    setVal("mqttHost", mqtt.host || "");
    setVal("mqttPort", mqtt.port || 1883);
    setVal("mqttUser", mqtt.user || "");
    setVal("mqttPass", "");
    $("mqttPass").placeholder = mqtt.has_pass ? "unchanged" : "";
    setCheck("mqttClearPass", false);
    setVal("mqttPrefix", mqtt.prefix || "homeassistant");
    setVal("mqttDeviceName", mqtt.deviceName || "tactula");
    setVal("timeFormat", c.clock.use24h ? "24" : "12");
    setVal("timezone", c.clock.timezone || "");
    const tz = qa("#timezonePreset option").find((o) => o.value === (c.clock.timezone || ""));
    setVal("timezonePreset", tz ? tz.value : "");
    setVal("wifiSsid", c.network.ssid || "");
    setVal("wifiPass", "");
    setVal("hostname", c.network.hostname || "tactula");
    setCheck("mdns", c.network.mdns);
    setCheck("showIp", c.network.showIp);
    setVal("ipMode", c.network.dhcp ? "dhcp" : "static");
    setVal("staticIp", c.network.staticIp || "");
    setVal("gateway", c.network.gateway || "");
    setVal("subnet", c.network.subnet || "");
    setVal("dns", c.network.dns || "");
    $("staticFields").classList.toggle("hidden", c.network.dhcp);
    updateRangeLabels();
    populating = false;
    dirty = false;
    updateSaveBar();
  }

  $("ipMode").addEventListener("change", () =>
    $("staticFields").classList.toggle("hidden", $("ipMode").value !== "static"),
  );
  $("timezonePreset").addEventListener("change", () => {
    if ($("timezonePreset").value) setVal("timezone", $("timezonePreset").value);
    markDirty();
  });

  async function loadConfig() {
    try {
      const c = await getJson("/api/config?t=" + Date.now());
      fillConfig(c);
      printerAvailable = c.bambu_available === true;
      $("navPrinter").classList.toggle("hidden", !printerAvailable);
      return true;
    } catch (e) {
      showToast(e.message, true);
      return false;
    }
  }
  /* URLSearchParams, NOT FormData: a multipart POST goes through the ESP32 WebServer's _parseForm, which allocates a FIXED RequestArgument[32] for post args and drops the connection past it (ERR_EMPTY_RESPONSE, no crash). The layout form alone posts 42 fields for 8 slots. Urlencoded uses _parseArguments, which sizes itself from the body. Keep this urlencoded, and keep file uploads (backup import, OTA) on their own handlers. */
  async function saveActive() {
    const f = activeForm();
    if (!f) return false;
    try {
      $("saveBtn").disabled = true;
      const data = await getJson(f.dataset.endpoint, {
        method: "POST",
        body: new URLSearchParams(new FormData(f)),
      });
      ledPreviewOn = false;
      /* the save re-inits the LED, which drops the preview server-side */ dirty = false;
      updateSaveBar();
      showToast(data.message || "Settings saved", false);
      if (data.restarting) {
        $("saveBtnText").textContent = "Restarting...";
        setTimeout(() => location.reload(), 5000);
      } else {
        await loadConfig();
        setTimeout(refreshPreview, 350);
      }
      return true;
    } catch (e) {
      showToast(e.message, true);
      return false;
    } finally {
      $("saveBtn").disabled = false;
    }
  }
  $("mqttSaveInline").addEventListener("click", async () => {
    const button = $("mqttSaveInline");
    button.disabled = true;
    try {
      await saveActive(); // same validated /save/advanced endpoint, no second handler
    } finally {
      button.disabled = false;
    }
  });
  $("saveBtn").addEventListener("click", saveActive);
  $("revertBtn").addEventListener("click", loadConfig);

  function connectionState(id, value, state, detailId, detail) {
    const node = $(id);
    node.textContent = value;
    node.dataset.state = state;
    if (detailId) $(detailId).textContent = detail || "";
  }

  // Poll the existing printer API only on Overview, every 15 seconds rather
  // than with the 2-second general status poll: Bambu status includes AMS
  // details and is substantially heavier than the normal /api/status reply.
  let overviewBambuPolling = false;
  let overviewBambuConfigured = false;
  let overviewBambuSource = "";
  function overviewBambuUnavailable() {
    if (!overviewBambuConfigured) return;
    connectionState("factBambu", "Status unavailable", "error");
    connectionState("factBambuAvailability", "Printer: Unknown", "neutral");
    $("factBambuError").textContent = "Unable to retrieve connection status";
  }
  async function refreshOverviewBambuConnection() {
    if (!printerAvailable || overviewBambuPolling ||
        document.hidden ||
        document.querySelector(".page.active")?.dataset.page !== "overview") return;
    overviewBambuPolling = true;
    try {
      const p = await getJson("/api/office/bambu?t=" + Date.now());
      // An error/overflow response is not proof that configuration was removed.
      if (typeof p.configured !== "boolean") {
        overviewBambuUnavailable();
        return;
      }
      overviewBambuConfigured = p.configured;
      $("factBambuRow").classList.toggle("hidden", !overviewBambuConfigured);
      if (!overviewBambuConfigured) return;
      overviewBambuSource = p.source;
      $("factBambuLabel").textContent = p.display_name || "Bambu printer";
      const connection = p.connection || "unknown";
      const label = connection === "connected" ? "Connected" :
        connection === "connecting" ? "Connecting" :
        connection === "failed" ? "Connection failed" :
        connection === "disconnected" ? "Disconnected" : "Unavailable";
      connectionState("factBambu", label,
        connection === "connected" ? "connected" :
        connection === "connecting" ? "pending" : "error",
        "factBambuDetail", p.source === "ha" ? "Via Home Assistant" :
          p.source === "lan" ? "Via LAN" : "Unknown connection method");
      const availability = p.availability || "unknown";
      const printerLabel = availability === "online" ? "Online" :
        availability === "offline" ? "Offline" :
        availability === "unreachable" ? "Offline / unreachable" : "Unknown";
      connectionState("factBambuAvailability", "Printer: " + printerLabel,
        availability === "online" ? "connected" :
        availability === "offline" ? "neutral" :
        availability === "unreachable" ? "error" : "neutral");
      $("factBambuError").textContent = connection === "failed" ? (p.error || "") : "";
    } catch (error) {
      overviewBambuUnavailable();
    } finally {
      overviewBambuPolling = false;
    }
  }
  setInterval(refreshOverviewBambuConnection, 15000);

  let statusBusy = false, lastStatusPoll = 0;
  async function pollStatus() {
    if (statusBusy || (document.hidden && !awaitingOtaRestart)) return;
    statusBusy = true;
    lastStatusPoll = Date.now();
    try {
      const s = await getJson("/api/status?t=" + Date.now());
      const version = typeof s.version === "string" ? s.version : null;
      if ((observedFirmwareVersion !== null && version && version !== observedFirmwareVersion) ||
          (previousUptime !== null && Number(s.uptime_seconds) + 5 < previousUptime))
        waitForOtaRestart();
      if (Number.isFinite(Number(s.uptime_seconds))) previousUptime = Number(s.uptime_seconds);
      else if (observedFirmwareVersion === null && version) observedFirmwareVersion = version;
      if (awaitingOtaRestart && Date.now() >= otaReloadNotBefore) {
        // The successful status response proves the device is responding.
        location.reload();
        return;
      }
      $("srLed").classList.remove("offline");
      $("srTitle").textContent = s.ap_mode ? "setup access point" : "device online";
      $("srIp").textContent = s.ip || "--";
      const hostname = String(s.hostname || "tactula").replace(/\.local$/i, "");
      $("srHost").textContent = hostname + ".local";
      $("topHostname").textContent = hostname;
      $("topHostname").title = hostname;
      document.title = "tactula | " + hostname;
      const ha = String(s.ha_link || "unknown");
      connectionState("factHa", ha.charAt(0).toUpperCase() + ha.slice(1),
        ha === "connected" ? "connected" :
        ["connecting", "authenticating", "waiting for WiFi"].includes(ha) ? "pending" :
        ha === "not set up" ? "neutral" : "error",
        "factHaDetail", s.ha_url || "");
      if (overviewBambuConfigured && overviewBambuSource === "ha" && ha !== "connected") {
        connectionState("factBambu", "Disconnected", "error");
        connectionState("factBambuAvailability", "Printer: Unknown", "neutral");
        $("factBambuError").textContent = "";
      }
      const mqtt = s.mqtt || {};
      const mqttState = mqtt.state || "off";
      connectionState("factMqtt", mqttState === "off" ? "Disabled" :
        mqttState === "connected" ? "Connected" :
        mqttState === "connecting" ? "Connecting" :
        mqttState === "error" ? "Connection failed" : mqttState,
        mqttState === "connected" ? "connected" :
        mqttState === "connecting" ? "pending" :
        mqttState === "off" ? "neutral" : "error",
        "factMqttDetail", mqttState === "off" ? "MQTT discovery is disabled" :
          mqtt.error || mqtt.broker || "");
      $("mqttStatus").textContent = mqttState + (mqtt.error ? " - " + mqtt.error : "");
      const mem = s.memory || {}, internal = mem.internal || {}, psram = mem.psram || {};
      // A missing status field is unknown, not 0 KB (e.g. older firmware).
      const ramBytes = (value) => typeof value === "number" && Number.isFinite(value)
        ? fmtBytes(value) : "--";
      $("diagInternalTotal").textContent = ramBytes(internal.total);
      $("diagInternalFree").textContent = ramBytes(internal.free);
      $("diagInternalBlock").textContent = ramBytes(internal.largest_block);
      $("diagInternalMin").textContent = ramBytes(internal.minimum_free);
      $("diagPsramTotal").textContent = ramBytes(psram.total);
      $("diagPsramFree").textContent = ramBytes(psram.free);
      $("diagPsramBlock").textContent = ramBytes(psram.largest_block);
      $("diagPsramMin").textContent = ramBytes(psram.minimum_free);
      $("diagRssi").textContent = s.rssi + " dBm";
      $("diagUptime").textContent = fmtUptime(s.uptime_seconds);
      $("maintIp").textContent = s.ip || "--";
      const pct = Math.round((Number(s.backlight || 0) * 100) / 255);
      $("backlightState").textContent = s.manual_display_off
        ? "screen off"
        : s.night_wake
          ? "night wake " + pct + "%"
          : s.night_active
            ? "night " + pct + "%"
            : !s.time_valid && $("nightEnabled").checked
              ? "day / time pending"
              : "day " + pct + "%";
      const pr = s.presence || {};
      $("sleepState").textContent = !pr.sleep_min
        ? "off"
        : pr.asleep
          ? "asleep"
          : pr.present
            ? "present"
            : "sleeps in " + Math.max(0, Math.ceil((pr.sleep_min * 60 - pr.idle_s) / 60)) + " min";
      if ($("ledState"))
        $("ledState").textContent = !s.led_enabled
          ? "disabled"
          : !s.led_pin_valid
            ? "invalid pin"
            : Number(s.led_duty) === 0
              ? "off"
              : (s.led_night ? "night " : "") + Math.round((Number(s.led_duty) * 100) / 255) + "%";
    } catch (e) {
      $("srLed").classList.add("offline");
      $("srTitle").textContent = "device unavailable";
      connectionState("factHa", "Unavailable", "error", "factHaDetail", "");
      connectionState("factMqtt", "Unavailable", "error", "factMqttDetail", "");
      overviewBambuUnavailable();
    } finally {
      statusBusy = false;
    }
  }

  function refreshPreview() {
    if (previewBusy) return;
    previewBusy = true;
    const img = new Image();
    img.onload = () => {
      const target = $("screenPreview"),
        size = $("previewSize");
      target.src = img.src;
      const sh = target.closest(".screen-shell");
      if (sh && img.naturalWidth && img.naturalHeight)
        sh.style.setProperty("--shell-ar", img.naturalWidth + "/" + img.naturalHeight);
      if (img.naturalWidth) size.textContent = img.naturalWidth + " x " + img.naturalHeight;
      previewBusy = false;
    };
    img.onerror = () => {
      previewBusy = false;
    };
    img.src = "/screen_live.bmp?t=" + Date.now();
  }
  $("previewNow").addEventListener("click", refreshPreview);
  setInterval(() => {
    if (!document.hidden && $("previewAuto").checked && document.querySelector(".page.active")?.dataset.page === "overview")
      refreshPreview();
  }, 5000);


  // Firmware release checks are independent of the manual OTA upload.
  let firmwareStatusRequest = 0;
  async function refreshFirmwareUpdateStatus(options) {
    const request = ++firmwareStatusRequest;
    try {
      const data = await getJson("/api/firmware/updates?t=" + Date.now(), options);
      if (request !== firmwareStatusRequest) return data;
      $("fwChecksEnabled").checked = !!data.checksEnabled;
      $("fwShowBadge").checked = !!data.showBadge;
      $("fwAutoDownload").checked = !!data.autoDownload;
      $("fwAllowBeta").checked = !!data.allowBeta;
      $("fwAllowTouchUpdates").checked = data.allowTouchUpdates !== false;
      $("fwAllowHaUpdates").checked = data.allowHaUpdates === true;
      const fwAction = $("fwDownloadNow");
      fwAction.dataset.action = data.staged ? "confirm" : "download";
      fwAction.textContent = data.staged ? "Install firmware" : "Download verified OTA";
      fwAction.disabled = data.otaSupported === false || data.downloading || (!data.staged && !data.available);
      $("fwDownloadStatus").textContent = data.downloading
        ? "Downloading to Tactula and verifying: " + fmtBytes(data.downloadedBytes) + " / " + fmtBytes(data.totalBytes)
        : data.staged
          ? (data.allowTouchUpdates === false
            ? "Verified firmware ready. Select Install firmware here to authorise physical confirmation for two minutes."
            : "Verified firmware stored in Tactula's inactive OTA partition. Select Install firmware to confirm on the touchscreen.")
          : data.error
            ? "Firmware update error: " + data.error
            : data.available
              ? "Update found, but NOT downloaded to Tactula. Select Download verified OTA, then wait for verification before installing."
              : "";
      $("fwUpdateStatus").textContent = data.otaSupported === false
        ? "OTA unavailable: this device has no inactive OTA partition. Preserve NVS and check the partition layout before USB recovery."
        : data.checking
        ? "Checking the " + (data.allowBeta ? "beta" : "stable") + " firmware release feed..."
        : data.checkPending
          ? "Update check queued..."
        : data.available
          ? "Update available: " + data.latest + " (installed: " + data.current + ")"
          : data.error
            ? "Update check: " + data.error
            : "Installed: " + data.current + " · No newer compatible release found.";
      return data;
    } catch (error) {
      if (request === firmwareStatusRequest)
        $("fwUpdateStatus").textContent = "Unable to read firmware update status.";
      return null;
    }
  }
  async function saveFirmwareUpdatePreferences() {
    const body = new URLSearchParams({
      checksEnabled: $("fwChecksEnabled").checked ? "1" : "0",
      showBadge: $("fwShowBadge").checked ? "1" : "0",
      autoDownload: $("fwAutoDownload").checked ? "1" : "0",
      allowBeta: $("fwAllowBeta").checked ? "1" : "0",
      allowTouchUpdates: $("fwAllowTouchUpdates").checked ? "1" : "0",
      allowHaUpdates: $("fwAllowHaUpdates").checked ? "1" : "0"
    });
    try {
      await getJson("/api/firmware/updates/preferences", {
        method: "POST", body
      });
      await refreshFirmwareUpdateStatus();
    } catch (error) {
      showToast("Failed to save firmware update preferences", true);
      await refreshFirmwareUpdateStatus();
    }
  }
  $("fwChecksEnabled").addEventListener("change", saveFirmwareUpdatePreferences);
  $("fwShowBadge").addEventListener("change", saveFirmwareUpdatePreferences);
  $("fwAutoDownload").addEventListener("change", saveFirmwareUpdatePreferences);
  $("fwAllowBeta").addEventListener("change", saveFirmwareUpdatePreferences);
  $("fwAllowTouchUpdates").addEventListener("change", saveFirmwareUpdatePreferences);
  $("fwAllowHaUpdates").addEventListener("change", saveFirmwareUpdatePreferences);
  $("fwDownloadNow").addEventListener("click", async () => {
    const button = $("fwDownloadNow");
    const install = button.dataset.action === "confirm";
    try {
      await getJson(install
        ? "/api/firmware/updates/confirm-on-device"
        : "/api/firmware/updates/download", { method: "POST" });
      $("fwDownloadStatus").textContent = install
        ? "Installation requested. Confirm Install and restart on the physical touchscreen."
        : "Downloading verified firmware...";
      button.disabled = true;
      // The request itself cannot reboot or install from the web UI.
      // A refresh restores the button if confirmation is postponed.
      if (install) await refreshFirmwareUpdateStatus();
    } catch (error) {
      showToast((install ? "Unable to request touchscreen confirmation: " :
        "Unable to begin firmware download: ") + error.message, true);
      await refreshFirmwareUpdateStatus();
    }
  });
  $("fwCheckNow").addEventListener("click", async () => {
    const button = $("fwCheckNow");
    button.disabled = true;
    try {
      await getJson("/api/firmware/updates/check", { method: "POST" });
      $("fwUpdateStatus").textContent = "Update check requested...";
      const deadline = Date.now() + 45000;
      while (Date.now() < deadline) {
        const data = await refreshFirmwareUpdateStatus({ signal: AbortSignal.timeout(5000) });
        if (data && !data.checkPending && !data.checking) break;
        await new Promise((resolve) => setTimeout(resolve, 1000));
      }
    } catch (error) {
      showToast("Cannot start update check", true);
    } finally {
      button.disabled = false;
    }
  });
  refreshFirmwareUpdateStatus();
  setInterval(() => {
    if (!document.hidden && document.querySelector(".page.active")?.dataset.page === "maintenance")
      refreshFirmwareUpdateStatus();
  }, 15000);

  let firmwareUploadInProgress = false;
  async function uploadFirmware(file) {
    if (!file || !file.name.toLowerCase().endsWith(".bin")) {
      showToast("Choose a firmware .bin file", true);
      return;
    }
    if (firmwareUploadInProgress) {
      showToast("A firmware upload is already running.", true);
      return;
    }
    firmwareUploadInProgress = true;
    try {
      const readiness = await getJson("/api/firmware/updates?t=" + Date.now());
      if (readiness.otaSupported === false)
        throw new Error("No inactive OTA partition. An application-only USB recovery may be needed; do not erase P4 NVS.");
      if (readiness.otaSlotBytes && file.size > readiness.otaSlotBytes)
        throw new Error("Firmware exceeds the inactive OTA partition (" + fmtBytes(readiness.otaSlotBytes) + ").");
    } catch (error) {
      firmwareUploadInProgress = false;
      showToast("Cannot start firmware upload: " + error.message, true);
      return;
    }
    const fd = new FormData();
    fd.append("firmware", file);
    const xhr = new XMLHttpRequest();
    $("otaProgress").classList.add("show");
    $("otaBar").style.width = "0%";
    $("otaText").textContent = "Starting upload...";
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        const p = Math.round((e.loaded * 100) / e.total);
        $("otaBar").style.width = p + "%";
        $("otaText").textContent = "Uploading " + p + "%";
      }
    };
    xhr.onload = () => {
      firmwareUploadInProgress = false;
      let data = {};
      try {
        data = JSON.parse(xhr.responseText);
      } catch (e) {}
      if (xhr.status >= 200 && xhr.status < 300) {
        $("otaBar").style.width = "100%";
        $("otaText").textContent = "Update complete. Device is restarting. This page will refresh when it is back online.";
        showToast(data.message || "Firmware installed", false);
        waitForOtaRestart();
      } else {
        $("otaText").textContent = data.message || "Update failed";
        showToast($("otaText").textContent, true);
      }
    };
    xhr.onerror = () => {
      firmwareUploadInProgress = false;
      showToast("Firmware upload failed", true);
      $("otaText").textContent = "Upload failed";
    };
    xhr.onabort = () => {
      firmwareUploadInProgress = false;
      $("otaText").textContent = "Firmware upload cancelled";
    };
    xhr.open("POST", "/ota/upload");
    xhr.send(fd);
  }
  $("otaBrowse").addEventListener("click", () => $("otaFile").click());
  $("otaFile").addEventListener("change", () => uploadFirmware($("otaFile").files[0]));
  ["dragenter", "dragover"].forEach((n) =>
    $("otaDrop").addEventListener(n, (e) => {
      e.preventDefault();
      $("otaDrop").classList.add("drag");
    }),
  );
  ["dragleave", "drop"].forEach((n) =>
    $("otaDrop").addEventListener(n, (e) => {
      e.preventDefault();
      $("otaDrop").classList.remove("drag");
    }),
  );
  $("otaDrop").addEventListener("drop", (e) => uploadFirmware(e.dataTransfer.files[0]));

  function exportConfiguration() {
    const a = document.createElement("a");
    a.href = "/api/config/export?t=" + Date.now();
    a.download = "tactula-config-" + new Date().toISOString().slice(0, 10) + ".json";
    document.body.appendChild(a);
    a.click();
    a.remove();
    showToast("Configuration backup download started", false);
  }
  async function importConfiguration(file) {
    if (!file) return;
    if (file.size > 98304) {
      showToast("The configuration backup is too large", true);
      $("backupFile").value = "";
      return;
    }
    try {
      const raw = await file.text();
      let parsed;
      try {
        parsed = JSON.parse(raw);
      } catch (e) {
        throw new Error("The selected file is not valid JSON");
      }
      if (!parsed || parsed.format !== "touchdeck-config")
        throw new Error("This is not a tactula-compatible configuration backup");
      if (!confirm("Restore this configuration and restart the device? WiFi credentials will be preserved."))
        return;
      $("backupImport").disabled = true;
      const data = await getJson("/api/config/import", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: raw,
      });
      showToast(data.message || "Configuration restored", false);
      $("backupImport").textContent = "Restarting...";
    } catch (e) {
      showToast(e.message, true);
      $("backupImport").disabled = false;
    } finally {
      $("backupFile").value = "";
    }
  }
  $("backupExport").addEventListener("click", exportConfiguration);
  $("backupImport").addEventListener("click", () => $("backupFile").click());
  $("backupFile").addEventListener("change", () => importConfiguration($("backupFile").files[0]));

  // Bedroom PNG icons: upload to microSD, list, preview, and remove.
  const bedroomIconValid = (name) =>
    /^[a-z0-9_]+_(on|off)\.png$/.test(name) && name.length <= 64;
  let bedroomIconListRequest = 0;
  async function loadBedroomIcons() {
    const request = ++bedroomIconListRequest;
    const status = $("bedroomIconsStatus"), list = $("bedroomIconsList");
    try {
      const data = await getJson("/api/bedroom/icons");
      if (request !== bedroomIconListRequest) return;
      const names = [...new Set((data.files || []).map((file) => file.name).filter(bedroomIconValid))];
      window.dispatchEvent(new CustomEvent("tactula:png-icons", { detail: { names, mounted: !!data.mounted } }));
      list.replaceChildren();
      const mounted = !!data.mounted;
      $("bedroomIconsBrowse").disabled = !mounted;
      status.textContent = mounted
        ? "microSD connected • " + names.length + " icon(s) saved"
        : "microSD not mounted. Enable it above, save and restart before uploading.";
      for (const name of names) {
        const card = document.createElement("div");
        card.style.cssText = "width:128px;padding:10px;border:1px solid var(--border,#444);border-radius:12px;text-align:center";
        const img = document.createElement("img");
        img.src = "/api/bedroom/icons/file?n=" + encodeURIComponent(name) + "&v=" + Date.now();
        img.alt = name;
        img.style.cssText = "width:78px;height:78px;object-fit:contain;display:block;margin:auto auto 8px";
        const label = document.createElement("div");
        label.textContent = name;
        label.style.cssText = "overflow-wrap:anywhere;font-size:12px;margin-bottom:6px";
        const remove = document.createElement("button");
        remove.type = "button";
        remove.className = "btn btn-danger";
        remove.textContent = "Delete";
        remove.addEventListener("click", async () => {
          if (!confirm("Delete " + name + " from the microSD card?")) return;
          remove.disabled = true;
          try {
            const body = new URLSearchParams({ n: name });
            await getJson("/api/bedroom/icons/delete", { method: "POST", body });
            showToast("Deleted " + name, false);
            await loadBedroomIcons();
          } catch (e) { showToast(e.message, true); remove.disabled = false; }
        });
        card.append(img, label, remove);
        list.append(card);
      }
    } catch (e) {
      if (request !== bedroomIconListRequest) return;
      status.textContent = "Could not read icons: " + e.message;
      $("bedroomIconsBrowse").disabled = true;
    }
  }
  const weatherPackCard = $("weatherIconPacksCard");
  if (weatherPackCard) {
    // Install weather icon packs through the existing validated microSD PNG API.
    // Browser-side conversion keeps SVG parsing and animation off the ESP32.
    const WEATHER_SVGS = ['clear-day','clear-night','partly-cloudy-day','partly-cloudy-night',
      'cloudy','fog','hail','rain','thunderstorms','snow','wind'];
    async function weatherPng(style, slug) {
      const url='https://cdn.jsdelivr.net/npm/@meteocons/svg-static@0.1.0/'+style+'/'+slug+'.svg';
      const response=await fetch(url);
      if (!response.ok) throw new Error('Meteocons '+slug+': HTTP '+response.status);
      const svg=await response.blob();
      if (svg.size>250000 || !((svg.type||'').includes('svg') || svg.type==='text/plain'))
        throw new Error('Unexpected Meteocons SVG '+slug);
      const objectUrl=URL.createObjectURL(svg);
      try {
        const img=new Image();
        await new Promise((resolve,reject)=>{img.onload=resolve;img.onerror=()=>reject(new Error('Could not decode '+slug));img.src=objectUrl;});
        const canvas=document.createElement('canvas');
        canvas.width=canvas.height=64;
        const ctx=canvas.getContext('2d');
        if (!ctx) throw new Error('Canvas unavailable');
        ctx.drawImage(img,0,0,64,64);
        return await new Promise((resolve,reject)=>canvas.toBlob(b=>b?resolve(b):reject(new Error('Could not convert '+slug)),'image/png'));
      } finally { URL.revokeObjectURL(objectUrl); }
    }
    weatherPackCard.querySelectorAll('[data-wclock-install]').forEach(x=>{
      x.onclick=async()=>{
        const style=x.dataset.wclockInstall;
        const status=x.closest('.field').querySelector('[data-wclock-status]');
        const buttons=[...weatherPackCard.querySelectorAll('button')];
        buttons.forEach(b=>b.disabled=true);
        try {
          const check=await getJson('/api/bedroom/icons');
          if (!check.mounted) throw new Error('Insert or mount the microSD card first.');
          for(let i=0;i<WEATHER_SVGS.length;i++){
            const slug=WEATHER_SVGS[i];
            status.textContent='Installing '+style+' '+(i+1)+'/'+WEATHER_SVGS.length+': '+slug;
            const png=await weatherPng(style,slug);
            const name='wc_'+style+'_'+slug.replace(/-/g,'_')+'_off.png';
            const fd=new FormData();
            fd.append('icon',png,name);
            await getJson('/api/bedroom/icons/upload?n='+encodeURIComponent(name),{method:'POST',body:fd});
          }
          status.textContent='Meteocons '+style+' installed ('+WEATHER_SVGS.length+' icons).';
          await loadBedroomIcons();
        }catch(e){status.textContent='Install failed: '+e.message;}
        finally {buttons.forEach(b=>b.disabled=false);}
      };
    });
    const SKYCONS_STATES = ['clear-day','clear-night','partly-cloudy-day','partly-cloudy-night',
      'cloudy','rain','pouring','lightning','thunderstorms','hail','sleet','snow','wind','windy-variant','fog','exceptional'];
    // Original Tactula outline artwork. Render only in the browser installer;
    // the device uses its existing eight-frame PNG playback pipeline.
    function drawTactulaWeather(ctx,slug,index,size) {
      const t=2*Math.PI*index/8;
      ctx.save();ctx.scale(size/64,size/64);
      ctx.strokeStyle='#fff';ctx.fillStyle='#fff';
      ctx.lineWidth=2.8;ctx.lineCap='round';ctx.lineJoin='round';
      function line(x,y,a,b){ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(a,b);ctx.stroke();}
      function circle(x,y,r){ctx.beginPath();ctx.arc(x,y,r,0,Math.PI*2);ctx.stroke();}
      function star(x,y,r,phase){
        ctx.save();ctx.globalAlpha=.72+.28*Math.cos(t+phase);
        ctx.translate(x,y);ctx.scale(r,r);
        ctx.beginPath();ctx.moveTo(0,-1);ctx.quadraticCurveTo(.12,-.12,1,0);
        ctx.quadraticCurveTo(.12,.12,0,1);ctx.quadraticCurveTo(-.12,.12,-1,0);
        ctx.quadraticCurveTo(-.12,-.12,0,-1);ctx.closePath();ctx.fill();ctx.restore();
      }
      function moon(x,y,s){
        ctx.save();ctx.translate(x,y);ctx.scale(s,s);
        ctx.beginPath();ctx.moveTo(3,-18);
        ctx.bezierCurveTo(-23,-21,-28,14,-6,22);
        ctx.bezierCurveTo(5,26,18,19,21,8);
        ctx.bezierCurveTo(1,15,-9,-3,3,-18);
        ctx.closePath();ctx.stroke();ctx.restore();
      }
      function sun(x,y,s){
        ctx.save();ctx.translate(x,y);ctx.scale(s,s);circle(0,0,12);
        ctx.rotate(t/8);
        for(let k=0;k<8;k++){let a=k*Math.PI/4;line(Math.cos(a)*18,Math.sin(a)*18,Math.cos(a)*24,Math.sin(a)*24);}
        ctx.restore();
      }
      function cloud(x,y,s){
        ctx.save();ctx.translate(x,y);ctx.scale(s,s);
        ctx.beginPath();ctx.moveTo(-17,11);
        ctx.bezierCurveTo(-31,11,-31,-7,-18,-9);
        ctx.bezierCurveTo(-14,-25,6,-26,13,-10);
        ctx.bezierCurveTo(29,-13,34,11,18,11);
        ctx.closePath();ctx.stroke();ctx.restore();
      }
      if(slug==='clear-night'){
        moon(28,31,.94);star(47,14,4.8,0);star(54,29,3.2,2);star(35,8,2.6,4);
      }else if(slug==='clear-day'){sun(32,32,1);
      }else if(slug.startsWith('partly-cloudy')){
        const shift=2*Math.sin(t);
        if(slug.endsWith('night')){moon(22,22,.65);star(40,9,3.3,0);star(48,19,2.5,2);}
        else sun(22,23,.73);
        // Mask the rear icon with the cloud silhouette, retaining transparency.
        ctx.save();ctx.translate(36+shift,40);ctx.scale(.82,.82);
        ctx.globalCompositeOperation='destination-out';
        ctx.beginPath();ctx.moveTo(-17,11);ctx.bezierCurveTo(-31,11,-31,-7,-18,-9);
        ctx.bezierCurveTo(-14,-25,6,-26,13,-10);ctx.bezierCurveTo(29,-13,34,11,18,11);ctx.closePath();
        ctx.fill();ctx.lineWidth=5;ctx.stroke();ctx.restore();
        cloud(36+shift,40,.82);
      }else if(slug==='cloudy'){
        // Readable motion even when the weather clock scales to ~32px.
        cloud(32+5*Math.sin(t),32+2*Math.cos(t),.85);
      }else if(slug==='fog'){
        cloud(32,23,.78);
        for(let k=0;k<3;k++){let x=3*Math.sin(t+k);line(12+x,42+k*7,51+x,42+k*7);}
      }else if(slug==='exceptional'){
        // Rotating storm spiral for the generic exceptional condition.
        ctx.save();ctx.translate(32,32);ctx.rotate(t);circle(0,0,4);
        for(let k=0;k<3;k++){
          ctx.rotate(2*Math.PI/3);ctx.beginPath();ctx.moveTo(7,0);
          ctx.bezierCurveTo(24,-12,29,8,17,20);ctx.stroke();
        }
        ctx.restore();
      }else if(slug==='wind'||slug==='windy-variant'){
        if(slug==='windy-variant')cloud(32,17,.52);
        for(let k=0;k<3;k++){
          const y=(slug==='windy-variant'?34:17)+k*(slug==='windy-variant'?10:14),x=3*Math.sin(t+k);
          ctx.beginPath();ctx.moveTo(7+x,y);ctx.lineTo(39+x,y);
          ctx.bezierCurveTo(54+x,y,56+x,y-12,47+x,y-10);ctx.stroke();
        }
      }else{
        cloud(32,23,.85);
        const storm=slug==='lightning'||slug==='thunderstorms';
        if(storm){
          // Keep the bolt recognisable throughout the loop, with a subtle pulse.
          ctx.save();ctx.globalAlpha=.52+.48*Math.pow(Math.max(0,Math.cos(t)),2);
          ctx.beginPath();ctx.moveTo(31,34);ctx.lineTo(23,46);ctx.lineTo(33,46);
          ctx.lineTo(28,60);ctx.lineTo(43,42);ctx.lineTo(33,42);ctx.lineTo(38,34);
          ctx.stroke();ctx.restore();
        }
        const n=slug==='lightning'?0:slug==='pouring'?6:slug==='thunderstorms'?2:3;
        for(let k=0;k<n;k++){
          const phase=(index/8+k/Math.max(1,n))%1;
          const x=storm?(k===0?12:52):n===6?10+k*8.8:17+k*15;
          const y=39+phase*15;
          ctx.save();ctx.globalAlpha=Math.min(1,phase*5,(1-phase)*5);
          if(slug==='snow'||(slug==='sleet'&&k===1)){
            for(let a=0;a<3;a++){let q=a*Math.PI/3;line(x-4*Math.cos(q),y-4*Math.sin(q),x+4*Math.cos(q),y+4*Math.sin(q));}
          }else if(slug==='hail'){
            circle(x,y,2.7);
          }else if(slug==='pouring'){
            line(x+3,y-5,x-3,y+5);
          }else line(x+2,y-3,x-2,y+4);
          ctx.restore();
        }
      }
      ctx.restore();
    }
    async function skyconsAtlas(slug,size=64) {
      const frame=document.createElement('canvas');frame.width=frame.height=size;
      const ctx=frame.getContext('2d');
      const atlas=document.createElement('canvas');atlas.width=8*size;atlas.height=size;
      const output=atlas.getContext('2d');
      if(!ctx||!output) throw new Error('Canvas unavailable');
      for(let i=0;i<8;i++){
        ctx.clearRect(0,0,size,size);
        drawTactulaWeather(ctx,slug,i,size);
        output.drawImage(frame,i*size,0);
      }
      const blob=await new Promise((resolve,reject)=>
        atlas.toBlob(x=>x?resolve(x):reject(new Error('PNG conversion failed')),'image/png'));
      if (blob.size>512*1024) throw new Error('Sprite PNG exceeds 512 KB');
      return blob;
    }
    weatherPackCard.querySelectorAll('[data-skycons-install]').forEach(button=>{
      button.onclick=async()=>{
        const status=button.closest('.field').querySelector('[data-wclock-status]');
        const buttons=[...weatherPackCard.querySelectorAll('button')];
        buttons.forEach(b=>b.disabled=true);
        try {
          const check=await getJson('/api/bedroom/icons');
          if(!check.mounted) throw new Error('Insert or mount the microSD card first.');
          for(let i=0;i<SKYCONS_STATES.length;i++){
            const slug=SKYCONS_STATES[i];
            status.textContent='Installing animated Tactula '+(i+1)+'/'+SKYCONS_STATES.length+': '+slug;
            const png=await skyconsAtlas(slug);
            const name='wc_outline_anim_'+slug.replace(/-/g,'_')+'_off.png';
            const form=new FormData();
            form.append('icon',png,name);
            await getJson('/api/bedroom/icons/upload?n='+encodeURIComponent(name),{method:'POST',body:form});
            if(check.skyconsHdFrameSize===160){
              status.textContent='Installing HD Tactula '+(i+1)+'/'+SKYCONS_STATES.length+': '+slug;
              // Install individually decoded HD frames, avoiding the 1280px atlas.
              const canvas=document.createElement('canvas');
              canvas.width=canvas.height=160;
              const ctx=canvas.getContext('2d');
              if(!ctx) throw new Error('Cannot render HD weather '+slug);
              for(let f=0;f<8;f++){
                status.textContent='Installing HD Tactula '+(i+1)+'/'+SKYCONS_STATES.length+': '+slug+' ('+(f+1)+'/8)';
                ctx.clearRect(0,0,160,160);
                drawTactulaWeather(ctx,slug,f,160);
                const hd=await new Promise((resolve,reject)=>canvas.toBlob(b=>b?resolve(b):reject(new Error('HD PNG conversion failed')),'image/png'));
                const hdName='wc_outline_hd_'+slug.replace(/-/g,'_')+'_f'+f+'_off.png';
                const hdForm=new FormData();
                hdForm.append('icon',hd,hdName);
                await getJson('/api/bedroom/icons/upload?n='+encodeURIComponent(hdName),{method:'POST',body:hdForm});
              }
            }
          }
          status.textContent='Animated Tactula weather installed ('+SKYCONS_STATES.length+' icons'+(check.skyconsHdFrameSize===160?' + HD pack':'')+').';
          await loadBedroomIcons();
        }catch(e){status.textContent='Weather installation failed: '+e.message;}
        finally {buttons.forEach(b=>b.disabled=false);}
      };
    });
  }
  $("bedroomIconsBrowse").addEventListener("click", () => $("bedroomIconsFiles").click());
  $("bedroomIconsRefresh").addEventListener("click", loadBedroomIcons);
  $("bedroomIconsFiles").addEventListener("change", async () => {
    const files = Array.from($("bedroomIconsFiles").files || []);
    if (!files.length) return;
    const invalid = files.find((file) => !bedroomIconValid(file.name) ||
      file.size < 33 || file.size > 512 * 1024 || file.type && file.type !== "image/png");
    if (invalid) {
      showToast("Invalid PNG: " + invalid.name + " (use lowercase name_on/off.png, max 512 KB)", true);
      $("bedroomIconsFiles").value = "";
      return;
    }
    $("bedroomIconsBrowse").disabled = true;
    try {
      for (const file of files) {
        // PNG's leading IHDR chunk stores width and height as big-endian values.
        const header = new DataView(await file.slice(0, 24).arrayBuffer());
        if (header.getUint32(0) !== 0x89504e47 || header.getUint32(4) !== 0x0d0a1a0a ||
            header.getUint32(8) !== 13 || header.getUint32(12) !== 0x49484452)
          throw new Error(file.name + ": invalid PNG header");
        const width = header.getUint32(16), height = header.getUint32(20);
        if (!width || !height || width > 256 || height > 256)
          throw new Error(file.name + ": " + width + "×" + height + " pixels. Maximum is 256×256.");
      }
    } catch (e) {
      showToast(e.message, true);
      $("bedroomIconsFiles").value = "";
      $("bedroomIconsBrowse").disabled = false;
      return;
    }
    let uploaded = 0;
    for (const file of files) {
      try {
        $("bedroomIconsStatus").textContent = "Uploading " + file.name + " (" + (uploaded + 1) + "/" + files.length + ")...";
        const fd = new FormData();
        fd.append("icon", file, file.name);
        await getJson("/api/bedroom/icons/upload?n=" + encodeURIComponent(file.name), {
          method: "POST", body: fd
        });
        uploaded++;
      } catch (e) {
        showToast(file.name + ": " + e.message, true);
        break;
      }
    }
    $("bedroomIconsFiles").value = "";
    await loadBedroomIcons();
    if (uploaded === files.length) showToast(uploaded + " icon(s) saved to microSD", false);
  });
  // Internal flash LittleFS: never auto-format a failed mount, because the
  // partition may contain recoverable user files from an older installation.
  async function loadFlashStorage() {
    try {
      const data = await getJson("/api/storage?t=" + Date.now());
      $("flashStorageRecovery").classList.toggle("hidden", !!data.mounted);
      $("flashStorageStatus").textContent = data.mounted
        ? "Mounted · " + fmtBytes(data.used) + " used of " + fmtBytes(data.total)
        : "Not mounted. Pages and tiles cannot be saved until storage is repaired or initialized.";
    } catch (e) {
      $("flashStorageStatus").textContent = "Unable to check storage: " + e.message;
      $("flashStorageRecovery").classList.add("hidden");
    }
  }
  async function loadStorageStatus() {
    await loadFlashStorage();
    try {
      const data = await wallApi("walls?o=l&gallery=m&limit=1");
      const sd = data.sd;
      $("sdFormat").classList.toggle("hidden", !sd.enabled || sd.supported === false);
      if (sd.supported === false) {
        $("sdState").textContent = "microSD unavailable on this firmware. Use internal flash storage.";
        return;
      }
      $("sdState").textContent =
        (sd.format ? "Format at the last restart: " + sd.format + "\n" : "") +
        (!sd.enabled
        ? "Not enabled"
        : !sd.mounted
          ? "Card not mounted" +
            (sd.error ? " (" + sd.error + ")" : "") +
            " - using internal storage. " +
            (/not accepted/.test(sd.error || "")
              ? "Format card below erases it and makes it readable."
              : "Power the deck off and on to retry.") +
            (sd.log ? "\n" + sd.log : "")
          : "Mounted" +
            (sd.mode ? " (" + sd.mode + ")" : "") +
            ": " +
            Math.floor(data.free / 1048576) +
            " MB free of " +
            Math.floor(data.total / 1048576) +
            " MB - My pictures: " +
            sd.own.join(" landscape / ") +
            " portrait; Daily: " +
            sd.daily.join(" landscape / ") +
            " portrait");
    } catch (e) {
      $("sdState").textContent = "Unable to check microSD card: " + e.message;
    }
  }
  $("flashInitializeConfirm").addEventListener("input", () => {
    $("flashInitialize").disabled =
      $("flashInitializeConfirm").value.trim() !== "FORMAT INTERNAL FLASH";
  });
  $("flashInitialize").addEventListener("click", async () => {
    if ($("flashInitializeConfirm").value.trim() !== "FORMAT INTERNAL FLASH") return;
    if (!confirm("PERMANENTLY ERASE the internal flash filesystem? Existing deck.json, icons and pictures will be lost. This cannot be undone.")) return;
    const button = $("flashInitialize");
    button.disabled = true;
    try {
      const response = await getJson("/api/storage/initialize", {
        method: "POST",
        body: new URLSearchParams({confirmation: "FORMAT INTERNAL FLASH"}),
      });
      $("flashInitializeConfirm").value = "";
      showToast(response.message || "Internal flash initialized", false);
      await loadFlashStorage();
    } catch (e) {
      showToast(e.message, true);
      button.disabled = false;
    }
  });
  function updateFactoryReset() {
    const ready = $("factoryConfirm").value.trim() === "RESET";
    $("factoryReset").disabled = !ready;
  }
  $("factoryConfirm").addEventListener("input", updateFactoryReset);
  $("sdCard").addEventListener("change", () => {
    $("sdSave").disabled = $("sdCard").checked === !!config.display.sdCard;
  });
  $("sdSave").addEventListener("click", async () => {
    try {
      $("sdSave").disabled = true;
      const body = new URLSearchParams($("sdCard").checked ? { sdCard: "1" } : {});
      const data = await getJson("/save/sd", { method: "POST", body });
      showToast(data.message || "Saved", false);
      if (data.restarting) {
        $("sdSave").textContent = "Restarting...";
        setTimeout(() => location.reload(), 5000);
      }
    } catch (e) {
      showToast(e.message, true);
      $("sdSave").disabled = false;
    }
  });
  $("sdFormat").addEventListener("click", async () => {
    if (!confirm("Erase everything on the microSD card (pictures, brand icons) and create a new FAT file system?"))
      return;
    try {
      $("sdFormat").disabled = true;
      const body = new URLSearchParams({ confirmation: "FORMAT" });
      const data = await getJson("/api/sd/format", { method: "POST", body });
      showToast(data.message || "Formatting", false);
      $("sdFormat").textContent = "Restarting...";
      setTimeout(() => location.reload(), 8000);
    } catch (e) {
      showToast(e.message, true);
      $("sdFormat").disabled = false;
    }
  });
  $("factoryReset").addEventListener("click", async () => {
    if ($("factoryConfirm").value.trim() !== "RESET") return;
    if (!confirm("Erase all settings and WiFi credentials? The device will restart in setup mode.")) return;
    try {
      $("factoryReset").disabled = true;
      $("factoryConfirm").disabled = true;
      const body = new URLSearchParams({ confirmation: "RESET" });
      const data = await getJson("/api/factory-reset", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body,
      });
      showToast(data.message || "Factory reset complete", false);
      $("factoryReset").textContent = "Restarting...";
    } catch (e) {
      showToast(e.message, true);
      $("factoryConfirm").disabled = false;
      updateFactoryReset();
    }
  });

  /* Glass galleries: crop and upload sequentially to bound device memory use. */
  /* Help tips: long hints (or ones marked data-tip) fold behind a "?" next to
   their label, card title or check text. Hints with an id are filled by code,
   and hints with nothing to hang on stay as they are. */
  function tipButton(target) {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "tip-btn";
    b.textContent = "?";
    b.setAttribute("aria-label", "More info");
    b.setAttribute("aria-expanded", "false");
    b.onclick = (e) => {
      e.preventDefault();
      e.stopPropagation();
      const open = target.classList.toggle("tip-open");
      b.setAttribute("aria-expanded", open ? "true" : "false");
    };
    return b;
  }
  function initHelpTips() {
    const fold = (el) =>
      !el.id && (el.hasAttribute("data-tip") || el.textContent.replace(/\s+/g, " ").trim().length >= 80);
    qa(".ct-hint").forEach((el) => {
      const host = el.closest(".check-text")?.querySelector("strong");
      if (!fold(el) || !host || host.querySelector(".tip-btn")) return;
      host.appendChild(tipButton(el));
      el.classList.add("tip");
    });
    qa("p.field-hint").forEach((el) => {
      const prev = el.previousElementSibling;
      const title = prev?.classList.contains("card-head") ? prev.querySelector(".card-title") : prev;
      const host = title?.classList.contains("card-title")
        ? title
        : el.closest(".field")?.querySelector(".field-label") ||
          (prev?.classList.contains("field") ? prev.querySelector(".field-label") : null);
      if (!fold(el) || !host || host.querySelector(".tip-btn")) return;
      host.appendChild(tipButton(el));
      el.classList.add("tip");
    });
  }
  const walls = {
    l: { files: [], selected: new Set(), loaded: 24 },
    p: { files: [], selected: new Set(), loaded: 24 },
  };
  let wallSource = 0,
    wallView = 0;
  function wallGallery() {
    return wallSource === 2 ? $("wallBrowse").value : wallSource === 1 ? "d" : "m";
  }
  function resetWalls() {
    wallView++;
    wallCache = Math.max(Date.now(), wallCache + 1);
    for (const o of ["l", "p"]) {
      walls[o].loaded = 24;
      walls[o].files = [];
      walls[o].selected.clear();
      wallRender(o);
    }
    wallStatus();
  }
  function setWallSource(source) {
    $("wallBrowseField").classList.toggle("hidden", source !== 2);
    if (wallSource === source) return;
    wallSource = source;
    resetWalls();
  }
  $("wallBrowse").onchange = (e) => {
    e.stopPropagation();
    resetWalls();
  };
  $("wallBrowse").oninput = (e) => e.stopPropagation();
  let wallBusy = false,
    wallRefreshing = false,
    wallCache = Date.now();
  async function wallApi(path, method = "GET", body) {
    const r = await fetch("/api/deck/" + path, { method, body, cache: "no-store" });
    const j = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(j.message || "HTTP " + r.status);
    return j;
  }
  function wallButtons() {
    $("wallBrowse").disabled = wallBusy;
    for (const o of ["l", "p"]) {
      const s = walls[o];
      $("wallMore_" + o).disabled = wallBusy || wallRefreshing;
      $("wallPick_" + o).disabled = wallBusy || wallSource === 1;
      $("wallHint_" + o).textContent =
        wallGallery() === "d"
          ? "Daily pictures, downloaded by the deck. Uploads go to My pictures."
          : "Add pictures or drop them here.";
      $("wallShow_" + o).disabled = wallBusy || s.selected.size !== 1;
      $("wallDelete_" + o).disabled = wallBusy || !s.selected.size;
      $("wallAll_" + o).disabled = wallBusy || !s.files.length;
      qa("button", $("wallLegacy_" + o)).forEach((b) => (b.disabled = wallBusy));
    }
  }
  const wallThumbQueue = new Set();
  let wallThumbLoading = false;
  const wallThumbObserver = new IntersectionObserver((entries) => {
    for (const { target, isIntersecting } of entries) {
      if (isIntersecting) wallThumbQueue.add(target);
      else wallThumbQueue.delete(target);
    }
    loadWallThumbnails();
  }, { rootMargin: "200px" });

  function requestWallThumbnail(img, url) {
    return new Promise((resolve) => {
      const finish = (loaded) => {
        clearTimeout(timeout);
        img.onload = img.onerror = null;
        resolve(loaded);
      };
      const timeout = setTimeout(() => {
        finish(false);
        img.removeAttribute("src");
      }, 12000);
      img.onload = () => finish(true);
      img.onerror = () => finish(false);
      img.src = url;
    });
  }

  async function loadWallThumbnails() {
    if (wallThumbLoading) return;
    wallThumbLoading = true;
    try {
      // The ESP32 serves one request at a time. Load visible thumbnails in
      // sequence and give transient failures a fresh request.
      for (const img of wallThumbQueue) {
        wallThumbQueue.delete(img);
        wallThumbObserver.unobserve(img);
        if (!img.isConnected) continue;
        const thumbnail = img.parentElement;
        thumbnail.dataset.state = "loading";
        const url = img.dataset.wallSrc;
        let loaded = false;
        for (let attempt = 0; attempt < 3 && img.isConnected && !loaded; attempt++) {
          if (attempt) await new Promise((resolve) => setTimeout(resolve, 300 * attempt));
          if (!img.isConnected) break;
          loaded = await requestWallThumbnail(img, attempt ? url + "&retry=" + attempt : url);
        }
        thumbnail.dataset.state = loaded ? "ready" : "error";
      }
    } finally {
      wallThumbLoading = false;
    }
  }

  function wallRender(o) {
    const s = walls[o],
      grid = $("wallGrid_" + o);
    s.selected = new Set([...s.selected].filter((n) => s.files.some((f) => f.n === n)));
    // The status poll re-renders every 5 s: skip when nothing changed and reuse
    // the <img> nodes, or the one-request-at-a-time device restarts every thumbnail.
    const sig = [
      wallCache,
      s.current,
      s.pinned,
      s.free,
      s.legacy,
      s.count,
      ...s.files.map((f) => f.n),
      "|",
      ...s.selected,
    ].join(",");
    if (sig === s.sig) return;
    s.sig = sig;
    if (s.imgsFor !== wallCache) {
      for (const img of s.imgs?.values() ?? []) {
        wallThumbObserver.unobserve(img);
        wallThumbQueue.delete(img);
      }
      s.imgs = new Map();
      s.imgsFor = wallCache;
    }
    grid.replaceChildren();
    for (const f of s.files) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "btn";
      const sel = s.selected.has(f.n);
      button.style.cssText =
        "position:relative;display:flex;flex-direction:column;gap:5px;padding:5px;white-space:normal;border-width:2px";
      button.setAttribute("aria-pressed", sel ? "true" : "false");
      button.title = f.n;
      let img = s.imgs.get(f.n);
      if (!img) {
        img = document.createElement("img");
        img.alt = f.n;
        img.dataset.wallSrc =
          "/api/deck/walls/file?o=" +
          o +
          "&gallery=" +
          wallGallery() +
          "&n=" +
          encodeURIComponent(f.n) +
          "&v=" +
          wallCache;
        const thumbnail = document.createElement("span");
        thumbnail.className = "wall-thumbnail";
        thumbnail.dataset.state = "queued";
        thumbnail.appendChild(img);
        s.imgs.set(f.n, img);
        wallThumbObserver.observe(img);
      }
      const label = document.createElement("span");
      label.style.cssText = "font-size:11px;min-height:14px;color:var(--accent)";
      label.textContent = [s.current === f.n ? "Current" : "", s.pinned === f.n ? "Pinned" : ""]
        .filter(Boolean)
        .join(" - ");
      button.style.borderColor = sel ? "var(--accent)" : "";
      const mark = document.createElement("span");
      mark.textContent = sel ? "\u2713" : "";
      mark.style.cssText =
        "position:absolute;top:9px;right:9px;width:20px;height:20px;border-radius:50%;display:" +
        (sel ? "flex" : "none") +
        ";align-items:center;justify-content:center;font-size:13px;background:var(--accent);color:#fff";
      button.append(img.parentElement, label, mark);
      button.onclick = () => {
        if (wallBusy) return;
        if (s.selected.has(f.n)) s.selected.delete(f.n);
        else s.selected.add(f.n);
        wallRender(o);
      };
      grid.appendChild(button);
    }
    $("wallSpace_" + o).textContent =
      (wallSource === 2 ? (wallGallery() === "d" ? "Daily: " : "My pictures: ") : "") +
      s.files.length +
      " of " +
      (s.count || 0) +
      " pictures - about " +
      Math.max(0, Math.floor(((s.free || 0) - 256 * 1024) / (60 * 1024))) +
      " more pictures fit";
    $("wallMore_" + o).classList.toggle("hidden", s.files.length >= (s.count || 0));
    $("wallMore_" + o).disabled = wallBusy || wallRefreshing;
    const legacy = $("wallLegacy_" + o);
    legacy.replaceChildren();
    if (s.legacy) {
      legacy.append(document.createTextNode("Old single picture in use - "));
      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "btn";
      remove.textContent = "Remove";
      remove.onclick = () => wallAction(o, "wallpaper/remove?o=" + o);
      legacy.appendChild(remove);
    }
    wallButtons();
  }
  async function wallStatus() {
    if (wallRefreshing) return;
    wallRefreshing = true;
    const view = wallView,
      gallery = wallGallery();
    try {
      for (const o of ["l", "p"]) {
        const s = walls[o];
        let data,
          files = [];
        for (let offset = 0; offset < s.loaded; offset += 24) {
          data = await wallApi("walls?o=" + o + "&gallery=" + gallery + "&offset=" + offset + "&limit=24");
          if (view !== wallView) return;
          files.push(...data.files);
          if (offset + 24 >= data.count) break;
        }
        Object.assign(s, data, { files });
        const sd = data.sd;
        $("sdWallState").innerHTML = !sd.enabled
          ? 'Pictures are stored internally. To use a microSD card, enable it in <a href="#" data-page="storage">Storage &gt; microSD card</a>.'
          : !sd.mounted
            ? 'microSD card not mounted, using internal storage. See <a href="#" data-page="storage">Storage &gt; microSD card</a>.'
            : "Stored on the microSD card.";
        wallRender(o);
      }
    } catch (e) {
      showToast(e.message, true);
    } finally {
      wallRefreshing = false;
      for (const o of ["l", "p"]) $("wallMore_" + o).disabled = wallBusy;
      if (view !== wallView) wallStatus();
    }
  }
  // bust: after deletes, as names are reused once the newest files are gone.
  async function wallAction(o, path, bust = true) {
    if (wallBusy) return;
    wallBusy = true;
    wallButtons();
    try {
      await wallApi(path + "&gallery=" + wallGallery(), "POST");
      if (bust) wallCache = Date.now();
      await wallStatus();
    } catch (e) {
      showToast(e.message, true);
    } finally {
      wallBusy = false;
      wallButtons();
    }
  }
  async function wallJpeg(file, o) {
    const img = await createImageBitmap(file);
    try {
      const c = document.createElement("canvas");
      c.width = o === "l" ? 480 : 320;
      c.height = o === "l" ? 320 : 480;
      const scale = Math.max(c.width / img.width, c.height / img.height),
        w = img.width * scale,
        h = img.height * scale;
      const ctx = c.getContext("2d");
      ctx.imageSmoothingEnabled = true;
      ctx.imageSmoothingQuality = "high";
      ctx.drawImage(img, (c.width - w) / 2, (c.height - h) / 2, w, h);
      return await new Promise((resolve, reject) =>
        c.toBlob(
          (blob) => (blob ? resolve(blob) : reject(new Error("JPEG conversion failed"))),
          "image/jpeg",
          0.9,
        ),
      );
    } finally {
      img.close();
    }
  }
  async function wallBatch(o, files) {
    if (wallBusy || !files.length) return;
    wallBusy = true;
    wallButtons();
    $("wallErrors_" + o).replaceChildren();
    let stored = 0,
      attempted = 0;
    try {
      for (let i = 0; i < files.length; i++) {
        attempted = i + 1;
        $("wallProgress_" + o).textContent = i + 1 + " / " + files.length;
        try {
          const blob = await wallJpeg(files[i], o),
            form = new FormData();
          form.append("wall", blob, "wall.jpg");
          await wallApi("walls?o=" + o + "&i=" + (i + 1) + "&c=" + files.length, "POST", form);
          stored++;
        } catch (e) {
          const line = document.createElement("div");
          line.textContent = files[i].name + ": " + e.message;
          $("wallErrors_" + o).appendChild(line);
          if (e.message === "flash full") break;
        }
      }
    } finally {
      $("wallProgress_" + o).textContent = attempted + " / " + files.length + " - " + stored + " stored";
      wallCache = Date.now();
      // Uploads land in My pictures: show them when Both was browsing Daily.
      if (wallSource === 2 && stored && $("wallBrowse").value !== "m") {
        $("wallBrowse").value = "m";
        resetWalls();
      } else await wallStatus();
      wallBusy = false;
      wallButtons();
    }
  }
  for (const o of ["l", "p"]) {
    $("wallMore_" + o).onclick = () => {
      if (wallBusy || wallRefreshing) return;
      walls[o].loaded += 24;
      wallStatus();
    };
    $("wallPick_" + o).onclick = () => $("wallFile_" + o).click();
    $("wallFile_" + o).oninput = (e) => e.stopPropagation();
    $("wallFile_" + o).onchange = (e) => {
      e.stopPropagation();
      const files = [...e.target.files];
      e.target.value = "";
      wallBatch(o, files);
    };
    const gallery = $("wallGallery_" + o);
    gallery.ondragover = (e) => {
      e.preventDefault();
      e.dataTransfer.dropEffect = wallBusy || wallSource === 1 ? "none" : "copy";
    };
    gallery.ondrop = (e) => {
      e.preventDefault();
      if (wallSource !== 1) wallBatch(o, [...e.dataTransfer.files]);
    };
    $("wallShow_" + o).onclick = () =>
      wallAction(o, "walls/show?o=" + o + "&n=" + encodeURIComponent([...walls[o].selected][0]), false);
    $("wallDelete_" + o).onclick = () =>
      wallAction(o, "walls/delete?o=" + o + "&n=" + encodeURIComponent([...walls[o].selected].join(",")));
    $("wallAll_" + o).onclick = () => {
      if (confirm("Delete all " + (o === "l" ? "landscape" : "portrait") + " pictures?"))
        wallAction(o, "walls/delete?o=" + o + "&n=*");
    };
  }
  /* Daily pictures: status line, Download now, gallery refresh when the source or set changes. */
  let dailyActive = false,
    dailyRunning = false,
    dailyTick = 0;
  function dailyText(d) {
    if (d.running) return d.step + " - " + (d.got[0] + d.got[1]) + " of " + (d.want[0] + d.want[1]) + " stored";
    const parts = [d.files[0] + " landscape and " + d.files[1] + " portrait pictures on the device"];
    parts.push(d.lastRun ? "last download " + new Date(d.lastRun * 1000).toLocaleString() : "not downloaded yet");
    if (d.error) parts.push(d.error);
    return parts.join(" - ");
  }
  async function dailyStatus() {
    try {
      const d = await wallApi("daily");
      const refresh = dailyActive !== d.active || (dailyRunning && !d.running);
      dailyActive = d.active;
      setWallSource(d.source ?? (d.active ? 1 : 0));
      dailyRunning = d.running;
      $("dailyState").textContent = dailyText(d);
      $("dailyFetch").disabled = d.running;
      if (refresh) {
        wallCache = Date.now();
        await wallStatus();
      }
      wallButtons();
    } catch (e) {}
  }
  $("dailyFetch").onclick = async () => {
    try {
      showToast((await wallApi("daily/run", "POST")).message, false);
      await dailyStatus();
    } catch (e) {
      showToast(e.message, true);
    }
  };
  wallStatus();
  setInterval(() => {
    if (!document.hidden && !wallBusy && $("wallGrid_l").offsetParent !== null) wallStatus();
  }, 5000);
  setInterval(() => {
    if (document.hidden || $("wallGrid_l").offsetParent === null) return;
    if (dailyRunning || ++dailyTick % 5 === 0) dailyStatus();
  }, 2000);

  /* Bambu-capable firmware only: printer is a normal portal page, not /printer.
     The backend still owns credentials and endpoints; no access code comes back
     from its status API. Never update form fields during polling after initial load. */
  // Printer data is untrusted, even when it comes from the device itself.
  // Build the AMS view with text nodes, not HTML interpolation: corrupted or
  // exceptionally long filament names must never flood/reshape the portal.
  let printerPolling = false;
  // Temperatures are numeric fields; keep this formatter separate from
  // the text-only, bounded AMS filament-name renderer.
  const printerTemp = (value, target) => {
    const n = Number(value);
    const t = Number(target);
    return value != null && Number.isFinite(n)
      ? n.toFixed(1) + "°C" +
        (target != null && Number.isFinite(t) ? " / " + t.toFixed(0) + "°C" : "")
      : "—";
  };
  const printerText = (value, max = 40) => {
    if (typeof value !== "string" && typeof value !== "number") return "";
    const text = String(value).trim();
    if (!text || text.length > max ||
        /[{}[\]\\\\]/.test(text)) return "";
    return text;
  };
  function renderPrinterAms(units) {
    const target = $("prAms");
    target.replaceChildren();
    if (!Array.isArray(units) || !units.length) {
      target.textContent = "No AMS data yet";
      return;
    }
    for (const unit of units.slice(0, 6)) {
      if (!unit || typeof unit !== "object") continue;
      const card = document.createElement("div");
      card.className = "pr-ams-unit";
      const title = document.createElement("strong");
      const humidity = Number(unit.humidity);
      title.textContent = "AMS " + (printerText(unit.unit, 3) || "—") +
        " · humidity " + (Number.isFinite(humidity) && humidity >= 0 && humidity <= 100
            ? humidity + "%" : "—");
      card.appendChild(title);
      for (const tray of (Array.isArray(unit.slots) ? unit.slots.slice(0, 4) : [])) {
        if (!tray || typeof tray !== "object") continue;
        const line = document.createElement("div");
        line.className = "pr-ams-slot";
        const dot = document.createElement("span");
        dot.className = "pr-ams-dot";
        dot.style.background = /^#[0-9a-fA-F]{6}$/.test(tray.colour || "")
          ? tray.colour : "#444963";
        const label = document.createElement("span");
        const material = tray.present ? printerText(tray.type, 32) || "Loaded" : "Empty";
        label.textContent = "Slot " + (printerText(tray.slot, 2) || "?") + ": " + material;
        line.append(dot, label);
        card.appendChild(line);
      }
      target.appendChild(card);
    }
  }

  async function refreshPrinter() {
    if (!printerAvailable || printerPolling) return;
    printerPolling = true;
    try {
      const data = await getJson("/api/office/bambu");
      $("prState").textContent = data.status || "unknown";
      $("prFault").textContent = data.error || "";
      $("prJob").textContent = data.job || data.state || "No job";
      $("prProgress").value = Math.min(100, Math.max(0, Number(data.progress) || 0));
      $("prPct").textContent = (data.progress ?? 0) + "%";
      $("prTime").textContent = data.remaining_minutes != null
        ? Math.floor(data.remaining_minutes / 60) + "h " + (data.remaining_minutes % 60) + "m" : "—";
      $("prLayer").textContent = (data.layer ?? "—") + " / " + (data.total_layers ?? "—");
      $("prNozzle").textContent = printerTemp(data.nozzle, data.nozzle_target);
      $("prBed").textContent = printerTemp(data.bed, data.bed_target);
      $("prChamber").textContent = printerTemp(data.chamber);
      $("prLightOn").disabled = $("prLightOff").disabled = data.status !== "online";
      renderPrinterAms(data.ams);
      if (!printerConfigLoaded) {
        printerConfigLoaded = true;
        $("prEnabled").checked = Boolean(data.enabled);
        $("prSource").value = data.source || "lan";
        $("prAddress").value = data.address || "";
        $("prDisplayName").value = data.display_name || "";
        $("prCamera").value = data.camera || "";
        $("prCameraRefresh").value = data.camera_refresh || 60;
        $("prHasCode").textContent = data.access_code_set ? "(saved)" : "(not set)";
        // Serial number is write-only in the backend; never invent its contents.
        $("prSerial").placeholder = data.serial_set
          ? "Saved — re-enter to change" : "Printer serial number";
      }
    } catch (e) {
      $("prState").textContent = "Unavailable";
      $("prFault").textContent = e.message;
    } finally {
      printerPolling = false;
    }
  }
  $("prConfig").addEventListener("submit", async (event) => {
    event.preventDefault();
    const button = $("prSave");
    const body = new URLSearchParams();
    body.set("enabled", $("prEnabled").checked ? "1" : "0");
    for (const [key, id] of [["source", "prSource"], ["address", "prAddress"],
                            ["display_name", "prDisplayName"], ["camera", "prCamera"], ["camera_refresh", "prCameraRefresh"],
                            ["serial", "prSerial"], ["code", "prCode"]]) {
      body.set(key, $(id).value);
    }
    button.disabled = true;
    try {
      await getJson("/api/office/bambu", {method:"POST", body});
      $("prCode").value = "";
      $("prMessage").textContent = "Printer settings saved";
      await refreshPrinter();
      refreshOverviewBambuConnection();
    } catch (e) {
      $("prMessage").textContent = "Not saved: " + e.message;
    } finally {
      button.disabled = false;
    }
  });
  $("prDiscover").addEventListener("click", async () => {
    try {
      const data = await getJson("/api/office/bambu/discover");
      const found = data.printers || [];
      $("prMessage").textContent = found.map(p => p.name + ": " + p.serial)
        .join(" · ") || "No Bambu printers found in Home Assistant";
      if (found.length === 1) {
        $("prSerial").value = found[0].serial;
        $("prSource").value = "ha";
        if (!$("prDisplayName").value.trim() && found[0].name)
          $("prDisplayName").value = found[0].name;
      }
    } catch (e) {
      $("prMessage").textContent = e.message;
    }
  });
  for (const [id, on] of [["prLightOn","1"], ["prLightOff","0"]]) {
    $(id).addEventListener("click", async () => {
      try {
        await getJson("/api/office/bambu/light", {
          method: "POST", body: new URLSearchParams({on}),
        });
        await refreshPrinter();
      } catch (e) {
        $("prMessage").textContent = e.message;
      }
    });
  }
  setInterval(() => {
    if (printerAvailable && !document.hidden &&
        document.querySelector(".page.active")?.dataset.page === "printer")
      refreshPrinter();
  }, 4000);

  let initial = "overview";
  try {
    initial = localStorage.getItem("sn_page") || "overview";
  } catch (e) {}
  if (initial === "logs") initial = "debug"; // migrate saved navigation from older firmware
  if (!document.querySelector('[data-nav="' + initial + '"]')) initial = "overview";
  initHelpTips();
  // The deck editor (deck.js) renders late and re-renders on every edit.
  let tipsQueued = false;
  new MutationObserver(() => {
    if (tipsQueued) return;
    tipsQueued = true;
    setTimeout(() => {
      tipsQueued = false;
      initHelpTips();
    });
  }).observe(document.body, { childList: true, subtree: true });
  loadDebugStatus();
  (async () => {
    const config = loadConfig();
    if (initial !== "debug") await config;
    if (document.querySelector(".page.active")?.dataset.page !== "overview") return;
    if (initial === "printer" && !printerAvailable) initial = "overview";
    await showPage(initial);
  })();
  pollStatus();
  if (initial === "overview" && !document.hidden) refreshPreview();
  setInterval(() => {
    const interval = p4DiagnosticsAvailable && !awaitingOtaRestart ? 10000 : 2000;
    if (Date.now() - lastStatusPoll >= interval) pollStatus();
  }, 2000);
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) pollStatus();
  });
})();
