#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import tempfile
import urllib.parse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

APP_DIR = pathlib.Path(__file__).resolve().parent
MAX_SCL_BYTES = 16 * 1024 * 1024


class Handler(SimpleHTTPRequestHandler):
    server_version = "ARStackSmvGui/1"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(APP_DIR), **kwargs)

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        super().end_headers()

    def do_POST(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path != "/api/scl/inspect":
            self.send_error(404)
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_error(400, "Invalid Content-Length")
            return
        if length <= 0 or length > MAX_SCL_BYTES:
            self.send_error(413, "Engineering file size is invalid")
            return

        body = self.rfile.read(length)
        query = urllib.parse.parse_qs(parsed.query)
        counter = query.get("counterModulus", [None])[0]
        if counter is not None:
            try:
                value = int(counter, 10)
                if value <= 0 or value > 65535:
                    raise ValueError
            except ValueError:
                self._json(400, {"fatalError": "Counter modulus must be 1..65535."})
                return

        suffix = pathlib.Path(self.headers.get("X-File-Name", "engineering.scl")).suffix
        if not suffix or len(suffix) > 12:
            suffix = ".scl"

        temp_path = None
        try:
            with tempfile.NamedTemporaryFile(delete=False, suffix=suffix) as handle:
                handle.write(body)
                temp_path = pathlib.Path(handle.name)

            command = [str(self.server.profile_tool), str(temp_path)]
            if counter is not None:
                command += ["--counter-modulus", counter]
            completed = subprocess.run(
                command,
                capture_output=True,
                text=True,
                timeout=12,
                check=False,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            )
            payload_text = completed.stdout.strip()
            if not payload_text:
                raise RuntimeError(completed.stderr.strip() or "Profile compiler returned no output.")
            payload = json.loads(payload_text)
            status = 200 if completed.returncode == 0 else 422
            self._json(status, payload)
        except subprocess.TimeoutExpired:
            self._json(504, {"fatalError": "SCL profile compilation timed out."})
        except Exception as error:  # local control-plane boundary
            self._json(500, {"fatalError": str(error)})
        finally:
            if temp_path is not None:
                try:
                    temp_path.unlink(missing_ok=True)
                except OSError:
                    pass

    def _json(self, status: int, payload: dict) -> None:
        encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def log_message(self, fmt: str, *args) -> None:
        print(f"{self.address_string()} - {fmt % args}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile-tool", required=True)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()

    tool = pathlib.Path(args.profile_tool).resolve()
    if not tool.is_file():
        raise SystemExit(f"Profile compiler not found: {tool}")

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.profile_tool = tool
    print("ARStack61850 SMV Injector GUI")
    print(f"Local UI: http://127.0.0.1:{args.port}/")
    print(f"Smart SCL engine: {tool}")
    print("IMPORTANT: close idf.py monitor first so the GUI can own the serial port.")
    print("Press Ctrl+C to stop the local control-plane server.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
