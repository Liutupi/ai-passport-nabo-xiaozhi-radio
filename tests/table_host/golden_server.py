"""Serve the browser prototype and save golden traces POSTed by it.

Usage: python3 tests/table_host/golden_server.py PORT
GET  serves ../nabo-table/prototype; POST /save/<name>.json writes tests/table_host/golden/<name>.json.
"""
import http.server
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROTO = HERE.parents[2] / "nabo-table" / "prototype"


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(PROTO), **kwargs)

    def do_POST(self):
        m = re.fullmatch(r"/save/([a-z0-9_]+\.json)", self.path)
        if not m:
            self.send_error(404)
            return
        data = self.rfile.read(int(self.headers["Content-Length"]))
        (HERE / "golden" / m.group(1)).write_bytes(data)
        self.send_response(204)
        self.end_headers()


if __name__ == "__main__":
    http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
