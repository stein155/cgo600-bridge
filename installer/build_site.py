"""Assemble the web installer site (GitHub Pages) from built firmware.

Expects both variants to be built already:
    pio run -d firmware && pio run -d firmware -t buildfs
    esphome -s project_version <version> compile esphome/.install-ci.yaml

Usage:
    python3 installer/build_site.py --version 1.2.0 [--out site]

esp-web-tools fetches the manifests and binaries with fetch(), and GitHub
release assets are not served with CORS headers, so everything is published
on the same Pages origin as the page itself.
"""

import argparse
import json
import os
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO_BUILD = os.path.join(ROOT, "firmware", ".pio", "build", "esp32")
ESPHOME_BUILD = os.path.join(ROOT, "esphome", ".esphome", "build", "cgo600-bridge", ".pioenvs", "cgo600-bridge")
BOOT_APP0 = os.path.expanduser(
    "~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
)

# Flash layout of the standalone firmware (see firmware/partitions.csv).
STANDALONE_PARTS = [
    ("bootloader.bin", os.path.join(PIO_BUILD, "bootloader.bin"), 0x1000),
    ("partitions.bin", os.path.join(PIO_BUILD, "partitions.bin"), 0x8000),
    ("boot_app0.bin", BOOT_APP0, 0xE000),
    ("firmware.bin", os.path.join(PIO_BUILD, "firmware.bin"), 0x10000),
    ("littlefs.bin", os.path.join(PIO_BUILD, "littlefs.bin"), 0x3D0000),
]


def _copy(src, dest):
    if not os.path.isfile(src):
        raise SystemExit("[SITE] Missing build output: %s" % src)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.copyfile(src, dest)


def _write_manifest(path, manifest):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")


def build(version, out_dir):
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)

    shutil.copyfile(os.path.join(ROOT, "installer", "index.html"), os.path.join(out_dir, "index.html"))

    parts = []
    for name, src, offset in STANDALONE_PARTS:
        _copy(src, os.path.join(out_dir, "standalone", name))
        parts.append({"path": "standalone/" + name, "offset": offset})
    _write_manifest(
        os.path.join(out_dir, "manifest-standalone.json"),
        {
            "name": "CGO600 Bridge (standalone)",
            "version": version,
            "new_install_prompt_erase": True,
            "builds": [{"chipFamily": "ESP32", "parts": parts}],
        },
    )

    _copy(
        os.path.join(ESPHOME_BUILD, "firmware.factory.bin"),
        os.path.join(out_dir, "esphome", "cgo600-bridge.factory.bin"),
    )
    _write_manifest(
        os.path.join(out_dir, "manifest-esphome.json"),
        {
            "name": "CGO600 Bridge (ESPHome)",
            "version": version,
            "home_assistant_domain": "esphome",
            "new_install_prompt_erase": True,
            "builds": [
                {
                    "chipFamily": "ESP32",
                    "parts": [{"path": "esphome/cgo600-bridge.factory.bin", "offset": 0}],
                }
            ],
        },
    )
    print("[SITE] Installer for %s written to %s" % (version, out_dir))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", default=os.path.join(ROOT, "site"))
    args = parser.parse_args()
    build(args.version, args.out)
