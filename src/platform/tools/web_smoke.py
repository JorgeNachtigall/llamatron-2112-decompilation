#!/usr/bin/env python3
"""Web smoke test (PLAN.md §5): serve a built page on 127.0.0.1, load it in local headless
Chrome, and check the hash line the app prints into the page.

    web_smoke.py --dir build-web/src/platform --page llamatron_title.html \
                 --query frames=30 --expect "title 320x400 hash=c7ab74c4" [--expect ...]

Exit 0 = pass, 1 = fail, 77 = skipped (no Chrome found; ctest SKIP_RETURN_CODE).
Only local resources are used: no network access beyond the loopback server.
"""
import argparse
import functools
import html
import http.server
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading

SKIP = 77
CHROME_CANDIDATES = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "google-chrome", "google-chrome-stable", "chromium", "chromium-browser",
]


def find_chrome():
    env = os.environ.get("LLAMATRON_CHROME")
    for c in ([env] if env else []) + CHROME_CANDIDATES:
        path = c if os.path.isabs(c) else shutil.which(c)
        if path and os.access(path, os.X_OK):
            return path
    return None


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--page", required=True)
    ap.add_argument("--query", default="")
    ap.add_argument("--expect", action="append", default=[])
    ap.add_argument("--budget-ms", type=int, default=10000)
    args = ap.parse_args()

    chrome = find_chrome()
    if not chrome:
        print("web_smoke: no Chrome/Chromium found, skipping")
        return SKIP
    if not os.path.exists(os.path.join(args.dir, args.page)):
        print(f"web_smoke: {args.page} not built in {args.dir}")
        return 1

    handler = functools.partial(QuietHandler, directory=args.dir)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/{args.page}"
    if args.query:
        url += "?" + args.query

    with tempfile.TemporaryDirectory() as profile:
        cmd = [chrome, "--headless=new", "--no-first-run", "--no-default-browser-check",
               f"--user-data-dir={profile}", "--window-size=1000,900",
               f"--virtual-time-budget={args.budget_ms}", "--dump-dom", url]
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=120).stdout
        finally:
            server.shutdown()

    # Our shell (src/platform/web/shell.html) appends stdout to <pre id="output">.
    m = re.search(r'<pre[^>]*id="output"[^>]*>(.*?)</pre>', out, re.S)
    text = html.unescape(m.group(1)) if m else ""
    if not m:
        print("web_smoke: page has no <pre id=\"output\"> (built without our shell?)")
    print(text.strip())
    missing = [e for e in args.expect if e not in text]
    if missing:
        print("web_smoke: missing expected output: " + "; ".join(missing))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
