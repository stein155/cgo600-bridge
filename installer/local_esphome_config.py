"""Write a copy of esphome/ebike-bridge.yaml that uses the local component.

The install configuration pulls the component from the main branch on GitHub,
which is what adopted devices should track. CI and release builds must compile
the component from the checked-out commit instead, so this rewrites the
external_components source to the local esphome/components directory.

Usage:
    python3 installer/local_esphome_config.py   # writes esphome/.install-ci.yaml
"""

import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "esphome", "ebike-bridge.yaml")
OUT = os.path.join(ROOT, "esphome", ".install-ci.yaml")

LOCAL_SOURCE = ["  - source:", "      type: local", "      path: components"]


def main():
    out, in_source, replaced = [], False, False
    for line in open(SRC, encoding="utf-8").read().splitlines():
        if line.strip() == "- source:" and not replaced:
            out.extend(LOCAL_SOURCE)
            in_source, replaced = True, True
            continue
        if in_source and line.startswith("      "):
            continue  # drop the git source settings
        in_source = False
        out.append(line)
    if not replaced:
        raise SystemExit("[ESPHOME] No external_components source found in %s" % SRC)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("[ESPHOME] Wrote %s" % os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    main()
