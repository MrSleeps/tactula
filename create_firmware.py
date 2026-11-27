#!/usr/bin/env python3
"""
Build and package Tactula firmware for all supported boards and variants.

Usage:
    python create_firmware.py               # version from FW_VERSION in include/config.h
    python create_firmware.py v1.0.0        # explicit version
    python create_firmware.py --board jc8012p4a1c # Guition 10.1-inch P4
    python create_firmware.py --variant with_bambu # one variant only
    # Guition JC8012P4A1C builds are opt-in
    python create_firmware.py --no-build    # package what .pio/build already has
    python create_firmware.py --flasher     # also publish the full images to the web flasher

Output (release/<version>/):
    tactula-<version>-<family>-<board>-<edition>-full.bin   bootloader + partitions + app
    tactula-<version>-<family>-<board>-<edition>-ota.bin    app only, for the portal's firmware update

With --flasher, docs/firmware/latest/ gets the full images, VERSION and SHA256SUMS.txt
(optional deployment target for the Tactula browser flasher).
"""

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

# The P4 ROM loads the bootloader from 0x2000, the S3 from 0x0.
# Build environments must be defined in the approved Tactula platformio.ini.
# Never substitute the old upstream TouchDeck/P4 firmware for a missing variant.
BOARDS = {
    "jc3248w535": {"bootloader": 0x0, "env": {
        "without_bambu": "without_bambu",
        "with_bambu": "with_bambu",
    }},
    "jc8012p4a1c": {"opt_in": True, "factory": "firmware.factory.bin", "env": {
        "with_bambu": "jc8012p4a1c_bambu",
    }},
    "p4-7b": {"bootloader": 0x2000, "env": {
        "without_bambu": "ws_p4_7b_without_bambu",
        "with_bambu": "ws_p4_7b_with_bambu",
    }},
}
VARIANTS = ("without_bambu", "with_bambu")
S3_RELEASE_TARGETS = (("jc3248w535", "without_bambu"), ("jc3248w535", "with_bambu"))
RELEASE_TARGETS = (*S3_RELEASE_TARGETS, ("jc8012p4a1c", "with_bambu"))
ARTIFACT_HARDWARE = {"jc3248w535": "s3", "jc8012p4a1c": "p4"}
ARTIFACT_EDITIONS = {"without_bambu": "standard", "with_bambu": "bambu"}
P4_BUILD_INPUTS = {"scripts/p4_fix_jd9365.py", "scripts/p4_pin_hosted_wifi.py"}
PARTITIONS_OFFSET = 0x8000
FIRMWARE_OFFSET = 0x10000

ROOT = Path(__file__).resolve().parent
FLASHER = ROOT / "docs" / "firmware" / "latest"


def artifact_name(version, board, variant, kind, *, legacy=False):
    suffix = f"{board}-{variant}"
    if not legacy and board in ARTIFACT_HARDWARE:
        suffix = f"{ARTIFACT_HARDWARE[board]}-{board}-{ARTIFACT_EDITIONS[variant]}"
    return f"tactula-{version}-{suffix}-{kind}.bin"


def read_version():
    text = (ROOT / "include" / "config.h").read_text(encoding="utf-8")
    m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', text)
    return "v" + m.group(1) if m else None


def find_pio():
    for name in ("pio", "platformio"):
        found = shutil.which(name)
        if found:
            return found
    for c in (Path.home() / ".platformio/penv/Scripts/pio.exe",
              Path.home() / ".platformio/penv/bin/pio"):
        if c.exists():
            return str(c)
    return None


def build(env):
    pio = find_pio()
    if not pio:
        sys.exit("error: PlatformIO CLI not found (PATH or ~/.platformio/penv)")
    print(f"$ {pio} run -e {env}")
    if subprocess.run([pio, "run", "-e", env], cwd=ROOT).returncode != 0:
        sys.exit("error: build failed")


def merge_full(build_dir, bootloader_offset, out):
    parts = [build_dir / "bootloader.bin", build_dir / "partitions.bin", build_dir / "firmware.bin"]
    for p in parts:
        if not p.exists():
            sys.exit(f"error: {p} not found, build first")
    bl, pt, fw = (p.read_bytes() for p in parts)
    # The 0xFF fill also leaves otadata blank, so the bootloader starts app0.
    with open(out, "wb") as f:
        f.write(b"\xFF" * bootloader_offset)
        f.write(bl)
        f.write(b"\xFF" * (PARTITIONS_OFFSET - bootloader_offset - len(bl)))
        f.write(pt)
        f.write(b"\xFF" * (FIRMWARE_OFFSET - PARTITIONS_OFFSET - len(pt)))
        f.write(fw)


def publish_flasher(fulls, version):
    FLASHER.mkdir(parents=True, exist_ok=True)
    for old in FLASHER.glob("tactula-*-full.bin"):
        old.unlink()
    sums = []
    for full in fulls:
        dst = FLASHER / full.name
        shutil.copy2(full, dst)
        sums.append(f"{hashlib.sha256(dst.read_bytes()).hexdigest()}  {dst.name}\n")
        print(f"  {dst.relative_to(ROOT)}  (web flasher, {version})")
    (FLASHER / "SHA256SUMS.txt").write_text("".join(sums), encoding="utf-8", newline="\n")
    (FLASHER / "VERSION").write_text(version + "\n", encoding="utf-8", newline="\n")


def apply_build_version(version):
    """Inject the tag version into the working copy BEFORE compilation.

    This intentionally does not commit or push include/config.h. The release
    binary and its displayed FW_VERSION must agree with the GitHub tag.
    """
    if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?", version):
        sys.exit(f"error: invalid release tag {version!r} (expected v1.2.3)")
    path = ROOT / "include" / "config.h"
    original = path.read_text(encoding="utf-8")
    data, count = re.subn(r'(#define\s+FW_VERSION\s+)"[^"]+"',
                          lambda m: m.group(1) + '"' + version[1:] + '"', original, count=1)
    if count != 1:
        sys.exit("error: FW_VERSION not found in include/config.h")
    data = re.sub(r'(#define\s+PRODUCT_NAME\s+)"[^"]+"',
                  lambda m: m.group(1) + '"tactula"', data, count=1)
    if data != original:
        path.write_text(data, encoding="utf-8")


def select_builds(requested_boards, requested_variants):
    """Require every declared environment for an explicitly requested board."""
    config = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    available = set(re.findall(r"(?m)^\[env:([^]]+)\]", config))
    boards = requested_boards or [name for name, board in BOARDS.items() if not board.get("opt_in")]
    selected = []
    for name in boards:
        environments = BOARDS[name]["env"]
        missing = [env for env in environments.values() if env not in available]
        if missing:
            if requested_boards:
                sys.exit("error: board " + name + " is missing " + ", ".join(missing))
            print("Skipping unsupported board " + name + ": " + ", ".join(missing))
            continue
        variants = requested_variants or list(environments)
        for variant in variants:
            if variant not in environments:
                sys.exit(f"error: {name} does not support variant {variant}")
            selected.append((name, variant, environments[variant]))
    if not selected:
        sys.exit("error: no complete board variants found in platformio.ini")
    return selected


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("version", nargs="?", default=None)
    ap.add_argument("--board", choices=sorted(BOARDS), action="append",
                    help="board to package, repeatable (default: all)")
    ap.add_argument("--variant", choices=VARIANTS, action="append",
                    help="variant to package, repeatable (default: both)")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--flasher", action="store_true")
    args = ap.parse_args()

    version = args.version or read_version()
    if not version:
        sys.exit("error: FW_VERSION not found in include/config.h; pass a version")
    if not version.startswith("v"):
        version = "v" + version
    selected = select_builds(args.board, args.variant)
    apply_build_version(version)
    if args.flasher and any(
        {variant for board, variant, _ in selected if board == name} != set(BOARDS[name]["env"])
        for name in {board for board, _, _ in selected}
    ):
        sys.exit("error: --flasher requires every supported edition of each selected board")

    out_dir = ROOT / "release" / version
    out_dir.mkdir(parents=True, exist_ok=True)
    fulls = []
    images = []
    for name, variant, env in selected:
        board = BOARDS[name]
        # One build at a time: parallel PlatformIO builds race on the shared cache.
        if not args.no_build:
            build(env)
        build_dir = ROOT / ".pio" / "build" / env
        full = out_dir / artifact_name(version, name, variant, "full")
        ota = out_dir / artifact_name(version, name, variant, "ota")
        if board.get("factory"):
            shutil.copy2(build_dir / board["factory"], full)
        else:
            merge_full(build_dir, board["bootloader"], full)
        shutil.copy2(build_dir / "firmware.bin", ota)
        for p in (full, ota):
            print(f"  {p.relative_to(ROOT)}  ({p.stat().st_size / 1024:.1f} KB)")
        fulls.append(full)
        images.extend((full, ota))
    manifest = {
        "version": version,
        "builds": [
            {"board": board, "variant": variant, "environment": env}
            for board, variant, env in selected
        ],
    }
    (out_dir / "MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (out_dir / "SHA256SUMS.txt").write_text(
        "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n"
                for p in sorted(images)),
        encoding="utf-8")
    if args.flasher:
        publish_flasher(fulls, version)


if __name__ == "__main__":
    main()
