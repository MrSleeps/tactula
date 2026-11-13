# tactula firmware variants

**Upstream:** tactula is a fork of [TouchDeck by Rafał Stolarek (Keralots)](https://github.com/Keralots/TouchDeck). Both variants retain the original project's underlying firmware, with additional modifications in this fork. The upstream copyright and MIT licence are retained in [LICENSE](LICENSE).


## Native unified firmware

The **same repository** now contains the common bedroom functionality and the optional Bambu backend imported from upstream TouchDeck v1.3.0.

| Target | Command | Bambu | Device UI | Configuration |
| --- | --- | --- | --- | --- |
| Bedroom | `pio run -e without_bambu` | Excluded from compilation | Original custom bedroom pages, weather, climate, TV, media | Existing portal |
| Office | `pio run -e with_bambu` | LAN TLS/MQTT or Home Assistant Bambu integration | Clock starter; **P1S Printer + AMS pages appear after printer setup** | **Configuration → Bambu printer** in the main portal |

Both use the 3.5-inch Guition JC3248W535. The imported source includes printer state, AMS slots, temperature readings, Bambu print errors and chamber-light commands; the local office renderer shows essential status tiles.

The `without_bambu` environment explicitly omits `deck_bambu.cpp`, `deck_bambu_ha.cpp`, `deck_hms.cpp`, `deck_office_bambu.cpp` and `deck_ui_bambu.cpp` from compilation. The web endpoints, startup and tile rendering are also compile-time guarded. **The printer-specific error lookup table data is referenced only by excluded printer source files.** Check linker-map and binary sizes during verification.


## Names, hostnames and hotspot

Both firmware versions identify themselves as **tactula** on the splash screen, web API and portal. The build target controls only the printer feature: `without_bambu` or `with_bambu`. On a fresh device, the default DHCP/mDNS hostname is `tactula-XXXX`, where `XXXX` is a stable suffix from the ESP32 MAC address. The setup Wi-Fi access point is `tactula-XXXX` with the existing password (`touchdeck1234`).

The web portal may set a descriptive hostname, for example `tactula-bedroom` or `tactula-office`. Previously saved custom hostnames are normalised on boot, keeping the suffix but adding the `tactula-` prefix when needed. On first boot, a versioned NVS migration copies the legacy `bedroomdeck` settings into the active `touchdeck` namespace, verifies copied values and marks migration complete only after committing. If anything fails (such as insufficient NVS space), the firmware continues using `bedroomdeck` for that boot and retries at the next startup. The old namespace remains intact for rollback, rather than being erased. Home Assistant and MQTT credentials use separate NVS namespaces (`deckha`, `deckmqtt`) and are not moved by this migration. The physical Wi-Fi network (SSID) being joined is **never** renamed by firmware. Full-chip erase, partition changes or factory reset can still remove settings; back up first.

### Printer setup

1. Back up both touchscreen configurations. Do not flash an unverified CI build.
2. Build or download the **`tactula-with_bambu`** artifact. This is now the unified office target, with printer code.
3. Open the office device's web server and choose **Configuration → Bambu printer** in the normal portal sidebar.
4. Choose **P1S LAN** (printer IP/hostname, serial number and LAN access code) or **Home Assistant Bambu integration** (printer serial number; discovery button helps find it).
5. Enable and save. Serial/access code stay in NVS on the office device; the status API never returns them. The on-device **P1S Printer** and **P1S AMS** pages are generated automatically only when valid printer settings are enabled (within the eight-page limit). They appear without restarting; disabling the printer hides them.
6. Swipe sideways to **P1S Printer** for job status, progress and time remaining, nozzle/bed temperatures, active filament, layer and chamber light. Swipe vertically to **P1S AMS** for AMS status and colour-coded filament slots. Both pages are runtime-only; your own `deck.json` is not changed.

**Security:** like the existing tactula portal, printer configuration endpoints are intended for a trusted local network; the port is not an Internet-facing administration API.

### Upstream comparison and outstanding work

- [x] `-O2` optimisation
- [x] Paged Home Assistant entity picker with streaming fallback
- [x] Early network-disconnect safeguards
- [x] PSRAM allocation improvements
- [x] Avoid repeated Glass card buffer reallocation
- [x] Daily wallpaper task allocation handling
- [x] Home Assistant media artist/title/duration text
- [x] Import Bambu LAN / HA printer backend and HMS/error tables
- [x] Add office NVS configuration, printer status API and web dashboard
- [x] Add office-only P1S Printer on-device page with status tiles
- [ ] Verify the renamed without_bambu and with_bambu builds on CI; fix any compilation failures
- [ ] Test actual P1S LAN connection, Home Assistant integration, AMS trays and lamp toggles
- [ ] Validate free memory and flash size after a printer is online
- [ ] Port upstream album-cover artwork and refresh pacing
- [ ] Extend the on-device display with richer upstream-style AMS per-slot cards, fault details, print-finished alerts and energy tracking

The separate `touchdeck-office-bambu-upstream-v1.3.0` artifact is still generated as a known-good **upstream reference**, but is no longer the goal for the office unit. It does not include the customised bedroom features.

### Firmware validation

GitHub Actions builds `without_bambu`, `with_bambu` and the pinned upstream-reference binaries. Passing compilation is not proof that memory, LAN discovery or configuration migration works on physical hardware. Keep PR #34 as draft until these are tested.
