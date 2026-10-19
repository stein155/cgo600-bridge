"""Bundle the web UI in web/ into gzipped assets embedded in the firmware.

- web/js/* is concatenated in JS_ORDER into a single strict-mode IIFE (app.js)
- web/css/* is concatenated in filename order (style.css)
- every other file in web/ is included as-is

Used by the component's code generation; run it directly to inspect the
bundle sizes:
    python3 esphome/components/cgo600/web_assets.py
"""

import gzip
import mimetypes
import os

WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")

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

JS_HEADER = "// CGO600 Bridge web UI. Generated from esphome/components/cgo600/web/js/, do not edit.\n"
CSS_HEADER = (
    "/* CGO600 Bridge web UI styles. Generated from esphome/components/cgo600/web/css/, do not edit.\n"
    "   Tokens mirror the design prototype (test-t/project/theme.js). */\n"
)

CONTENT_TYPES = {
    ".html": "text/html",
    ".js": "application/javascript",
    ".css": "text/css",
    ".json": "application/manifest+json",
}


def _read(path):
    with open(path, encoding="utf-8") as f:
        return f.read().strip("\n")


def _bundle_js():
    js_dir = os.path.join(WEB_DIR, "js")
    found = set()
    for root, _, files in os.walk(js_dir):
        for name in files:
            if name.endswith(".js"):
                found.add(os.path.relpath(os.path.join(root, name), js_dir).replace(os.sep, "/"))
    missing = [name for name in JS_ORDER if name not in found]
    unlisted = sorted(found - set(JS_ORDER))
    if missing or unlisted:
        raise ValueError("JS_ORDER out of sync with web/js: missing=%s unlisted=%s" % (missing, unlisted))
    body = "\n\n".join(_read(os.path.join(js_dir, name)) for name in JS_ORDER)
    return JS_HEADER + "\n(function () {\n'use strict';\n\n" + body + "\n\n})();\n"


def _bundle_css():
    css_dir = os.path.join(WEB_DIR, "css")
    names = sorted(n for n in os.listdir(css_dir) if n.endswith(".css"))
    return CSS_HEADER + "\n" + "\n\n".join(_read(os.path.join(css_dir, n)) for n in names) + "\n"


def content_type(path):
    ext = os.path.splitext(path)[1]
    return CONTENT_TYPES.get(ext) or mimetypes.guess_type(path)[0] or "application/octet-stream"


def build_assets():
    """Return [(url_path, content_type, gzipped_bytes)], sorted by path."""
    files = {
        "app.js": _bundle_js().encode("utf-8"),
        "style.css": _bundle_css().encode("utf-8"),
    }
    for name in os.listdir(WEB_DIR):
        path = os.path.join(WEB_DIR, name)
        if os.path.isfile(path):
            with open(path, "rb") as f:
                files[name] = f.read()

    assets = []
    for name, data in sorted(files.items()):
        # mtime=0 keeps the output, and therefore the firmware, reproducible.
        packed = gzip.compress(data, compresslevel=9, mtime=0)
        assets.append(("/" + name, content_type(name), packed))
    return assets


if __name__ == "__main__":
    total = 0
    for path, ctype, data in build_assets():
        total += len(data)
        print("%-16s %-26s %6d bytes" % (path, ctype, len(data)))
    print("total %d bytes gzipped" % total)
