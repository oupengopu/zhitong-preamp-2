from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib import error, parse, request
import json
import os
import re
import socket
import subprocess
import time


ROOT = Path(__file__).resolve().parent
PORT = int(os.environ.get("PREVIEW_PORT", "8084"))
TIMEOUT = float(os.environ.get("REAL_DEVICE_TIMEOUT", "0.9"))
ROTATE_STEP_GAP = float(os.environ.get("REAL_ROTATE_STEP_GAP", "0.04"))
MAX_ROTATE_STEPS = 24
HOST_CACHE_SECONDS = 30.0
REAL_DEVICE_MAC = os.environ.get("REAL_DEVICE_MAC", "14-c1-9f-d8-cb-8c").lower()
_host_cache = {}


def _find_host_by_mac(mac):
    if not mac:
        return None
    normalized = mac.lower().replace(":", "-")
    try:
        output = subprocess.check_output(
            ["arp", "-a"],
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=1.0,
            encoding="utf-8",
            errors="ignore",
        )
    except Exception:
        return None

    for line in output.splitlines():
        if normalized not in line.lower().replace(":", "-"):
            continue
        match = re.search(r"\b(?:\d{1,3}\.){3}\d{1,3}\b", line)
        if match:
            return match.group(0)
    return None


def _resolve_host(host):
    if not host:
        return host
    cached = _host_cache.get(host)
    now = time.monotonic()
    if cached and now - cached[1] < HOST_CACHE_SECONDS:
        return cached[0]
    try:
        resolved = socket.gethostbyname(host)
    except OSError:
        if host.endswith(".local"):
            resolved = _find_host_by_mac(REAL_DEVICE_MAC)
            if resolved:
                _host_cache[host] = (resolved, now)
                return resolved
        return host
    _host_cache[host] = (resolved, now)
    return resolved


def _post_target(target):
    req = request.Request(target, method="POST")
    with request.urlopen(req, timeout=TIMEOUT) as resp:
        return resp.status


class PreviewHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def _json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        parsed = parse.urlsplit(self.path)
        if not parsed.path.startswith("/real/"):
            return super().do_POST()

        query = parse.parse_qs(parsed.query)
        ip = (query.get("ip") or [""])[0].strip()
        if not ip:
            self._json(400, {"ok": False, "error": "missing ip"})
            return

        host = _resolve_host(ip)
        if parsed.path == "/real/encoder/rotate":
            try:
                steps = int((query.get("steps") or ["0"])[0])
            except ValueError:
                steps = 0
            if steps == 0:
                self._json(200, {"ok": True, "sent": 0})
                return
            if steps > MAX_ROTATE_STEPS:
                steps = MAX_ROTATE_STEPS
            if steps < -MAX_ROTATE_STEPS:
                steps = -MAX_ROTATE_STEPS

            button = "Virtual%20Encoder%20CW" if steps > 0 else "Virtual%20Encoder%20CCW"
            count = abs(steps)
            sent = 0
            try:
                for i in range(count):
                    target = f"http://{host}/button/{button}/press"
                    _post_target(target)
                    sent += 1
                    if i + 1 < count:
                        time.sleep(ROTATE_STEP_GAP)
                self._json(200, {"ok": True, "sent": sent, "steps": steps, "host": host})
            except error.HTTPError as exc:
                self._json(502, {"ok": False, "sent": sent, "status": exc.code, "host": host})
            except Exception as exc:
                self._json(502, {"ok": False, "sent": sent, "error": str(exc), "host": host})
            return

        device_path = "/" + parsed.path[len("/real/") :]
        target = f"http://{host}{device_path}"
        try:
            status = _post_target(target)
            self._json(200, {"ok": True, "status": status, "target": target, "host": host})
        except error.HTTPError as exc:
            self._json(502, {"ok": False, "status": exc.code, "target": target})
        except Exception as exc:
            self._json(502, {"ok": False, "error": str(exc), "target": target})


if __name__ == "__main__":
    server = ThreadingHTTPServer(("127.0.0.1", PORT), PreviewHandler)
    print(f"Preview server running at http://localhost:{PORT}/")
    server.serve_forever()
