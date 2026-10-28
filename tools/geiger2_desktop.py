#!/usr/bin/env python3
"""Single-window Windows app: shared web UI plus the local COM bridge."""

from __future__ import annotations

import ctypes
import threading
import traceback
import urllib.request

from geiger2_windows_bridge import BridgeHandler, LocalBridgeServer


def show_error(message: str) -> None:
    ctypes.windll.user32.MessageBoxW(None, message, "Dosimeter App", 0x10)


def main() -> int:
    try:
        import webview
    except Exception as error:
        show_error(
            "The WebView2 desktop component could not start. Install or repair "
            f"Microsoft Edge WebView2 Runtime, then retry.\n\n{error}"
        )
        return 1

    server = LocalBridgeServer(("127.0.0.1", 0), BridgeHandler)
    server_thread = threading.Thread(target=server.serve_forever,
                                     kwargs={"poll_interval": 0.25}, daemon=True)
    server_thread.start()
    address = f"http://127.0.0.1:{server.server_port}/app/index.html"
    try:
        webview.settings["ALLOW_DOWNLOADS"] = True
        with urllib.request.urlopen(address, timeout=5) as response:
            if response.status != 200:
                raise RuntimeError(f"UI returned HTTP {response.status}")
        webview.create_window(
            "Dosimeter App", address, width=1180, height=820,
            min_size=(720, 560), text_select=True,
        )
        webview.start(debug=False)
        return 0
    except Exception as error:
        traceback.print_exc()
        show_error(f"Could not start Dosimeter App.\n\n{error}")
        return 1
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    raise SystemExit(main())
