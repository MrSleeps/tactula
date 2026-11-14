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
    if (name === "light" || name === "hardware") loadEntityLists();
    if (name === "hardware") loadAudio();
    if (name === "printer") {
      if (!printerAvailable) return false;
      refreshPrinter();
    }
    if (name === "maintenance") { loadBedroomIcons(); loadFlashStorage(); }
    qa(".page").forEach((p) => p.classList.toggle("active", p.dataset.page === name));
    if (name === "overview") refreshOverviewBambuConnection();
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
    "glassTint",
    "glassBlur",
    "glassDim",
    "glassOn",
    "sleepDim",
    "hapticStrength",
    "hapticMs",
    "audioVolume",
  ].forEach((id) => $(id).addEventListener("input", updateRangeLabels));
  function updateGlassFields() {
    const g = $("theme").value === "2";
    $("glassLook").classList.toggle("hidden", !g);
  }
  $("theme").addEventListener("change", updateGlassFields);
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
    $("dailyFields").classList.toggle("hidden", $("wallSource").value === "0");
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
    setVal("rotation", c.display.rotation);
    setVal("theme", c.display.theme || 0);
    setVal("tileFx", c.display.tileFx || 0);
    FX_IDS.forEach((id) => setVal(id, c.display[id] || 0));
    setVal("brightness", c.display.brightness);
    setVal("wallEvery", c.display.wallEvery ?? 5);
    setVal("wallOrder", c.display.wallOrder ?? 0);
    setVal("wallSource", c.display.wallSource ?? (c.display.wallDaily ? 1 : 0));
    setCheck("sdCard", c.display.sdCard ?? false);
    $("sdSave").disabled = true;
    setVal("dailyMax", c.display.dailyMax ?? 100);
    setWallSource(c.display.wallSource ?? (c.display.wallDaily ? 1 : 0));
    setVal("dailyOrient", c.display.dailyOrient ?? 0);
    setVal("dailyCount", c.display.dailyCount ?? 12);
    setVal("dailyHour", c.display.dailyHour ?? 3);
    updateDailyFields();
    dailyStatus();
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
  let overviewBambuLan = false;
  async function refreshOverviewBambuConnection() {
    if (!printerAvailable || overviewBambuPolling ||
        document.hidden ||
        document.querySelector(".page.active")?.dataset.page !== "overview") return;
    overviewBambuPolling = true;
    try {
      const p = await getJson("/api/office/bambu?t=" + Date.now());
      overviewBambuLan = p.enabled === true && p.source === "lan";
      $("factBambuRow").classList.toggle("hidden", !overviewBambuLan);
      if (!overviewBambuLan) return; // HA-backed printer isn't a LAN connection
      const name = p.display_name || "Bambu printer";
      $("factBambuLabel").textContent = name + " (LAN)";
      const state = p.status || "unknown";
      const label = state === "online" ? "Connected" :
        state === "connecting" ? "Connecting" :
        state === "failed" ? "Connection failed" :
        state === "off" ? "Offline" : "Unavailable";
      connectionState("factBambu", label,
        state === "online" ? "connected" :
        state === "connecting" ? "pending" : "error",
        "factBambuDetail", p.error || "");
    } catch (error) {
      // Do not claim the printer is disconnected merely because the web
      // request failed; only show an error if it was a known LAN printer.
      if (overviewBambuLan) connectionState("factBambu", "Status unavailable",
        "error", "factBambuDetail", "Unable to retrieve printer status");
    } finally {
      overviewBambuPolling = false;
    }
  }
  setInterval(refreshOverviewBambuConnection, 15000);

  async function pollStatus() {
    try {
      const s = await getJson("/api/status?t=" + Date.now());
      $("srLed").classList.remove("offline");
      $("srTitle").textContent = s.ap_mode ? "setup access point" : "device online";
      $("srIp").textContent = s.ip || "--";
      $("srHost").textContent = (s.hostname || "tactula") + ".local";
      const ha = String(s.ha_link || "unknown");
      connectionState("factHa", ha,
        ha === "connected" ? "connected" :
        ["connecting", "authenticating", "waiting for WiFi"].includes(ha) ? "pending" :
        ha === "not set up" ? "neutral" : "error");
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
      connectionState("factHa", "Unavailable", "error");
      connectionState("factMqtt", "Unavailable", "error", "factMqttDetail", "");
      if (overviewBambuLan) connectionState("factBambu", "Unavailable", "error",
        "factBambuDetail", "");
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
    if ($("previewAuto").checked && document.querySelector(".page.active")?.dataset.page === "overview")
      refreshPreview();
  }, 5000);


  // Firmware release checks are independent of the manual OTA upload.
  async function refreshFirmwareUpdateStatus() {
    try {
      const data = await getJson("/api/firmware/updates?t=" + Date.now());
      $("fwChecksEnabled").checked = !!data.checksEnabled;
      $("fwShowBadge").checked = !!data.showBadge;
      $("fwAutoDownload").checked = !!data.autoDownload;
      const fwAction = $("fwDownloadNow");
      fwAction.dataset.action = data.staged ? "confirm" : "download";
      fwAction.textContent = data.staged ? "Install firmware" : "Download verified OTA";
      fwAction.disabled = data.downloading || (!data.staged && !data.available);
      $("fwDownloadStatus").textContent = data.downloading
        ? "Downloading and verifying: " + fmtBytes(data.downloadedBytes) + " / " + fmtBytes(data.totalBytes)
        : data.staged
          ? "Verified image staged. Select Install firmware to open the confirmation on the touchscreen."
          : data.available ? "Download is ready; installation still requires a touchscreen confirmation." : "";
      $("fwUpdateStatus").textContent = data.checking
        ? "Checking the firmware release feed..."
        : data.available
          ? "Update available: " + data.latest + " (installed: " + data.current + ")"
          : data.error
            ? "Update check: " + data.error
            : "Installed: " + data.current + " · No newer compatible release found.";
    } catch (error) {
      $("fwUpdateStatus").textContent = "Unable to read firmware update status.";
    }
  }
  async function saveFirmwareUpdatePreferences() {
    const body = new URLSearchParams({
      checksEnabled: $("fwChecksEnabled").checked ? "1" : "0",
      showBadge: $("fwShowBadge").checked ? "1" : "0",
      autoDownload: $("fwAutoDownload").checked ? "1" : "0"
    });
    try {
      await getJson("/api/firmware/updates/preferences", {
        method: "POST", body
      });
      await refreshFirmwareUpdateStatus();
    } catch (error) {
      showToast("Failed to save firmware update preferences", true);
    }
  }
  $("fwChecksEnabled").addEventListener("change", saveFirmwareUpdatePreferences);
  $("fwShowBadge").addEventListener("change", saveFirmwareUpdatePreferences);
  $("fwAutoDownload").addEventListener("change", saveFirmwareUpdatePreferences);
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
    try {
      await getJson("/api/firmware/updates/check", { method: "POST" });
      $("fwUpdateStatus").textContent = "Update check requested...";
    } catch (error) {
      showToast("Cannot start update check", true);
    }
  });
  refreshFirmwareUpdateStatus();
  setInterval(() => {
    if (document.querySelector(".page.active")?.dataset.page === "maintenance")
      refreshFirmwareUpdateStatus();
  }, 15000);

  function uploadFirmware(file) {
    if (!file || !file.name.toLowerCase().endsWith(".bin")) {
      showToast("Choose a firmware .bin file", true);
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
      let data = {};
      try {
        data = JSON.parse(xhr.responseText);
      } catch (e) {}
      if (xhr.status >= 200 && xhr.status < 300) {
        $("otaBar").style.width = "100%";
        $("otaText").textContent = "Update complete. Device is restarting.";
        showToast(data.message || "Firmware installed", false);
      } else {
        $("otaText").textContent = data.message || "Update failed";
        showToast($("otaText").textContent, true);
      }
    };
    xhr.onerror = () => {
      showToast("Firmware upload failed", true);
      $("otaText").textContent = "Upload failed";
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
  async function loadBedroomIcons() {
    const status = $("bedroomIconsStatus"), list = $("bedroomIconsList");
    try {
      const data = await getJson("/api/bedroom/icons");
      list.replaceChildren();
      const mounted = !!data.mounted;
      $("bedroomIconsBrowse").disabled = !mounted;
      status.textContent = mounted
        ? "microSD connected • " + (data.files || []).length + " icon(s) saved"
        : "microSD not mounted. Enable it above, save and restart before uploading.";
      for (const entry of data.files || []) {
        const name = String(entry.name || "");
        if (!bedroomIconValid(name)) continue;
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
      status.textContent = "Could not read icons: " + e.message;
      $("bedroomIconsBrowse").disabled = true;
    }
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
  // Load on demand when the Maintenance section is opened.
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
        img.loading = "lazy";
        img.alt = f.n;
        img.src =
          "/api/deck/walls/file?o=" +
          o +
          "&gallery=" +
          wallGallery() +
          "&n=" +
          encodeURIComponent(f.n) +
          "&v=" +
          wallCache;
        img.style.cssText = "width:100%;height:80px;object-fit:cover;border-radius:4px";
        s.imgs.set(f.n, img);
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
      button.append(img, label, mark);
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
          ? 'Pictures are stored internally. To use a microSD card, enable it in <a href="#" data-page="maintenance">Maintenance &gt; microSD card</a>.'
          : !sd.mounted
            ? 'microSD card not mounted, using internal storage. See <a href="#" data-page="maintenance">Maintenance &gt; microSD card</a>.'
            : "Stored on the microSD card.";
        $("sdFormat").classList.toggle("hidden", !sd.enabled);
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
                            ["display_name", "prDisplayName"],
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
  (async () => {
    await loadConfig();
    if (initial === "printer" && !printerAvailable) initial = "overview";
    await showPage(initial);
  })();
  pollStatus();
  refreshPreview();
  setInterval(pollStatus, 2000);
})();
