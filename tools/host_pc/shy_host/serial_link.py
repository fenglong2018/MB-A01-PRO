"""串口读写线程：行分隔，打开时拉高 DTR。"""

from __future__ import annotations

import threading
import time
from typing import Callable

import serial
from serial.tools import list_ports


def list_com_ports() -> list[str]:
    items = []
    for p in list_ports.comports():
        desc = p.description or ""
        label = f"{p.device}  {desc}".strip()
        items.append(label)
    return items


def port_device(label: str) -> str:
    """从 'COMx  desc' 取设备名。"""
    return (label or "").split()[0] if label else ""


class SerialLink:
    def __init__(
        self,
        on_line: Callable[[str], None],
        on_status: Callable[[str], None] | None = None,
    ) -> None:
        self._on_line = on_line
        self._on_status = on_status or (lambda _s: None)
        self._ser: serial.Serial | None = None
        self._rx_thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._tx_lock = threading.Lock()
        self._buf = bytearray()

    @property
    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    def open(self, port: str, baud: int = 115200) -> None:
        self.close()
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = baud
        ser.bytesize = serial.EIGHTBITS
        ser.parity = serial.PARITY_NONE
        ser.stopbits = serial.STOPBITS_ONE
        ser.timeout = 0.05
        ser.write_timeout = 2.0
        ser.dsrdtr = False
        ser.rtscts = False
        ser.open()
        # USB CDC：部分口需 DTR 才出数
        try:
            ser.dtr = True
            ser.rts = True
        except Exception:
            pass
        time.sleep(0.15)
        try:
            ser.reset_input_buffer()
        except Exception:
            pass
        self._ser = ser
        self._buf.clear()
        self._stop.clear()
        self._rx_thread = threading.Thread(target=self._rx_loop, name="serial-rx", daemon=True)
        self._rx_thread.start()
        self._on_status(f"已连接 {port} @ {baud} (DTR=1)")

    def close(self) -> None:
        self._stop.set()
        th = self._rx_thread
        if th and th.is_alive():
            th.join(timeout=1.0)
        self._rx_thread = None
        if self._ser is not None:
            try:
                self._ser.dtr = False
            except Exception:
                pass
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None
        self._buf.clear()

    def write_line(self, text: str) -> None:
        if not self.is_open or self._ser is None:
            raise RuntimeError("串口未打开")
        data = (text.rstrip("\r\n") + "\n").encode("utf-8", errors="replace")
        with self._tx_lock:
            self._ser.write(data)
            self._ser.flush()

    def _rx_loop(self) -> None:
        while not self._stop.is_set():
            ser = self._ser
            if ser is None or not ser.is_open:
                break
            try:
                chunk = ser.read(256)
            except Exception as exc:
                self._on_status(f"串口读错误: {exc}")
                break
            if not chunk:
                continue
            self._buf.extend(chunk)
            while True:
                nl = self._buf.find(b"\n")
                if nl < 0:
                    break
                raw = bytes(self._buf[:nl])
                del self._buf[: nl + 1]
                if raw.endswith(b"\r"):
                    raw = raw[:-1]
                try:
                    line = raw.decode("utf-8", errors="replace")
                except Exception:
                    line = repr(raw)
                try:
                    self._on_line(line)
                except Exception:
                    pass
