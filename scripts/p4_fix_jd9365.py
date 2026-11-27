"""ESP32-P4 rev3 JD9365 DSI PLL clock compatibility patch.

The upstream chipguy_JC8012P4A1C_display driver obtains the legacy
MIPI_DSI_PHY_CLK_SRC_DEFAULT clock from JD9365_PANEL_BUS_DSI_2CH_CONFIG().
The rev3 ESP32-P4 HAL rejects PLL_F20M and aborts at DSI bus creation.
Until the dependency adopts a revision-safe default, correct it locally.

Runs only in the experimental jc8012p4a1c PlatformIO environment. The
upstream library stays untouched in Git; the installed libdeps copy is
patched idempotently and an unknown upstream layout fails explicitly.
"""
from pathlib import Path
from shlex import split

Import("env")

# Update the board define before PlatformIO loads the framework, so the
# custom SDK and Arduino stages both see exactly one CDC setting.
cdc_on_boot = env.GetProjectOption("custom_p4_usb_cdc", "")
if cdc_on_boot:
    board = env.BoardConfig()
    flags = board.get("build.extra_flags", [])
    if isinstance(flags, str):
        flags = split(flags)
    flags = [flag for flag in flags if not flag.startswith("-DARDUINO_USB_CDC_ON_BOOT=")]
    board.update("build.extra_flags", [*flags, f"-DARDUINO_USB_CDC_ON_BOOT={cdc_on_boot}"])

project = Path(env.subst("$PROJECT_DIR"))
library_root = project / ".pio" / "libdeps" / env.subst("$PIOENV")
candidates = sorted(library_root.glob("*/src/chipguy_JC8012P4A1C_display.cpp"))
if len(candidates) != 1:
    raise RuntimeError(
        "P4 DSI compatibility patch: expected one installed Guition "
        f"display driver in {library_root}, found {len(candidates)}. "
        "Run the PlatformIO build once to install lib_deps, then rebuild."
    )

source_file = candidates[0]
source = source_file.read_text(encoding="utf-8")
anchor = "esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();"
patch_comment = "// TACTULA_P4_REV3_DSI_PHY_PLLREF_FIX"
replacement = (
    anchor
    + "\n"
    + "    " + patch_comment + "\n"
    + "#if defined(CONFIG_IDF_TARGET_ESP32P4) && !CONFIG_ESP32P4_SELECTS_REV_LESS_V3\n"
    + "    // Rev3 HAL rejects the legacy PLL_F20M default. Use XTAL instead.\n"
    + "    bus_config.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;\n"
    + "#endif"
)
if patch_comment in source:
    print("[P4] JD9365 rev3 DSI PLL clock patch already applied")
elif source.count(anchor) == 1:
    source_file.write_text(source.replace(anchor, replacement, 1), encoding="utf-8")
    print(f"[P4] Applied JD9365 rev3 DSI PLL clock fix: {source_file}")
else:
    raise RuntimeError(
        "P4 DSI compatibility patch: expected upstream bus config anchor missing "
        "or ambiguous. Inspect the updated display dependency."
    )

# The newer V2/V3 glass uses a different vendor init table and 80 MHz DPI
# timing. This table is from Guition's NewPanel bundle, not a timing guess.
# Use only in the isolated P4 test. Keep original driver data intact for the
# older panel, should the user need to select it later.
panel_header = project / "include" / "p4_jd9365_new_panel.h"
if not panel_header.exists():
    raise RuntimeError(f"Missing checked-in new-panel command table: {panel_header}")

driver_dir = source_file.parent
injected_header = driver_dir / "tactula_p4_newpanel.h"
new_panel_contents = panel_header.read_text(encoding="utf-8")
if not injected_header.exists() or injected_header.read_text(encoding="utf-8") != new_panel_contents:
    injected_header.write_text(new_panel_contents, encoding="utf-8")
    print(f"[P4] Installed V2/V3 JD9365 init table: {injected_header}")

source = source_file.read_text(encoding="utf-8")
table_patch_marker = "// TACTULA_P4_NEW_PANEL_INIT_TABLE"
header_anchor = '#include "esp_lcd_jd9365.h"'
if table_patch_marker not in source:
    if source.count(header_anchor) != 1:
        raise RuntimeError("P4 new-panel patch: JD9365 include anchor missing or ambiguous")
    source = source.replace(
        header_anchor,
        header_anchor + "\n" + table_patch_marker + '\n#include "tactula_p4_newpanel.h"',
        1,
    )
    old = """    extern const jd9365_lcd_init_cmd_t vendor_specific_init_default[];
    extern const size_t vendor_specific_init_default_size;"""
    if source.count(old) != 1:
        raise RuntimeError("P4 new-panel patch: vendor command declarations changed")
    source = source.replace(
        old,
        """    const jd9365_lcd_init_cmd_t *panel_vendor_cmds =
        guition_jc8012p4a1_v2_init_cmds;
    const size_t panel_vendor_cmd_count =
        guition_jc8012p4a1_v2_init_cmds_count;""",
        1,
    )
    if source.count("vendor_specific_init_default_size") != 2:
        raise RuntimeError("P4 new-panel patch: unexpected command count references")
    source = source.replace("vendor_specific_init_default_size", "panel_vendor_cmd_count")
    if source.count("vendor_specific_init_default[i]") != 1:
        raise RuntimeError("P4 new-panel patch: command pointer reference changed")
    source = source.replace("vendor_specific_init_default[i]", "panel_vendor_cmds[i]", 1)
    source_file.write_text(source, encoding="utf-8")
    print("[P4] Applied 204-command newer JD9365 V2/V3 panel initialisation table")
else:
    print("[P4] Newer JD9365 V2/V3 panel init already applied")

# EspControl's V3 production-silicon profile uses 840 Mbps lanes, not
# the 1500 Mbps of the older board driver. Test the lower lane bitrate
# independently: keep the currently functional DPI clock for this run.
# Changing DPI to 70 MHz requires reworking the driver's clock-source
# selection (it only accepts integer divisors of 240 or 160 MHz).
source = source_file.read_text(encoding="utf-8")
lane_marker = "// TACTULA_P4_V3_840MBPS_LANES"
lane_anchor = "esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();"
if lane_marker not in source:
    if source.count(lane_anchor) != 1:
        raise RuntimeError("P4 V3 lane patch: bus config anchor changed")
    source = source.replace(
        lane_anchor,
        lane_anchor + "\n    " + lane_marker
        + "\n    bus_config.lane_bit_rate_mbps = 840;",
        1,
    )
    source_file.write_text(source, encoding="utf-8")
    print("[P4] Applied EspControl V3 840 Mbps MIPI lane setting")
else:
    print("[P4] EspControl V3 840 Mbps lane setting already applied")

# The original flip() only queues work to a PPA task, while LVGL currently
# signals flush complete immediately. Touch-induced partial redraws can mutate
# the source during DMA. Add an explicit completion barrier, and only switch
# the DSI scanout buffer at vsync after the PPA finishes rotating it.
header_file = source_file.with_suffix(".h")
header_source = header_file.read_text(encoding="utf-8")
source = source_file.read_text(encoding="utf-8")
sync_marker = "// TACTULA_P4_PPA_FLIP_WAIT_API"
header_anchor = "    void flip(uint8_t draw_buf_index, uint8_t target_fb_index);"
if sync_marker not in header_source:
    if header_source.count(header_anchor) != 1:
        raise RuntimeError("P4 PPA sync patch: flip declaration missing or ambiguous")
    header_source = header_source.replace(
        header_anchor,
        header_anchor + "\n\n    " + sync_marker
        + "\n    bool waitFlipComplete(uint32_t timeout_ms = 500);",
        1,
    )
    header_file.write_text(header_source, encoding="utf-8")
    print("[P4] Added waitFlipComplete API declaration")
else:
    print("[P4] PPA completion API declaration already applied")

wait_impl_marker = "// TACTULA_P4_PPA_FLIP_WAIT_IMPL"
next_anchor = "bool chipguy_JC8012P4A1C_display::waitVsync(uint32_t timeout_ms)"
if wait_impl_marker not in source:
    if source.count(next_anchor) != 1:
        raise RuntimeError("P4 PPA sync patch: waitVsync anchor missing or ambiguous")
    impl = """// TACTULA_P4_PPA_FLIP_WAIT_IMPL
bool chipguy_JC8012P4A1C_display::waitFlipComplete(uint32_t timeout_ms)
{
    if (!_ppa_done_sem) return false;
    if (xSemaphoreTake(_ppa_done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "PPA flip completion timed out after %lu ms",
                 (unsigned long)timeout_ms);
        return false;
    }
    xSemaphoreGive(_ppa_done_sem);
    return true;
}

"""
    source = source.replace(next_anchor, impl + next_anchor, 1)
    print("[P4] Added blocking PPA completion barrier")

# The LCD DMA must scan a finished framebuffer. The target is always the
# inactive buffer selected by the test's alternating FB index.
vsync_marker = "// TACTULA_P4_PPA_VSYNC_SWITCH"
old_switch = "            self->setActiveFramebuffer(w.target_fb_index, false);"
if vsync_marker not in source:
    if source.count(old_switch) != 1:
        raise RuntimeError("P4 PPA sync patch: framebuffer switch anchor changed")
    source = source.replace(
        old_switch,
        "            " + vsync_marker + "\n"
        + "            self->setActiveFramebuffer(w.target_fb_index, true);",
        1,
    )
    print("[P4] PPA now switches framebuffer at a frame boundary")

# The dashboard presents its splash before applying the saved brightness.
# Keep the vendor driver from briefly turning the backlight on at full power.
backlight_marker = "// TACTULA_P4_MANAGED_STARTUP_BACKLIGHT"
backlight_anchor = "    setBacklight(100);"
if backlight_marker not in source:
    if source.count(backlight_anchor) != 1:
        raise RuntimeError("P4 startup backlight patch: expected default brightness call changed")
    source = source.replace(
        backlight_anchor,
        "    " + backlight_marker + "\n"
        "#if !defined(TACTULA_P4_MANAGED_BACKLIGHT)\n"
        + backlight_anchor + "\n"
        "#endif",
        1,
    )
# The dashboard copies each frame into staging buffer 0 and waits for the
# PPA before reuse. It still has two LVGL buffers and two scanout buffers.
staging_marker = "// TACTULA_P4_SINGLE_DASHBOARD_STAGING_BUFFER"
staging_anchor = "bool chipguy_JC8012P4A1C_display::allocateDrawBuffers()"
if staging_marker not in source:
    start = source.index(staging_anchor)
    end = source.index("bool chipguy_JC8012P4A1C_display::begin(", start)
    allocation = source[start:end]
    loop = "    for (int i = 0; i < 2; i++) {"
    if allocation.count(loop) != 1:
        raise RuntimeError("P4 staging buffer patch: allocation loop changed")
    allocation = allocation.replace(
        loop,
        "    " + staging_marker + "\n"
        "#if defined(TACTULA_P4_MANAGED_BACKLIGHT)\n"
        "    constexpr int draw_buffer_count = 1;\n"
        "#else\n"
        "    constexpr int draw_buffer_count = 2;\n"
        "#endif\n"
        "    for (int i = 0; i < draw_buffer_count; i++) {",
        1,
    )
    source = source[:start] + allocation + source[end:]
    print("[P4] Dashboard uses one PPA staging buffer")

# A worker semaphore only proves the PPA task finished, not that scanout
# accepted the frame. Check the buffer the DMA actually selected.
handoff_marker = "// TACTULA_P4_CHECKED_FRAME_HANDOFF"
dpi_file = source_file.with_name("esp_lcd_panel_dpi_bb.c")
dpi_header_file = dpi_file.with_suffix(".h")
dpi_source = dpi_file.read_text(encoding="utf-8")
dpi_header = dpi_header_file.read_text(encoding="utf-8")

def replace_once(content, before, after):
    if content.count(before) != 1:
        raise RuntimeError(f"P4 checked frame handoff: expected one {before!r} anchor")
    return content.replace(before, after, 1)

if handoff_marker not in dpi_source:
    dpi_source = replace_once(dpi_source, "    uint8_t cur_fb_index;", "    volatile uint8_t cur_fb_index;")
    dpi_source = replace_once(dpi_source, "    uint8_t  scan_fb;", "    volatile uint8_t scan_fb;")
    anchor = "uint8_t chipguy_lcd_dpi_panel_get_num_fbs("
    helper = """// TACTULA_P4_CHECKED_FRAME_HANDOFF
bool chipguy_lcd_dpi_panel_fb_scanning(esp_lcd_panel_handle_t panel, uint8_t index)
{
    if (!panel) return false;
    esp_lcd_dpi_panel_t *dpi_panel = __containerof(panel, esp_lcd_dpi_panel_t, base);
    return dpi_panel->scan_fb == index;
}

int chipguy_lcd_dpi_panel_idle_fb(esp_lcd_panel_handle_t panel)
{
    if (!panel) return -1;
    esp_lcd_dpi_panel_t *dpi_panel = __containerof(panel, esp_lcd_dpi_panel_t, base);
    // The worker is the only switch requester on the dashboard. If the
    // last request hasn't been latched yet, neither buffer is safe to write.
    uint8_t requested = dpi_panel->cur_fb_index;
    if (dpi_panel->num_fbs != 2 || dpi_panel->scan_fb != requested) return -1;
    return requested ^ 1;
}

"""
    dpi_source = replace_once(dpi_source, anchor, helper + anchor)
    dpi_file.write_text(dpi_source, encoding="utf-8")
if handoff_marker not in dpi_header:
    anchor = "uint8_t chipguy_lcd_dpi_panel_get_num_fbs("
    dpi_header = replace_once(
        dpi_header, anchor, handoff_marker + "\n"
        "bool chipguy_lcd_dpi_panel_fb_scanning(esp_lcd_panel_handle_t panel, uint8_t index);\n"
        "int chipguy_lcd_dpi_panel_idle_fb(esp_lcd_panel_handle_t panel);\n\n" + anchor,
    )
    dpi_header_file.write_text(dpi_header, encoding="utf-8")

header_source = header_file.read_text(encoding="utf-8")
if handoff_marker not in header_source:
    header_source = replace_once(
        header_source, "    bool waitFlipComplete(uint32_t timeout_ms = 500);",
        "    bool waitFlipComplete(uint32_t timeout_ms = 500, bool *presented = nullptr);",
    )
    header_source = replace_once(
        header_source, "    SemaphoreHandle_t _ppa_done_sem = nullptr;",
        "    " + handoff_marker + "\n"
        "    bool _tactula_flip_presented = false;\n"
        "    SemaphoreHandle_t _ppa_done_sem = nullptr;",
    )
    header_file.write_text(header_source, encoding="utf-8")

if handoff_marker not in source:
    source = replace_once(
        source, "        auto &w = self->_ppa_work;",
        "        auto &w = self->_ppa_work;\n"
        "#if defined(TACTULA_P4_MANAGED_BACKLIGHT)\n"
        "        const int target = chipguy_lcd_dpi_panel_idle_fb(self->_panel_handle);\n"
        "        if (target < 0) {\n"
        "            ESP_LOGW(TAG, \"PPA frame skipped: previous scanout switch is pending\");\n"
        "            xSemaphoreGive(self->_ppa_done_sem);\n"
        "            continue;\n"
        "        }\n"
        "        w.target_fb_index = static_cast<uint8_t>(target);\n"
        "#endif",
    )
    source = replace_once(
        source, "            esp_cache_msync(target_fb, PANEL_FB_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_M2C);",
        "            ret = esp_cache_msync(target_fb, PANEL_FB_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_M2C);\n"
        "            if (ret != ESP_OK) {\n"
        "                ESP_LOGE(TAG, \"PPA target cache sync failed: %s\", esp_err_to_name(ret));\n"
        "                xSemaphoreGive(self->_ppa_done_sem);\n"
        "                continue;\n"
        "            }",
    )
    source = replace_once(
        source, "            self->setActiveFramebuffer(w.target_fb_index, true);",
        "            " + handoff_marker + "\n"
        "            self->setActiveFramebuffer(w.target_fb_index, true);\n"
        "            self->_tactula_flip_presented = chipguy_lcd_dpi_panel_fb_scanning(\n"
        "                self->_panel_handle, w.target_fb_index);\n"
        "            if (!self->_tactula_flip_presented)\n"
        "                ESP_LOGW(TAG, \"PPA scanout switch to framebuffer %u not confirmed\",\n"
        "                         (unsigned)w.target_fb_index);",
    )
    source = replace_once(
        source, "    esp_cache_msync(src, framebufferSize(), ESP_CACHE_MSYNC_FLAG_DIR_C2M);",
        "    _tactula_flip_presented = false;\n"
        "    const esp_err_t sync = esp_cache_msync(src, framebufferSize(), ESP_CACHE_MSYNC_FLAG_DIR_C2M);\n"
        "    if (sync != ESP_OK) {\n"
        "        ESP_LOGE(TAG, \"PPA source cache sync failed: %s\", esp_err_to_name(sync));\n"
        "        xSemaphoreGive(_ppa_done_sem);\n"
        "        return;\n"
        "    }",
    )
    source = replace_once(
        source, "bool chipguy_JC8012P4A1C_display::waitFlipComplete(uint32_t timeout_ms)",
        "bool chipguy_JC8012P4A1C_display::waitFlipComplete(uint32_t timeout_ms, bool *presented)",
    )
    source = replace_once(
        source, "    xSemaphoreGive(_ppa_done_sem);\n    return true;\n}",
        "    if (presented) *presented = _tactula_flip_presented;\n"
        "    xSemaphoreGive(_ppa_done_sem);\n    return true;\n}",
    )
    print("[P4] Added checked scanout handoff and DMA cache diagnostics")

# Dashboard diagnostics read the framebuffer that the completed PPA work
# actually selected. The caller holds the staging buffer until it samples it.
header_source = header_file.read_text(encoding="utf-8")
output_accessor = "    const uint16_t* lastPresentedFramebuffer()"
if output_accessor not in header_source:
    declaration = "    bool waitFlipComplete(uint32_t timeout_ms = 500, bool *presented = nullptr);"
    header_source = replace_once(
        header_source, declaration,
        declaration + "\n" + output_accessor + " {\n"
        "        return _tactula_flip_presented ? getFramebuffer(_ppa_work.target_fb_index) : nullptr;\n"
        "    }",
    )
    header_file.write_text(header_source, encoding="utf-8")

native_marker = "// TACTULA_P4_NATIVE_SCANOUT_ACCESS"
if native_marker not in header_source:
    declaration = "    void setActiveFramebuffer(uint8_t index, bool wait_for_vsync = true);"
    header_source = replace_once(
        header_source, declaration, declaration + "\n"
        "    " + native_marker + "\n"
        "    int idleFramebufferIndex() const;\n"
        "    bool isFramebufferScanning(uint8_t index) const;",
    )
    header_file.write_text(header_source, encoding="utf-8")
if native_marker not in source:
    anchor = "void chipguy_JC8012P4A1C_display::ppaTaskFunc(void *arg)"
    access = """// TACTULA_P4_NATIVE_SCANOUT_ACCESS
int chipguy_JC8012P4A1C_display::idleFramebufferIndex() const
{
    return chipguy_lcd_dpi_panel_idle_fb(_panel_handle);
}

bool chipguy_JC8012P4A1C_display::isFramebufferScanning(uint8_t index) const
{
    return chipguy_lcd_dpi_panel_fb_scanning(_panel_handle, index);
}

"""
    source = replace_once(source, anchor, access + anchor)
    print("[P4] Exposed checked scanout framebuffer access")

source_file.write_text(source, encoding="utf-8")

# The FIFO underrun interrupt does not measure gaps between DMA completions.
# Record late frame boundaries in the ISR so short stalls survive log polling.
timing_marker = "// TACTULA_P4_SCANOUT_TIMING"
if timing_marker not in dpi_source:
    dpi_source = replace_once(
        dpi_source, "static volatile uint32_t g_frame_count = 0;",
        """static volatile uint32_t g_frame_count = 0;

#if defined(TACTULA_P4_MANAGED_BACKLIGHT)
""" + timing_marker + """
static portMUX_TYPE tactula_scanout_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t tactula_previous_frame_us;
static uint32_t tactula_worst_gap_us;
static uint32_t tactula_late_frames;
static uint32_t tactula_last_late_us;

void tactula_p4_scanout_timing(uint32_t *worst_gap_us, uint32_t *late_frames, uint32_t *last_late_us)
{
    portENTER_CRITICAL(&tactula_scanout_mux);
    *worst_gap_us = tactula_worst_gap_us;
    *late_frames = tactula_late_frames;
    *last_late_us = tactula_last_late_us;
    tactula_worst_gap_us = 0;
    tactula_late_frames = 0;
    portEXIT_CRITICAL(&tactula_scanout_mux);
}
#endif""",
    )
    dpi_source = replace_once(
        dpi_source, "    dw_gdma_channel_enable_ctrl(chan, true);",
        """    dw_gdma_channel_enable_ctrl(chan, true);
#if defined(TACTULA_P4_MANAGED_BACKLIGHT)
    // Normal cadence with the dashboard's 80 MHz timing is about 16.7 ms.
    if (tactula_previous_frame_us) {
        uint32_t gap_us = (uint32_t)(now - tactula_previous_frame_us);
        portENTER_CRITICAL_ISR(&tactula_scanout_mux);
        if (gap_us > tactula_worst_gap_us) tactula_worst_gap_us = gap_us;
        if (gap_us >= 20000) {
            tactula_late_frames++;
            tactula_last_late_us = (uint32_t)now;
        }
        portEXIT_CRITICAL_ISR(&tactula_scanout_mux);
    }
    tactula_previous_frame_us = now;
#endif""",
    )
    dpi_file.write_text(dpi_source, encoding="utf-8")
    print("[P4] Added DMA frame-boundary timing counters")

# The display DMA stops at each frame boundary until its interrupt restarts
# it. Give that interrupt precedence over level-1 work on the dashboard.
priority_marker = "// TACTULA_P4_SCANOUT_IRQ_PRIORITY"
if priority_marker not in dpi_source:
    callbacks_start = dpi_source.index("    dw_gdma_event_callbacks_t dsi_dma_cbs = {")
    callbacks_end = dpi_source.index("\n    return ESP_OK;", callbacks_start)
    callbacks = dpi_source[callbacks_start:callbacks_end].strip("\n")
    register = '    ESP_RETURN_ON_ERROR(dw_gdma_channel_register_event_callbacks(dma_chan, &dsi_dma_cbs, dpi_panel), TAG, "register DMA callbacks failed");'
    callback_config = callbacks.removesuffix(register).rstrip()
    if callback_config == callbacks or not callback_config.endswith("};"):
        raise RuntimeError("P4 scanout interrupt priority: unexpected DMA callback setup")
    dpi_source = replace_once(
        dpi_source, callbacks,
        """    if (!registered) {
    """ + register + """
    }""",
    )
    channel = '    ESP_RETURN_ON_ERROR(dw_gdma_new_channel(&dma_alloc_config, &dma_chan), TAG, "create DMA channel failed");'
    dpi_source = replace_once(
        dpi_source, channel,
        callback_config + "\n    " + priority_marker + """
    bool registered = false;
#if defined(TACTULA_P4_MANAGED_BACKLIGHT)
    dma_alloc_config.intr_priority = 2;
    esp_err_t irq_result = dw_gdma_new_channel(&dma_alloc_config, &dma_chan);
    if (irq_result == ESP_OK) {
        irq_result = dw_gdma_channel_register_event_callbacks(dma_chan, &dsi_dma_cbs, dpi_panel);
        if (irq_result != ESP_OK) {
            ESP_RETURN_ON_ERROR(dw_gdma_del_channel(dma_chan), TAG, "release DMA channel failed");
            dma_chan = NULL;
        }
    }
    registered = irq_result == ESP_OK;
    if (registered) {
        ESP_LOGI(TAG, "Tactula scanout DMA interrupt priority: 2");
    } else {
        ESP_LOGW(TAG, "Tactula scanout DMA using default interrupt priority: %s", esp_err_to_name(irq_result));
        dma_alloc_config.intr_priority = 0;
    }
#endif
    if (!registered) {
    """ + channel + """
    }""",
    )
    dpi_file.write_text(dpi_source, encoding="utf-8")
    print("[P4] Preferring priority 2 for dashboard scanout DMA interrupts")


# Opt-in fixed-frame hardware refresh experiment. The normal startup and
# double-buffered path are unchanged. Exit the experiment by rebooting.
reload_marker = "// TACTULA_P4_STATIC_RELOAD_TEST"
dpi_source = dpi_file.read_text(encoding="utf-8")
dpi_header = dpi_header_file.read_text(encoding="utf-8")
header_source = header_file.read_text(encoding="utf-8")
source = source_file.read_text(encoding="utf-8")
if reload_marker not in dpi_source:
    anchor = "uint8_t chipguy_lcd_dpi_panel_get_num_fbs("
    implementation = """// TACTULA_P4_STATIC_RELOAD_TEST
// Hardware experiment: take over only a quiesced, contiguous scanout.
// No new buffer or framebuffer switch is permitted until reboot.
bool chipguy_lcd_dpi_panel_test_reload(esp_lcd_panel_handle_t panel)
{
    if (!panel) return false;
    esp_lcd_dpi_panel_t *p = __containerof(panel, esp_lcd_dpi_panel_t, base);
    const int ch = p->dma_ch_id;
    if (ch < 0 || ch >= sizeof(DW_GDMA.ch) / sizeof(DW_GDMA.ch[0])) return false;
    const uint8_t fb = p->scan_fb;
    if (fb >= p->num_fbs || p->cur_fb_index != fb || p->paused ||
        p->layout_pending || p->raster_pending || p->blocks_pending ||
        p->list_len[fb] != 1 || p->raster.chunks || p->n_blocks) return false;
    const size_t bytes = p->bytes_per_row * p->v_pixels;
    volatile dmac_channel_reg_t *regs = &DW_GDMA.ch[ch];
    if (regs->cfg0.src_multblk_type != DW_GDMA_BLOCK_TRANSFER_LIST ||
        regs->cfg0.dst_multblk_type != DW_GDMA_BLOCK_TRANSFER_LIST ||
        ((size_t)regs->block_ts0.block_ts + 1) * 8 != bytes) return false;
    // Descriptor SAR contains the bus-visible address, including any alias.
    const uint32_t reading = regs->sar0.sar0;
    const uint32_t cpu = (uint32_t)p->fbs[fb];
    const uint32_t offset = (reading - cpu) & 0x0fffffff;
    if (offset >= bytes) return false;
    const uint32_t base = reading - offset;

    // Reuse the vendor's existing frame-boundary pause handshake, but leave
    // the DSI bridge in video mode. The entry itself may miss a frame.
    p->stopped = false;
    p->paused = true;
    const int64_t deadline = esp_timer_get_time() + 100000;
    while (!p->stopped && esp_timer_get_time() < deadline) {
        vTaskDelay(1);
    }
    if (!p->stopped || (DW_GDMA.chen0.val & (1u << ch))) {
        // The channel state is uncertain; recover through normal boot.
        esp_restart();
        return false;
    }
    dw_gdma_ll_channel_enable_intr_propagation(&DW_GDMA, ch, UINT32_MAX, false);
    dw_gdma_ll_channel_clear_intr(&DW_GDMA, ch, UINT32_MAX);
    dw_gdma_ll_channel_set_src_multi_block_type(&DW_GDMA, ch, DW_GDMA_BLOCK_TRANSFER_RELOAD);
    dw_gdma_ll_channel_set_dst_multi_block_type(&DW_GDMA, ch, DW_GDMA_BLOCK_TRANSFER_RELOAD);
    dw_gdma_ll_channel_set_src_addr(&DW_GDMA, ch, base);
    dw_gdma_ll_channel_set_dst_addr(&DW_GDMA, ch, MIPI_DSI_BRG_MEM_BASE);
    dw_gdma_ll_channel_set_block_markers(&DW_GDMA, ch, false, false, false);
    dw_gdma_ll_channel_enable(&DW_GDMA, ch, true);

    // Verify movement across two samples before reporting success.
    vTaskDelay(pdMS_TO_TICKS(50) + 1);
    uint32_t before = regs->sar0.sar0;
    vTaskDelay(pdMS_TO_TICKS(3) + 1);
    if (!(DW_GDMA.chen0.val & (1u << ch)) || before == regs->sar0.sar0) {
        ESP_LOGE(TAG, "Tactula reload test failed; restarting normal driver");
        esp_restart();
        return false;
    }
    ESP_LOGI(TAG, "Tactula static DMA auto-reload active (channel %d)", ch);
    return true;
}

"""
    dpi_source = replace_once(dpi_source, anchor, implementation + anchor)
    dpi_source = replace_once(dpi_source, '#include "esp_cache.h"', '#include "esp_cache.h"\n#include "esp_system.h"')
    dpi_file.write_text(dpi_source, encoding="utf-8")
if reload_marker not in dpi_header:
    anchor = "uint8_t chipguy_lcd_dpi_panel_get_num_fbs("
    dpi_header = replace_once(dpi_header, anchor, reload_marker + "\nbool chipguy_lcd_dpi_panel_test_reload(esp_lcd_panel_handle_t panel);\n" + anchor)
    dpi_header_file.write_text(dpi_header, encoding="utf-8")
if reload_marker not in header_source:
    anchor = "    int idleFramebufferIndex() const;"
    header_source = replace_once(header_source, anchor, reload_marker + "\n    bool testStaticReload();\n" + anchor)
    header_file.write_text(header_source, encoding="utf-8")
if reload_marker not in source:
    anchor = "int chipguy_JC8012P4A1C_display::idleFramebufferIndex() const"
    source = replace_once(source, anchor, reload_marker + "\nbool chipguy_JC8012P4A1C_display::testStaticReload()\n{\n    return chipguy_lcd_dpi_panel_test_reload(_panel_handle);\n}\n\n" + anchor)
    source_file.write_text(source, encoding="utf-8")


# Live reload experiment: retain the proven fixed-address hardware refresh.
# PPA renders a complete native frame into the unused framebuffer; the
# worker copies it to the scanout buffer in bands behind the DMA read head.
# This is intentionally not an atomic framebuffer flip. A heavily delayed
# copy can tear; the DMA nevertheless keeps repeating its fixed address.
live_marker = "// TACTULA_P4_LIVE_RELOAD_TEST"
dpi_source = dpi_file.read_text(encoding="utf-8")
dpi_header = dpi_header_file.read_text(encoding="utf-8")
header_source = header_file.read_text(encoding="utf-8")
source = source_file.read_text(encoding="utf-8")
if live_marker not in dpi_source:
    anchor = "uint8_t chipguy_lcd_dpi_panel_get_num_fbs("
    implementation = """// TACTULA_P4_LIVE_RELOAD_TEST
bool chipguy_lcd_dpi_panel_reload_copy(esp_lcd_panel_handle_t panel, uint8_t index)
{
    if (!panel) return false;
    esp_lcd_dpi_panel_t *p = __containerof(panel, esp_lcd_dpi_panel_t, base);
    const int ch = p->dma_ch_id;
    const uint8_t scan = p->scan_fb;
    if (ch < 0 || ch >= sizeof(DW_GDMA.ch) / sizeof(DW_GDMA.ch[0]) ||
        p->num_fbs != 2 || scan >= p->num_fbs || index >= p->num_fbs ||
        index == scan || p->cur_fb_index != scan || !p->paused || !p->stopped)
        return false;
    volatile dmac_channel_reg_t *regs = &DW_GDMA.ch[ch];
    if (regs->cfg0.src_multblk_type != DW_GDMA_BLOCK_TRANSFER_RELOAD ||
        regs->cfg0.dst_multblk_type != DW_GDMA_BLOCK_TRANSFER_RELOAD)
        return false;
    const size_t bytes = p->bytes_per_row * p->v_pixels;
    if (bytes == 0 || (bytes & 127)) return false;
    uint8_t *dst = p->fbs[scan];
    const uint8_t *src = p->fbs[index];
    if (!dst || !src || ((uintptr_t)dst & 127)) return false;

    // Source is already cache-invalidated after PPA. Work in aligned bands
    // and leave at least a quarter frame before DMA next reads the band.
    const size_t band = 32768;
    const int64_t deadline = esp_timer_get_time() + 250000;
    unsigned slow = 0, changed = 0;
    for (size_t off = 0; off < bytes; off += band) {
        if (esp_timer_get_time() >= deadline ||
            !(DW_GDMA.chen0.val & (1u << ch))) return false;
        size_t n = bytes - off;
        if (n > band) n = band;
        if (memcmp(dst + off, src + off, n) == 0) continue;
        for (;;) {
            if (!(DW_GDMA.chen0.val & (1u << ch)) ||
                esp_timer_get_time() >= deadline) return false;
            const size_t pos = (regs->sar0.sar0 - (uint32_t)dst) & 0x0fffffff;
            if (pos < bytes) {
                const size_t ahead = (pos + bytes - off) % bytes;
                if (ahead >= n + bytes / 8 && ahead <= 3 * bytes / 4) break;
            }
            vTaskDelay(1);
        }
        const int64_t started = esp_timer_get_time();
        memcpy(dst + off, src + off, n);
        if (esp_cache_msync(dst + off, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK)
            return false;
        ++changed;
        if (esp_timer_get_time() - started > 3000) ++slow;
    }
    static unsigned frames;
    if (slow || (++frames % 64 == 0))
        ESP_LOGI(TAG, "Live reload copy: bands=%u slow=%u", changed, slow);
    return true;
}

"""
    dpi_source = replace_once(dpi_source, anchor, implementation + anchor)
    dpi_file.write_text(dpi_source, encoding="utf-8")
if live_marker not in dpi_header:
    anchor = "uint8_t chipguy_lcd_dpi_panel_get_num_fbs("
    dpi_header = replace_once(dpi_header, anchor, live_marker + "\nbool chipguy_lcd_dpi_panel_reload_copy(esp_lcd_panel_handle_t panel, uint8_t index);\n" + anchor)
    dpi_header_file.write_text(dpi_header, encoding="utf-8")
if live_marker not in header_source:
    anchor = "    bool testStaticReload();"
    header_source = replace_once(header_source, anchor, anchor + "\n    bool testLiveReload();\n    " + live_marker)
    anchor = "    bool _tactula_flip_presented = false;"
    header_source = replace_once(header_source, anchor, anchor + "\n    bool _tactula_live_reload = false;")
    header_file.write_text(header_source, encoding="utf-8")
if live_marker not in source:
    anchor = "int chipguy_JC8012P4A1C_display::idleFramebufferIndex() const"
    source = replace_once(source, anchor, live_marker + "\nbool chipguy_JC8012P4A1C_display::testLiveReload()\n{\n    if (!testStaticReload()) return false;\n    _tactula_live_reload = true;\n    return true;\n}\n\n" + anchor)
    old = """            self->setActiveFramebuffer(w.target_fb_index, true);
            self->_tactula_flip_presented = chipguy_lcd_dpi_panel_fb_scanning(
                self->_panel_handle, w.target_fb_index);"""
    new = """            if (self->_tactula_live_reload) {
                self->_tactula_flip_presented = chipguy_lcd_dpi_panel_reload_copy(
                    self->_panel_handle, w.target_fb_index);
            } else {
                self->setActiveFramebuffer(w.target_fb_index, true);
                self->_tactula_flip_presented = chipguy_lcd_dpi_panel_fb_scanning(
                    self->_panel_handle, w.target_fb_index);
            }"""
    source = replace_once(source, old, new)
    source_file.write_text(source, encoding="utf-8")
