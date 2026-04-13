#!/usr/bin/env python3
import argparse
import http.client
import mimetypes
import socket
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse


class SmartHomeWebServer(ThreadingHTTPServer):
    daemon_threads = True
    block_on_close = False


class Handler(SimpleHTTPRequestHandler):
    api_host = "127.0.0.1"
    api_port = 8090
    static_dir = Path(".")

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(self.static_dir), **kwargs)

    def do_OPTIONS(self):
        if self.path.startswith("/api/") or self.path == "/health":
            self.send_response(HTTPStatus.NO_CONTENT)
            self._write_proxy_headers(content_type=None, content_length=None)
            self.end_headers()
            return
        super().do_OPTIONS()

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path.startswith("/api/") or parsed.path == "/health":
            self._proxy_request()
            return
        self._serve_spa()

    def do_POST(self):
        if self.path.startswith("/api/"):
            self._proxy_request()
            return
        self.send_error(HTTPStatus.NOT_FOUND, "Not found")

    def _serve_spa(self):
        requested = self.translate_path(self.path)
        if self.path in {"/", ""}:
            self.path = "/index.html"
            return super().do_GET()

        if Path(requested).exists() and Path(requested).is_file():
            return super().do_GET()

        self.path = "/index.html"
        return super().do_GET()

    def _proxy_request(self):
        body = b""
        if self.command in {"POST", "PUT", "PATCH"}:
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length) if length else b""

        headers = {
            "Accept": self.headers.get("Accept", "*/*"),
            "Content-Type": self.headers.get("Content-Type", "application/json"),
        }
        if self.headers.get("Last-Event-ID"):
            headers["Last-Event-ID"] = self.headers["Last-Event-ID"]

        conn = http.client.HTTPConnection(self.api_host, self.api_port, timeout=15)
        try:
            conn.request(self.command, self.path, body=body, headers=headers)
            response = conn.getresponse()
            is_sse = response.getheader("Content-Type", "").startswith("text/event-stream")
            if is_sse:
                self.send_response(response.status)
                self._write_proxy_headers(
                    content_type=response.getheader("Content-Type", "text/event-stream"),
                    content_length=None,
                )
                self.send_header("Connection", "keep-alive")
                self.end_headers()
                self.wfile.flush()
                while True:
                    chunk = response.read(1024)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    self.wfile.flush()
                return

            payload = response.read()
            self.send_response(response.status)
            self._write_proxy_headers(
                content_type=response.getheader("Content-Type", "application/json; charset=utf-8"),
                content_length=len(payload),
            )
            self.end_headers()
            if payload:
                self.wfile.write(payload)
        except (OSError, TimeoutError, socket.timeout) as exc:
            payload = f'{{"error":"{str(exc)}"}}'.encode("utf-8")
            self.send_response(HTTPStatus.BAD_GATEWAY)
            self._write_proxy_headers(
                content_type="application/json; charset=utf-8",
                content_length=len(payload),
            )
            self.end_headers()
            self.wfile.write(payload)
        finally:
            conn.close()

    def _write_proxy_headers(self, content_type, content_length):
        if content_type is not None:
            self.send_header("Content-Type", content_type)
        if content_length is not None:
            self.send_header("Content-Length", str(content_length))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Last-Event-ID")
        self.send_header("Access-Control-Allow-Private-Network", "true")

    def end_headers(self):
        if not self.path.startswith("/api/") and self.path != "/health":
            self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def guess_type(self, path):
        return mimetypes.guess_type(path)[0] or "application/octet-stream"


def parse_args():
    parser = argparse.ArgumentParser(description="Smart Home web entrypoint")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--api-port", type=int, default=8090)
    parser.add_argument("--dist-dir", default="/home/trungcoolboy/smart-home/frontend/dist")
    return parser.parse_args()


def main():
    args = parse_args()
    Handler.api_port = args.api_port
    Handler.static_dir = Path(args.dist_dir)
    server = SmartHomeWebServer((args.host, args.port), Handler)
    print(f"smart home web listening on http://{args.host}:{args.port} proxying api to {args.api_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
