# Tactula firmware editions

Tactula is a touchscreen controller for the **Guition JC3248W535 ESP32-S3**, derived from [TouchDeck by Rafał Stolarek (Keralots)](https://github.com/Keralots/TouchDeck). The original MIT licence and copyright notices remain in [LICENSE](LICENSE).

## Choose an edition

| Edition | PlatformIO environment | Features |
| --- | --- | --- |
| **Standard** | `without_bambu` | Home Assistant, weather, media, pages and tiles; no Bambu printer backend |
| **With Bambu** | `with_bambu` | All Standard features, plus optional Bambu printer and AMS controls |

Both S3 editions work on the JC3248W535. The edition names are not restricted to particular rooms. A new installation begins with a basic Home page; the Bambu edition allows printer setup under **Configuration → Bambu printer**.

## Firmware downloads and installation

Get firmware from [Tactula GitHub Releases](https://github.com/MrSleeps/tactula/releases). The filename identifies the ESP32 family, board and edition.

| Hardware and edition | USB image | OTA image |
| --- | --- | --- |
| ESP32-S3 JC3248W535 Standard | `tactula-v<version>-s3-jc3248w535-standard-full.bin` | `tactula-v<version>-s3-jc3248w535-standard-ota.bin` |
| ESP32-S3 JC3248W535 Bambu | `tactula-v<version>-s3-jc3248w535-bambu-full.bin` | `tactula-v<version>-s3-jc3248w535-bambu-ota.bin` |
| ESP32-P4 JC8012P4A1C Bambu | `tactula-v<version>-p4-jc8012p4a1c-bambu-full.bin` | `tactula-v<version>-p4-jc8012p4a1c-bambu-ota.bin` |

For S3, use a **full** image for an initial USB flash at `0x0`. Use the matching **OTA** image in the device's **Maintenance → Firmware update** page or the built-in update checker. Never upload a full/factory image through OTA.

**P4 USB flashing is different from S3.** The P4's GSL3680 touch firmware is stored in NVS at `0x9000–0xDFFF`; do **not** erase flash or write a factory/full image over that region. For a P4 with Tactula's matching bootloader already installed, ordinary web OTA writes only the inactive application partition and preserves NVS. If a one-time USB repair is required, build `jc8012p4a1c_bambu` and use `scripts/flash_p4_preserve_nvs.ps1 -Environment jc8012p4a1c_bambu`, which writes only the P4 bootloader at `0x2000`, partition table at `0x8000` and app at `0x10000`, without touching the touch firmware. Its repository-local `partitions_p4_16mb.csv` matches the Arduino ESP32 `default_16MB.csv` layout, including two 6.25 MiB OTA application slots. The local copy is required for isolated P4 SDK builds. Existing devices must already have a compatible partition layout; OTA does not rewrite the partition table.

For general instructions, see [GUIDE.md](GUIDE.md#installing-tactula-firmware).

## Compatibility

New installations use `tactula-XXXX` for the default hostname and setup hotspot. Internal `touchdeck` and `bedroomdeck` settings namespaces, microSD paths and MQTT identifiers may remain for compatibility with older configurations and Home Assistant entities; they do not indicate an upstream TouchDeck installation.

Back up configuration before erasing flash or changing partition layouts. The device splash screen and portal identify the firmware as **Tactula**.
