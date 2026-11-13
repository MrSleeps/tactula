#!/usr/bin/env python3
"""
Build and package Tactula firmware for all supported boards and variants.

Usage:
    python create_firmware.py               # version from FW_VERSION in include/config.h
    python create_firmware.py v1.0.0        # explicit version
    python create_firmware.py --board p4-7b # one board only (fails if unsupported)
    python create_firmware.py --variant with_bambu # one variant only
    # default: both S3 variants; P4 variants added automatically when supported
    python create_firmware.py --no-build    # package what .pio/build already has
    python create_firmware.py --flasher     # also publish the full images to the web flasher

Output (release/<version>/), <board> is empty for the S3 deck and "-p4-7b" for the 7B:
    tactula-<version>-<board>-<variant>-full.bin   bootloader + partitions + app, flash at 0x0
    tactula-<version>-<board>-<variant>-ota.bin    app only, for the portal's firmware update

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
    "p4-7b": {"bootloader": 0x2000, "env": {
        "without_bambu": "ws_p4_7b_without_bambu",
        "with_bambu": "ws_p4_7b_with_bambu",
    }},
}
VARIANTS = ("without_bambu", "with_bambu")
PARTITIONS_OFFSET = 0x8000
FIRMWARE_OFFSET = 0x10000

ROOT = Path(__file__).resolve().parent
FLASHER = ROOT / "docs" / "firmware" / "latest"


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
    data = path.read_text(encoding="utf-8")
    data, count = re.subn(r'(#define\s+FW_VERSION\s+)"[^"]+"',
                          lambda m: m.group(1) + '"' + version[1:] + '"', data, count=1)
    if count != 1:
        sys.exit("error: FW_VERSION not found in include/config.h")
    data = re.sub(r'(#define\s+PRODUCT_NAME\s+)"[^"]+"',
                  lambda m: m.group(1) + '"tactula"', data, count=1)
    path.write_text(data, encoding="utf-8")


def select_builds(requested_boards, requested_variants):
    """Only ship boards whose BOTH Bambu variants are implemented.

    Missing one environment is an error, never a reason to silently ship
    half a hardware target. Explicitly requested unsupported boards fail.
    """
    config = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    available = set(re.findall(r"(?m)^\[env:([^]]+)\]", config))
    boards = requested_boards or list(BOARDS)
    variants = requested_variants or list(VARIANTS)
    selected = []
    for name in boards:
        environments = BOARDS[name]["env"]
        missing = [v for v in VARIANTS if environments[v] not in available]
        if missing:
            if requested_boards:
                sys.exit("error: board " + name + " is not release-ready; missing " +
                         ", ".join(environments[v] for v in missing))
            print("Skipping unsupported board " + name + ": " +
                  ", ".join(environments[v] for v in missing))
            continue
        selected.extend((name, variant, environments[variant]) for variant in variants)
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
    if args.flasher and args.variant and set(args.variant) != set(VARIANTS):
        sys.exit("error: --flasher requires both printer variants per board")

    out_dir = ROOT / "release" / version
    out_dir.mkdir(parents=True, exist_ok=True)
    fulls = []
    for name, variant, env in selected:
        board = BOARDS[name]
        # One build at a time: parallel PlatformIO builds race on the shared cache.
        if not args.no_build:
            build(env)
        build_dir = ROOT / ".pio" / "build" / env
        full = out_dir / f"tactula-{version}-{name}-{variant}-full.bin"
        ota = out_dir / f"tactula-{version}-{name}-{variant}-ota.bin"
        merge_full(build_dir, board["bootloader"], full)
        shutil.copy2(build_dir / "firmware.bin", ota)
        for p in (full, ota):
            print(f"  {p.relative_to(ROOT)}  ({p.stat().st_size / 1024:.1f} KB)")
        fulls.append(full)
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
                for p in sorted(out_dir.glob("*.bin"))),
        encoding="utf-8")
    if args.flasher:
        publish_flasher(fulls, version)


if __name__ == "__main__":
    main()
