# Tactula release handoff (development source only)

**Ownership:** All development/PRs remain in the private
`MrSleeps/ESP32-Touchscreen-bedroom` repository. Public releases are created
only in `MrSleeps/tactula`. This folder stages the workflow that is copied
to the public repository. It is not itself an active dev release workflow.

## What's supported today?

- JC3248W535 without Bambu
- JC3248W535 with Bambu

The Waveshare P4 7B will automatically be included when **both**
`ws_p4_7b_without_bambu` and `ws_p4_7b_with_bambu` genuinely exist.
The experimental P4 reference files in PR #45 are NOT release-ready.

## How to publish the first release

Once the development PR is merged and the S3 builds pass, synchronise the
**approved** source snapshot into the public repository. Example from a
machine with access to both repositories:

```bash
git clone git@github.com:MrSleeps/ESP32-Touchscreen-bedroom.git dev-tactula
git clone git@github.com:MrSleeps/tactula.git public-tactula

# Review what will change; do not expose private keys or environment files.
rsync -ani --delete \
  --exclude='.git/' --exclude='.github/' --exclude='.pio/' \
  --exclude='release/' --exclude='release-tooling/' \
  --exclude='tests/' --exclude='porting/' \
  dev-tactula/ public-tactula/

# Repeat without -n ONLY after inspecting the preview.
rsync -ai --delete \
  --exclude='.git/' --exclude='.github/' --exclude='.pio/' \
  --exclude='release/' --exclude='release-tooling/' \
  --exclude='tests/' --exclude='porting/' \
  dev-tactula/ public-tactula/

# Replace only the public release workflow using its dev-maintained template.
cp dev-tactula/release-tooling/publish-release.yml \
  public-tactula/.github/workflows/publish-release.yml

cd public-tactula
python -m py_compile create_firmware.py
git diff --stat
git add -A
git commit -m 'Prepare Tactula v1.0.0 source and release workflow'
git push origin main
```

**Watch for old `docs/firmware/latest/TouchDeck-*.bin`** when reviewing:
the public repository must not advertise upstream TouchDeck binaries as
Tactula. Update the public README, credits, URL links and installation
instructions before publishing. Do not copy developer credentials,
personal settings, token files, or development Git history.

Then in the **public Tactula repository**, choose
**Releases → Draft a new release → Create new tag `v1.0.0` from main → Publish**.

The public workflow checks out that exact tag, changes the version in the
build workspace before compiling, packages *both* S3 variants, verifies the
binary count and SHA-256 checksums, and attaches the full and OTA binaries
and manifest to the **public GitHub Release**.

A future P4 port adds four more binaries automatically; no release workflow
edit is needed once the two P4 environments are implemented and validated.
No firmware has been flashed or hardware-tested by this release tooling.

## Troubleshooting

- Missing S3 build environments: wrong/stale source was copied into public.
- Build fails: inspect the GitHub Actions job; no assets should be published.
- Release exists but has no binaries: publishing triggers the workflow, which
  uploads assets only after successful build and verification.
- Never simply rename old binaries: tag `FW_VERSION` is compiled in.
