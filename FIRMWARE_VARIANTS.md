# Tactula firmware editions

Tactula is a touchscreen controller for the **Guition JC3248W535 ESP32-S3**, derived from [TouchDeck by Rafał Stolarek (Keralots)](https://github.com/Keralots/TouchDeck). The original MIT licence and copyright notices remain in [LICENSE](LICENSE).

## Choose an edition

| Edition | PlatformIO environment | Features |
| --- | --- | --- |
| **Standard** | `without_bambu` | Home Assistant, weather, media, pages and tiles; no Bambu printer backend |
| **With Bambu** | `with_bambu` | All Standard features, plus optional Bambu printer and AMS controls |

Both editions work on the same JC3248W535 hardware. The edition names are not restricted to particular rooms. A new installation begins with a basic Home page; the Bambu edition allows printer setup under **Configuration → Bambu printer**.

## Firmware downloads and installation

Get firmware from [Tactula GitHub Releases](https://github.com/MrSleeps/tactula/releases). Each release offers four binaries:

- `tactula-v<version>-jc3248w535-without_bambu-full.bin` — Standard first-time USB installation
- `tactula-v<version>-jc3248w535-without_bambu-ota.bin` — Standard OTA update
- `tactula-v<version>-jc3248w535-with_bambu-full.bin` — Bambu first-time USB installation
- `tactula-v<version>-jc3248w535-with_bambu-ota.bin` — Bambu OTA update

Use a **full** image for an initial USB flash at `0x0`. Use the matching **OTA** image in the device's **Maintenance → Firmware update** page or the built-in update checker. Never upload a full image through the OTA page. For instructions, see [GUIDE.md](GUIDE.md#installing-tactula-firmware).

## Compatibility

New installations use `tactula-XXXX` for the default hostname and setup hotspot. Internal `touchdeck` and `bedroomdeck` settings namespaces, microSD paths and MQTT identifiers may remain for compatibility with older configurations and Home Assistant entities; they do not indicate an upstream TouchDeck installation.

Back up configuration before erasing flash or changing partition layouts. The device splash screen and portal identify the firmware as **Tactula**.
