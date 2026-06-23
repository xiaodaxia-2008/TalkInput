# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "rapidocr",
#     "onnxruntime",
# ]
# ///
"""RapidOCR local HTTP service."""

from __future__ import annotations

import argparse
import json
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from typing import Any

from rapidocr import RapidOCR


class RapidOcrHandler(BaseHTTPRequestHandler):
    engine: RapidOCR

    def do_GET(self) -> None:
        if self.path != "/health":
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        self.write_json({"ready": True})

    def do_POST(self) -> None:
        if self.path != "/ocr":
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        try:
            payload = self.read_json()
            image_path = Path(str(payload["path"]))
            if not image_path.is_file():
                self.send_error(HTTPStatus.BAD_REQUEST, "image path not found")
                return

            result = self.engine(str(image_path))
            text = "\n".join(str(item) for item in (result.txts if result else []))
            self.write_json({"text": text})
        except Exception as exc:
            self.send_error(HTTPStatus.INTERNAL_SERVER_ERROR, str(exc))

    def read_json(self) -> dict[str, Any]:
        content_length = int(self.headers.get("Content-Length", "0"))
        data = self.rfile.read(content_length)
        return json.loads(data.decode("utf-8"))

    def write_json(self, payload: dict[str, Any]) -> None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, format: str, *args: Any) -> None:
        return


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    args = parser.parse_args()

    RapidOcrHandler.engine = RapidOCR()
    server = HTTPServer((args.host, args.port), RapidOcrHandler)
    server.serve_forever()


if __name__ == "__main__":
    main()
