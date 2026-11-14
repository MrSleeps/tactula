# Tactula firmware releases

Download official firmware from [MrSleeps/tactula Releases](https://github.com/MrSleeps/tactula/releases).

Each JC3248W535 release provides a Standard (`without_bambu`) and With Bambu (`with_bambu`) edition. Each edition has a `*-full.bin` image for initial USB flashing and a `*-ota.bin` image for updating an existing compatible Tactula device. Releases also provide `MANIFEST.json` and `SHA256SUMS.txt`.

See the [installation guide](GUIDE.md#installing-tactula-firmware) and [automatic OTA updates](GUIDE.md#automatic-firmware-updates). Updates use the HTTPS manifest at `https://firmware.mrsleeps.uk/tactula/releases/current.json`; installation requires confirmation on the touchscreen.

Tactula remains a credited fork of [TouchDeck by Rafał Stolarek (Keralots)](https://github.com/Keralots/TouchDeck). See [LICENSE](LICENSE).
