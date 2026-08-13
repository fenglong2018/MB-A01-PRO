"""设备 JSON CLI 编解码（一行一条）。"""

from __future__ import annotations

import json
import threading
from typing import Any


class CmdIdGen:
    def __init__(self, start: int = 1) -> None:
        self._n = start
        self._lock = threading.Lock()

    def next(self) -> int:
        with self._lock:
            n = self._n
            self._n += 1
            if self._n > 1_000_000:
                self._n = 1
            return n


def build_cmd(cmd: str, cmd_id: int, **fields: Any) -> str:
    """构造一行请求 JSON（不含换行）。"""
    obj: dict[str, Any] = {"id": int(cmd_id), "cmd": cmd}
    for k, v in fields.items():
        if v is None:
            continue
        obj[k] = v
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":"))


def try_parse_rsp(line: str) -> dict[str, Any] | None:
    """
    若为本机应答行则返回 dict，否则 None。
    判据：可解析 JSON 且 type==rsp（与固件约定一致）。
    """
    s = line.strip()
    if not s.startswith("{"):
        return None
    try:
        obj = json.loads(s)
    except json.JSONDecodeError:
        return None
    if isinstance(obj, dict) and obj.get("type") == "rsp":
        return obj
    return None


def default_timeout_ms(cmd: str) -> int:
    """按命令给等待超时（固件侧 GNSS/RDSS 会阻塞）。"""
    if cmd == "test.gnss.fix":
        return 45_000
    if cmd in ("test.rdss.send",):
        return 60_000
    if cmd in ("test.rdss.card",):
        return 15_000
    if cmd == "test.session.once":
        return 90_000
    return 5_000
