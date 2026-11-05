#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Share one V4L2 camera with multiple local or Windows browsers."""

import argparse
import http.server
import subprocess
import threading


class Frames:
    def __init__(self) -> None:
        self.condition = threading.Condition()
        self.frame = b""
        self.sequence = 0

    def publish(self, frame: bytes) -> None:
        with self.condition:
            self.frame = frame
            self.sequence += 1
            self.condition.notify_all()

    def wait(self, sequence: int) -> tuple[int, bytes]:
        with self.condition:
            self.condition.wait_for(lambda: self.sequence != sequence)
            return self.sequence, self.frame


def capture(device: str, width: int, height: int, frames: Frames) -> None:
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error",
        "-f", "v4l2", "-input_format", "mjpeg",
        "-video_size", "1920x1080", "-framerate", "30",
        "-i", device, "-vf", f"scale={width}:{height}",
        "-q:v", "5", "-f", "image2pipe", "-vcodec", "mjpeg", "-",
    ]
    with subprocess.Popen(command, stdout=subprocess.PIPE) as process:
        data = bytearray()
        assert process.stdout is not None
        while chunk := process.stdout.read(65536):
            data.extend(chunk)
            while True:
                start = data.find(b"\xff\xd8")
                end = data.find(b"\xff\xd9", start + 2)
                if start < 0 or end < 0:
                    if start > 0:
                        del data[:start]
                    break
                frames.publish(bytes(data[start:end + 2]))
                del data[:end + 2]


def handler_for(frames: Frames):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path in ("/", "/index.html"):
                body = (b"<!doctype html><title>TaruGX camera</title>"
                        b"<style>body{margin:0;background:#111}img{width:100vw;"
                        b"height:100vh;object-fit:contain}</style>"
                        b"<img src='/camera.mjpg'>")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if self.path == "/snapshot.jpg":
                _, frame = frames.wait(0)
                self.send_response(200)
                self.send_header("Content-Type", "image/jpeg")
                self.send_header("Content-Length", str(len(frame)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(frame)
                return
            if self.path != "/camera.mjpg":
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type",
                             "multipart/x-mixed-replace; boundary=frame")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            sequence = 0
            try:
                while True:
                    sequence, frame = frames.wait(sequence)
                    self.wfile.write(
                        b"--frame\r\nContent-Type: image/jpeg\r\n" +
                        f"Content-Length: {len(frame)}\r\n\r\n".encode() +
                        frame + b"\r\n"
                    )
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, format: str, *args: object) -> None:
            pass

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default="/dev/video0")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    args = parser.parse_args()
    frames = Frames()
    threading.Thread(target=capture,
                     args=(args.device, args.width, args.height, frames),
                     daemon=True).start()
    server = http.server.ThreadingHTTPServer(
        (args.bind, args.port), handler_for(frames))
    print(f"TaruGX camera: http://localhost:{args.port}/", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
