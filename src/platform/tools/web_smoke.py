#!/usr/bin/env python3
"""Web smoke test (PLAN.md §5): serve a built page on 127.0.0.1, load it in local headless
Chrome, and check the hash line the app prints into the page.

    web_smoke.py --dir build-web/src/platform --page llamatron_title.html \
                 --query frames=30 --expect "title 320x400 hash=c7ab74c4" [--expect ...]

With --report the page runs in real time instead of virtual time (needed when it waits on
IndexedDB, which virtual time does not wait for): the shell posts each output line to the
loopback server and the run ends as soon as every --expect line has arrived (or --timeout-s).

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
import time

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
    reports = None          # list of lines posted to /report (--report mode)

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if self.path == "/report" and self.reports is not None:
            self.reports.append(body.decode("utf-8", "replace"))
        self.send_response(204)
        self.end_headers()


def run_reported(cmd, reports, expect, timeout_s):
    """Run Chrome in real time until every expected line was reported; returns the text."""
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline and proc.poll() is None:
            text = "\n".join(reports)
            if all(e in text for e in expect):
                break
            time.sleep(0.1)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    return "\n".join(reports)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--page", required=True)
    ap.add_argument("--query", default="")
    ap.add_argument("--expect", action="append", default=[])
    ap.add_argument("--budget-ms", type=int, default=10000)
    ap.add_argument("--profile", help="persistent browser profile dir (shares IndexedDB between runs)")
    ap.add_argument("--port", type=int, default=0,
                    help="fixed server port (IndexedDB is per origin: runs sharing --profile need it)")
    ap.add_argument("--report", action="store_true", help="real time, output posted by the page")
    ap.add_argument("--timeout-s", type=float, default=30.0)
    ap.add_argument("--scale-factor", type=float, default=0,
                    help="device pixel ratio to emulate (high-DPI check); default: the browser's")
    args = ap.parse_args()

    chrome = find_chrome()
    if not chrome:
        print("web_smoke: no Chrome/Chromium found, skipping")
        return SKIP
    if not os.path.exists(os.path.join(args.dir, args.page)):
        print(f"web_smoke: {args.page} not built in {args.dir}")
        return 1

    reports = []
    handler = functools.partial(QuietHandler, directory=args.dir)
    handler.func.reports = reports if args.report else None
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    query = "&".join(q for q in (args.query, "report" if args.report else "") if q)
    url = f"http://127.0.0.1:{server.server_address[1]}/{args.page}"
    if query:
        url += "?" + query

    with tempfile.TemporaryDirectory() as temp_profile:
        profile = args.profile or temp_profile
        cmd = [chrome, "--headless=new", "--no-first-run", "--no-default-browser-check",
               f"--user-data-dir={profile}", "--window-size=1000,900"]
        if args.scale_factor:
            cmd.append(f"--force-device-scale-factor={args.scale_factor:g}")
        try:
            if args.report:
                text = run_reported(cmd + [url], reports, args.expect, args.timeout_s)
            else:
                cmd += [f"--virtual-time-budget={args.budget_ms}", "--dump-dom", url]
                out = subprocess.run(cmd, capture_output=True, text=True, timeout=120).stdout
                # Our shell (src/platform/web/shell.html) appends stdout to <pre id="output">.
                m = re.search(r'<pre[^>]*id="output"[^>]*>(.*?)</pre>', out, re.S)
                text = html.unescape(m.group(1)) if m else ""
                if not m:
                    print("web_smoke: page has no <pre id=\"output\"> (built without our shell?)")
        finally:
            server.shutdown()
    print(text.strip())
    missing = [e for e in args.expect if e not in text]
    if missing:
        print("web_smoke: missing expected output: " + "; ".join(missing))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
