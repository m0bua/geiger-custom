#!/usr/bin/env python3
"""Serve the app on localhost and bridge its binary serial protocol to a COM port."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import socket
import struct
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - depends on the user's Python install
    raise SystemExit("Install pyserial first: python -m pip install pyserial") from exc


ROOT = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1]))
MAX_WS_MESSAGE = 1_048_576


def receive_exact(sock: socket.socket, size: int) -> bytes | None:
    output = bytearray()
    while len(output) < size:
        chunk = sock.recv(size - len(output))
        if not chunk:
            return None
        output.extend(chunk)
    return bytes(output)


class WebSocket:
    def __init__(self, sock: socket.socket):
        self.sock = sock
        # The ROM flasher makes many small write/ACK exchanges. Disable Nagle
        # so Windows does not hold each tiny localhost WebSocket frame for a
        # delayed TCP ACK interval before forwarding it to the browser.
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.send_lock = threading.Lock()

    def send_frame(self, opcode: int, payload: bytes) -> None:
        length = len(payload)
        if length < 126:
            header = bytes((0x80 | opcode, length))
        elif length <= 0xFFFF:
            header = bytes((0x80 | opcode, 126)) + struct.pack("!H", length)
        else:
            header = bytes((0x80 | opcode, 127)) + struct.pack("!Q", length)
        with self.send_lock:
            self.sock.sendall(header + payload)

    def send_json(self, value: dict) -> None:
        self.send_frame(1, json.dumps(value, separators=(",", ":")).encode("utf-8"))

    def send_binary(self, payload: bytes) -> None:
        if payload:
            self.send_frame(2, payload)

    def receive(self) -> tuple[int, bytes] | None:
        header = receive_exact(self.sock, 2)
        if header is None:
            return None
        first, second = header
        if first & 0x70:
            raise ValueError("WebSocket extensions are not supported")
        opcode = first & 0x0F
        length = second & 0x7F
        if length == 126:
            data = receive_exact(self.sock, 2)
            if data is None:
                return None
            length = struct.unpack("!H", data)[0]
        elif length == 127:
            data = receive_exact(self.sock, 8)
            if data is None:
                return None
            length = struct.unpack("!Q", data)[0]
        if length > MAX_WS_MESSAGE:
            raise ValueError("WebSocket message exceeds the size limit")
        mask = receive_exact(self.sock, 4) if second & 0x80 else None
        payload = receive_exact(self.sock, length)
        if payload is None:
            return None
        if mask:
            payload = bytes(byte ^ mask[index & 3] for index, byte in enumerate(payload))
        if opcode == 9:
            self.send_frame(10, payload)
            return self.receive()
        return opcode, payload


class BridgeSession:
    def __init__(self, websocket: WebSocket):
        self.websocket = websocket
        self.port: serial.Serial | None = None
        self.stop_reader = threading.Event()
        self.reader: threading.Thread | None = None
        self.disposed = False

    def send_error(self, request_id: int, error: Exception) -> None:
        self.websocket.send_json({"type": "error", "id": request_id,
                                  "message": str(error)[:500]})

    def open_port(self, request_id: int, data: dict) -> None:
        self.close_port()
        name = str(data.get("port", ""))
        available = {item.device for item in list_ports.comports()}
        if name not in available:
            raise ValueError("The selected COM port is not currently present.")
        parity = {"none": serial.PARITY_NONE, "even": serial.PARITY_EVEN,
                  "odd": serial.PARITY_ODD}.get(str(data.get("parity", "none")).lower())
        if parity is None:
            raise ValueError("Unsupported parity mode.")
        stop_bits = {1: serial.STOPBITS_ONE, 2: serial.STOPBITS_TWO}.get(
            int(data.get("stopBits", 1)))
        if stop_bits is None:
            raise ValueError("Unsupported stop-bit count.")
        port = serial.Serial(port=None, baudrate=int(data.get("baudRate", 115200)),
                              bytesize=int(data.get("dataBits", 8)), parity=parity,
                              stopbits=stop_bits, timeout=0.05, write_timeout=2)
        # Request inactive modem-control outputs as part of the initial DCB,
        # without adding explicit post-open DTR/RTS transitions.
        port.dtr = False
        port.rts = False
        port.port = name
        port.open()
        self.port = port
        self.stop_reader.clear()
        self.reader = threading.Thread(target=self.read_loop, daemon=True)
        self.reader.start()
        self.websocket.send_json({"type": "opened", "id": request_id, "port": name})

    def read_loop(self) -> None:
        while not self.stop_reader.is_set():
            port = self.port
            if port is None or not port.is_open:
                break
            try:
                # ROM bootloader replies are often just one ACK byte. Reading
                # a fixed 256-byte block makes pyserial wait for the 50 ms
                # timeout on every short reply, multiplying into minutes over
                # thousands of flash/read commands.
                chunk = port.read(max(1, port.in_waiting))
                if chunk:
                    self.websocket.send_binary(chunk)
            except Exception as error:
                if not self.stop_reader.is_set():
                    self.websocket.send_json({"type": "error", "id": 0,
                                              "message": f"COM read failed: {error}"[:500]})
                break

    def write_port(self, request_id: int, payload: bytes) -> None:
        if self.port is None or not self.port.is_open:
            raise RuntimeError("COM port is not open.")
        written = self.port.write(payload)
        if written != len(payload):
            raise IOError(f"Short COM write ({written}/{len(payload)} bytes).")
        self.websocket.send_json({"type": "writeAck", "id": request_id})

    def set_signals(self, request_id: int, data: dict) -> None:
        if self.port is None or not self.port.is_open:
            raise RuntimeError("COM port is not open.")
        if "dtr" in data:
            self.port.dtr = bool(data["dtr"])
        if "rts" in data:
            self.port.rts = bool(data["rts"])
        self.websocket.send_json({"type": "signalsSet", "id": request_id})

    def close_port(self) -> None:
        self.stop_reader.set()
        port, self.port = self.port, None
        if port is not None:
            try:
                port.close()
            finally:
                if self.reader and self.reader is not threading.current_thread():
                    self.reader.join(timeout=0.5)
        self.reader = None

    def run(self) -> None:
        try:
            while True:
                message = self.websocket.receive()
                if message is None:
                    break
                opcode, payload = message
                if opcode == 8:
                    break
                if opcode == 2:
                    if len(payload) < 4:
                        raise ValueError("Binary serial message is missing its write ID.")
                    request_id = struct.unpack("<I", payload[:4])[0]
                    try:
                        self.write_port(request_id, payload[4:])
                    except Exception as error:
                        self.send_error(request_id, error)
                    continue
                if opcode != 1:
                    continue
                request = json.loads(payload.decode("utf-8"))
                request_id = int(request.get("id", 0))
                try:
                    operation = request.get("op")
                    if operation == "open":
                        self.open_port(request_id, request)
                    elif operation == "close":
                        self.close_port()
                        self.websocket.send_json({"type": "closed", "id": request_id})
                    elif operation == "signals":
                        self.set_signals(request_id, request)
                    else:
                        raise ValueError("Unknown bridge operation.")
                except Exception as error:
                    self.send_error(request_id, error)
        except (ConnectionError, OSError, ValueError, json.JSONDecodeError):
            pass
        finally:
            self.close_port()
            try:
                self.websocket.send_frame(8, b"")
            except OSError:
                pass


class BridgeHandler(SimpleHTTPRequestHandler):
    server_version = "GeigerWindowsBridge/1"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def log_message(self, fmt: str, *args) -> None:
        print("[http] " + fmt % args)

    def do_GET(self) -> None:
        path = unquote(urlsplit(self.path).path)
        if path == "/":
            self.send_response(302)
            self.send_header("Location", "/app/index.html")
            self.end_headers()
            return
        if path == "/api/ports":
            devices = list(list_ports.comports())

            def port_priority(item):
                text = " ".join((item.description or "", item.hwid or "",
                                 item.manufacturer or "")).lower()
                if item.vid == 0x10C4 and item.pid == 0xEA60:
                    return 0
                if "cp210" in text or "silicon labs" in text:
                    return 1
                return 2

            devices.sort(key=lambda item: (port_priority(item), item.device))
            ports = [{"port": item.device, "description": item.description,
                      "vid": item.vid, "pid": item.pid, "hwid": item.hwid}
                     for item in devices]
            body = json.dumps(ports).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path == "/serial":
            self.handle_websocket()
            return
        if not (path.startswith("/app/") or path.startswith("/docs/renders/screens/") or
                path in {"/.pio/build/geiger2/firmware.bin", "/.pio/build/firmware.bin"}):
            self.send_error(404)
            return
        resolved = (ROOT / path.lstrip("/")).resolve()
        if ROOT not in resolved.parents or not resolved.is_file():
            self.send_error(404)
            return
        self.path = path
        super().do_GET()

    def handle_websocket(self) -> None:
        port = self.server.server_port
        expected_origins = {f"http://127.0.0.1:{port}", f"http://localhost:{port}"}
        if self.headers.get("Origin") not in expected_origins:
            self.send_error(403, "WebSocket origin is not the local app.")
            return
        key = self.headers.get("Sec-WebSocket-Key")
        if not key or self.headers.get("Upgrade", "").lower() != "websocket":
            self.send_error(400, "Expected a WebSocket upgrade.")
            return
        accept = base64.b64encode(hashlib.sha1(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()).decode("ascii")
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.wfile.flush()
        BridgeSession(WebSocket(self.connection)).run()


class LocalBridgeServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765, help="localhost HTTP/WebSocket port")
    args = parser.parse_args()
    server = LocalBridgeServer(("127.0.0.1", args.port), BridgeHandler)
    print(f"Windows serial bridge ready: http://127.0.0.1:{args.port}/app/index.html")
    print("The COM port opens only after you choose it in the app and press Connect.")
    print("Press Ctrl+C to stop the bridge.")
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        print("\nStopping bridge.")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
