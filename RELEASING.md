# Publishing Tactula

Development happens privately in `MrSleeps/ESP32-Touchscreen-bedroom`.
The public `MrSleeps/tactula` repository owns **all releases**. No release
publishing permissions or automated cross-repository pushes are needed.

## Before the first release

1. Copy the **approved, reviewed** source snapshot from the development
   repository to this public repository (not the development `.git` history).
   Review the files for credentials and personal data before publishing.
2. Keep the newer Tactula `create_firmware.py` and the
   `.github/workflows/publish-release.yml` release workflow.
3. Port/test the P4 firmware **with and without Bambu**. This repository
   currently contains upstream P4 code, not the same current Tactula firmware.
4. Define the four build environments in `platformio.ini`:
   - `without_bambu` (JC3248W535)
   - `with_bambu` (JC3248W535)
   - `ws_p4_7b_without_bambu` (Waveshare P4 7B)
   - `ws_p4_7b_with_bambu` (Waveshare P4 7B)

The release script **fails** if any of these are missing. It deliberately
will not ship a mismatched/old P4 binary. The P4 name above means the currently
declared 7B board; a 10.1-inch P4 board requires its own hardware target.

## Release procedure

1. Merge/push the tested source to Tactula's `main` branch.
2. In the Tactula GitHub repository, go to **Releases → Draft a new release**.
3. Create a tag such as `v1.0.0` **from the exact source commit** and publish.
4. GitHub Actions checks out that tag, injects `1.0.0` into the temporary
   build copy of `include/config.h`, builds all four firmware targets in
   sequence, generates checksums, and uploads the 8 BIN files to **that same
   GitHub Release**.

The code in `main` need not change its hardcoded version between releases.
The tag is the release version source of truth, including the version reported
by the compiled firmware. Do **not** just rename a precompiled binary.

For a prerelease, create a tag `v1.1.0-beta.1` and tick **Set as a pre-release**.

## Firmware files

Each board/variant has a **full** image for initial USB flashing and an
**OTA** image for updating an already installed device. The S3 full image
starts at 0x0; P4 7B uses 0x2000.

Example release assets:

```text
tactula-v1.0.0-jc3248w535-without_bambu-full.bin
tactula-v1.0.0-jc3248w535-without_bambu-ota.bin
tactula-v1.0.0-jc3248w535-with_bambu-full.bin
tactula-v1.0.0-jc3248w535-with_bambu-ota.bin
tactula-v1.0.0-p4-7b-without_bambu-full.bin
tactula-v1.0.0-p4-7b-without_bambu-ota.bin
tactula-v1.0.0-p4-7b-with_bambu-full.bin
tactula-v1.0.0-p4-7b-with_bambu-ota.bin
SHA256SUMS.txt
```

If a job fails, review its Actions logs. Fix the source and create a new
version; do not silently publish a release with missing variants.
