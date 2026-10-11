"""Bundle the web UI sources in firmware/web/ into firmware/data/.

- js/*  is concatenated in JS_ORDER into a single strict-mode IIFE (app.js)
- css/* is concatenated in filename order (style.css)
- everything else in web/ is copied as-is
- every output file is stored gzipped only; ESPAsyncWebServer serves
  "<name>.gz" for "<name>" and derives the ETag from the gzip CRC

Runs automatically as a PlatformIO pre-script, or standalone:
    python3 firmware/scripts/build_web.py
"""

import gzip
import os
import shutil

JS_ORDER = [
    "constants.js",
    "dom.js",
    "format.js",
    "components.js",
    "state.js",
    "stats.js",
    "router.js",
    "views/dashboard.js",
    "views/rides.js",
    "views/ride-detail.js",
    "views/settings.js",
    "views/tabbar.js",
    "main.js",
]

JS_HEADER = "// CGO600 Bridge web UI. Generated from firmware/web/js/, do not edit.\n"
CSS_HEADER = (
    "/* CGO600 Bridge web UI styles. Generated from firmware/web/css/, do not edit.\n"
    "   Tokens mirror the design prototype (test-t/project/theme.js). */\n"
)


def _read(path):
    with open(path, encoding="utf-8") as f:
        return f.read().strip("\n")


def bundle_js(src_dir):
    js_dir = os.path.join(src_dir, "js")
    found = set()
    for root, _, files in os.walk(js_dir):
        for name in files:
            if name.endswith(".js"):
                found.add(os.path.relpath(os.path.join(root, name), js_dir).replace(os.sep, "/"))
    missing = [name for name in JS_ORDER if name not in found]
    unlisted = sorted(found - set(JS_ORDER))
    if missing or unlisted:
        raise SystemExit(
            "[WEB] JS_ORDER out of sync with web/js: missing=%s unlisted=%s" % (missing, unlisted)
        )
    body = "\n\n".join(_read(os.path.join(js_dir, name)) for name in JS_ORDER)
    return JS_HEADER + "\n(function () {\n'use strict';\n\n" + body + "\n\n})();\n"


def bundle_css(src_dir):
    css_dir = os.path.join(src_dir, "css")
    names = sorted(n for n in os.listdir(css_dir) if n.endswith(".css"))
    return CSS_HEADER + "\n" + "\n\n".join(_read(os.path.join(css_dir, n)) for n in names) + "\n"


def _write_gz(out_dir, name, data):
    path = os.path.join(out_dir, name + ".gz")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # mtime=0 keeps the output (and the served ETag) stable across builds.
    with open(path, "wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as gz:
            gz.write(data)
    return len(data), os.path.getsize(path)


def build(firmware_dir):
    src_dir = os.path.join(firmware_dir, "web")
    out_dir = os.path.join(firmware_dir, "data")
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)

    outputs = {
        "app.js": bundle_js(src_dir).encode("utf-8"),
        "style.css": bundle_css(src_dir).encode("utf-8"),
    }
    for name in sorted(os.listdir(src_dir)):
        path = os.path.join(src_dir, name)
        if os.path.isfile(path):
            with open(path, "rb") as f:
                outputs[name] = f.read()

    total_raw = total_gz = 0
    for name, data in sorted(outputs.items()):
        raw, packed = _write_gz(out_dir, name, data)
        total_raw += raw
        total_gz += packed
    print("[WEB] Built %d files into data/: %d -> %d bytes gzipped" % (len(outputs), total_raw, total_gz))


try:
    Import("env")  # noqa: F821 - provided by PlatformIO/SCons
    build(env.subst("$PROJECT_DIR"))  # noqa: F821
except NameError:
    if __name__ == "__main__":
        build(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
