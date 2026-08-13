"""主窗口：连接 / 日志 / 监视 / 配置 / 自检 / 透传 / IO。"""

from __future__ import annotations

import json
import time
from datetime import datetime
from pathlib import Path
from typing import Any

from PySide6.QtCore import QObject, Qt, QTimer, Signal, Slot
from PySide6.QtGui import QFont, QTextCursor
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSplitter,
    QTabWidget,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from . import APP_TITLE, __version__
from .protocol import CmdIdGen, build_cmd, default_timeout_ms, try_parse_rsp
from .serial_link import SerialLink, list_com_ports, port_device

# name, dir: out=可勾选控制, in=只读显示
IO_PIN_DEFS: list[tuple[str, str]] = [
    ("EN_5V", "out"),
    ("EN_PLNA_POW", "out"),
    ("EN_LNA_GNSS", "out"),
    ("EN_PGNSS", "out"),
    ("EN_LNA_RDSS", "out"),
    ("EN_PRDSS", "out"),
    ("EN_BLE", "out"),
    ("LED1", "out"),
    ("LED2", "out"),
    ("LED3", "out"),
    ("USB_IN", "in"),
    ("SOS_KEY", "in"),
    ("FALL_KEY", "in"),
]

# LED 低电平点亮：勾选=点亮 → 下发 GPIO=0
IO_ACTIVE_LOW_PINS = frozenset({"LED1", "LED2", "LED3"})


def io_logical_to_hw(pin: str, logical_on: bool) -> int:
    """勾选态 → io.set 的 val（GPIO 电平）。"""
    if pin in IO_ACTIVE_LOW_PINS:
        return 0 if logical_on else 1
    return 1 if logical_on else 0


def io_hw_to_logical(pin: str, hw_val: int | bool) -> bool:
    """io.get/set 的 val（GPIO）→ 勾选态（亮/有效）。"""
    v = int(hw_val)
    if pin in IO_ACTIVE_LOW_PINS:
        return v == 0
    return v != 0


class Bridge(QObject):
    """把串口线程回调切回 Qt 主线程。"""

    line = Signal(str)
    status = Signal(str)


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle(APP_TITLE)
        self.resize(1100, 720)

        self._ids = CmdIdGen()
        self._bridge = Bridge()
        self._bridge.line.connect(self._on_line)
        self._bridge.status.connect(self._on_status)
        self._link = SerialLink(
            on_line=lambda s: self._bridge.line.emit(s),
            on_status=lambda s: self._bridge.status.emit(s),
        )
        self._pending: dict[int, dict[str, Any]] = {}
        self._log_path: Path | None = None
        self._io_checks: dict[str, QCheckBox] = {}
        self._io_dirs: dict[str, str] = {}
        self._io_ready: dict[str, int] = {}

        self._build_ui()
        self._apply_style()
        self._refresh_ports()

        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(2000)
        self._poll_timer.timeout.connect(self._auto_poll)
        self._expire_timer = QTimer(self)
        self._expire_timer.setInterval(200)
        self._expire_timer.timeout.connect(self._expire_pending)
        self._expire_timer.start()

    # ------------------------------------------------------------------ UI
    def _build_ui(self) -> None:
        root = QWidget()
        self.setCentralWidget(root)
        outer = QVBoxLayout(root)
        outer.setContentsMargins(10, 10, 10, 10)
        outer.setSpacing(8)

        outer.addLayout(self._build_conn_bar())

        split = QSplitter()
        split.setOrientation(Qt.Orientation.Vertical)
        outer.addWidget(split, 1)

        self.tabs = QTabWidget()
        self.tabs.addTab(self._tab_monitor(), "监视")
        self.tabs.addTab(self._tab_cfg(), "配置")
        self.tabs.addTab(self._tab_test(), "自检")
        self.tabs.addTab(self._tab_stream(), "透传")
        self.tabs.addTab(self._tab_io(), "IO")
        self.tabs.addTab(self._tab_raw(), "原始命令")
        split.addWidget(self.tabs)

        log_box = QGroupBox("设备日志 / 应答")
        log_l = QVBoxLayout(log_box)
        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setFont(QFont("Consolas", 10))
        log_l.addWidget(self.txt_log)
        btn_row = QHBoxLayout()
        self.chk_autoscroll = QCheckBox("自动滚底")
        self.chk_autoscroll.setChecked(True)
        btn_clear = QPushButton("清空")
        btn_clear.clicked.connect(self.txt_log.clear)
        btn_save = QPushButton("保存日志…")
        btn_save.clicked.connect(self._save_log)
        btn_row.addWidget(self.chk_autoscroll)
        btn_row.addStretch(1)
        btn_row.addWidget(btn_clear)
        btn_row.addWidget(btn_save)
        log_l.addLayout(btn_row)
        split.addWidget(log_box)
        split.setSizes([380, 280])

        self.statusBar().showMessage(f"V{__version__} · 未连接")

    def _build_conn_bar(self) -> QHBoxLayout:
        row = QHBoxLayout()
        row.addWidget(QLabel("端口"))
        self.cmb_port = QComboBox()
        self.cmb_port.setMinimumWidth(260)
        row.addWidget(self.cmb_port)
        btn_ref = QPushButton("刷新")
        btn_ref.clicked.connect(self._refresh_ports)
        row.addWidget(btn_ref)
        row.addWidget(QLabel("波特率"))
        self.cmb_baud = QComboBox()
        self.cmb_baud.addItems(["115200", "921600", "9600"])
        row.addWidget(self.cmb_baud)
        self.btn_conn = QPushButton("连接")
        self.btn_conn.clicked.connect(self._toggle_conn)
        row.addWidget(self.btn_conn)
        self.btn_ping = QPushButton("Ping")
        self.btn_ping.clicked.connect(lambda: self._send("ping"))
        row.addWidget(self.btn_ping)
        self.chk_poll = QCheckBox("自动监视 2s")
        self.chk_poll.toggled.connect(self._on_poll_toggled)
        row.addWidget(self.chk_poll)
        row.addStretch(1)
        return row

    def _tab_monitor(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        form = QFormLayout()
        self.lbl_mode = QLabel("-")
        self.lbl_pt = QLabel("-")
        self.lbl_bat = QLabel("-")
        self.lbl_rtc = QLabel("-")
        self.lbl_pm = QLabel("-")
        form.addRow("MODE", self.lbl_mode)
        form.addRow("透传 flags", self.lbl_pt)
        form.addRow("电量", self.lbl_bat)
        form.addRow("RTC", self.lbl_rtc)
        form.addRow("PM lock", self.lbl_pm)
        lay.addLayout(form)
        row = QHBoxLayout()
        for text, cmd in (
            ("刷新 MODE", "mode.get"),
            ("刷新 ADC", "test.adc"),
            ("刷新 RTC", "test.rtc"),
            ("刷新 PM", "test.pm"),
        ):
            b = QPushButton(text)
            b.clicked.connect(lambda _=False, c=cmd: self._send(c))
            row.addWidget(b)
        row.addStretch(1)
        lay.addLayout(row)
        tip = QLabel("连接后勾选「自动监视」将周期拉取 mode/adc/rtc/pm。")
        tip.setWordWrap(True)
        lay.addWidget(tip)
        lay.addStretch(1)
        return w

    def _tab_cfg(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        form = QFormLayout()
        self.ed_recv = QLineEdit("13500001")
        self.ed_dev = QLineEdit("1325000001")
        self.chk_pa = QCheckBox("有效波束后开 PA")
        form.addRow("recv_id", self.ed_recv)
        form.addRow("device_id", self.ed_dev)
        form.addRow("pa_enable", self.chk_pa)
        lay.addLayout(form)
        row = QHBoxLayout()
        b_get = QPushButton("读取 cfg.get")
        b_get.clicked.connect(lambda: self._send("cfg.get"))
        b_set = QPushButton("写入 cfg.set")
        b_set.clicked.connect(self._cfg_set)
        row.addWidget(b_get)
        row.addWidget(b_set)
        row.addStretch(1)
        lay.addLayout(row)
        lay.addStretch(1)
        return w

    def _tab_test(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        grid = QGridLayout()
        tests = [
            ("test.list", {}),
            ("test.key", {}),
            ("test.adc", {}),
            ("test.led mode=2", {"mode": 2}),
            ("test.led pct=50", {"pct": 50}),
            ("test.gnss.fix", {}),
            ("test.rdss.card", {}),
            ("test.rdss.send", {}),
            ("test.session.once", {}),
            ("test.rtc", {}),
            ("test.pm", {}),
            ("test.pm hold 3s", {"hold_ms": 3000}),
        ]
        for i, (label, fields) in enumerate(tests):
            cmd = label.split()[0]
            b = QPushButton(label)
            b.clicked.connect(lambda _=False, c=cmd, f=fields: self._send(c, **f))
            grid.addWidget(b, i // 3, i % 3)
        lay.addLayout(grid)

        rtc_row = QHBoxLayout()
        self.ed_unix = QLineEdit()
        self.ed_unix.setPlaceholderText("unix 秒，如 1735689600")
        b_rtc = QPushButton("test.rtc 写入")
        b_rtc.clicked.connect(self._rtc_set)
        b_now = QPushButton("填入本机时间")
        b_now.clicked.connect(lambda: self.ed_unix.setText(str(int(time.time()))))
        rtc_row.addWidget(QLabel("RTC set"))
        rtc_row.addWidget(self.ed_unix, 1)
        rtc_row.addWidget(b_now)
        rtc_row.addWidget(b_rtc)
        lay.addLayout(rtc_row)

        warn = QLabel(
            "注意：gnss/rdss/session 自检会阻塞板端 CLI 线程，等待期间请勿连发命令。"
        )
        warn.setWordWrap(True)
        lay.addWidget(warn)
        lay.addStretch(1)
        return w

    def _tab_stream(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        row = QHBoxLayout()
        b_list = QPushButton("stream.list")
        b_list.clicked.connect(lambda: self._send("stream.list"))
        row.addWidget(b_list)
        row.addStretch(1)
        lay.addLayout(row)
        for name in ("gnss", "rdss", "cli"):
            g = QGroupBox(name)
            r = QHBoxLayout(g)
            b_get = QPushButton("get")
            b_get.clicked.connect(lambda _=False, n=name: self._send("stream.get", name=n))
            b_on = QPushButton("enable=1")
            b_on.clicked.connect(
                lambda _=False, n=name: self._send("stream.set", name=n, enable=1)
            )
            b_off = QPushButton("enable=0")
            b_off.clicked.connect(
                lambda _=False, n=name: self._send("stream.set", name=n, enable=0)
            )
            r.addWidget(b_get)
            r.addWidget(b_on)
            r.addWidget(b_off)
            r.addStretch(1)
            lay.addWidget(g)
        tip = QLabel(
            "透传数据本身不走 JSON；此处只开关 stream 通道。"
            "真正模块透传依赖固件 MODE_PASSTHRU / sink。"
        )
        tip.setWordWrap(True)
        lay.addWidget(tip)
        lay.addStretch(1)
        return w

    def _tab_io(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        row = QHBoxLayout()
        b_list = QPushButton("刷新列表")
        b_list.clicked.connect(lambda: self._send("io.list"))
        b_read = QPushButton("读取全部电平")
        b_read.clicked.connect(self._io_read_all)
        row.addWidget(b_list)
        row.addWidget(b_read)
        row.addStretch(1)
        lay.addLayout(row)

        g_out = QGroupBox("输出（勾选=有效/点亮，立即下发 io.set）")
        grid_out = QGridLayout(g_out)
        g_in = QGroupBox("输入（只读，点「读取全部电平」刷新）")
        grid_in = QGridLayout(g_in)
        oi = ii = 0
        for name, direction in IO_PIN_DEFS:
            label = f"{name} (低亮)" if name in IO_ACTIVE_LOW_PINS else name
            chk = QCheckBox(label)
            chk.setTristate(False)
            self._io_checks[name] = chk
            self._io_dirs[name] = direction
            self._io_ready[name] = 0
            if direction == "out":
                if name in IO_ACTIVE_LOW_PINS:
                    chk.setToolTip("低电平点亮：勾选=亮(GPIO0)，取消=灭(GPIO1)")
                else:
                    chk.setToolTip("勾选=GPIO1，取消=GPIO0")
                chk.toggled.connect(
                    lambda checked, n=name: self._io_toggled(n, checked)
                )
                grid_out.addWidget(chk, oi // 3, oi % 3)
                oi += 1
            else:
                chk.setEnabled(False)
                chk.setToolTip("输入脚，仅显示")
                grid_in.addWidget(chk, ii // 3, ii % 3)
                ii += 1
        lay.addWidget(g_out)
        lay.addWidget(g_in)
        tip = QLabel(
            "勾选=有效。LED1/2/3 为低电平点亮（勾选下发 val=0）。"
            "固件骨架阶段多数 ready=0，失败会自动回退勾选。"
        )
        tip.setWordWrap(True)
        lay.addWidget(tip)
        lay.addStretch(1)
        return w

    def _tab_raw(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        self.ed_raw = QTextEdit()
        self.ed_raw.setPlaceholderText('{"id":1,"cmd":"ping"}')
        self.ed_raw.setMaximumHeight(120)
        self.ed_raw.setFont(QFont("Consolas", 10))
        lay.addWidget(self.ed_raw)
        row = QHBoxLayout()
        b_send = QPushButton("发送一行")
        b_send.clicked.connect(self._send_raw)
        row.addWidget(b_send)
        row.addStretch(1)
        lay.addLayout(row)
        lay.addStretch(1)
        return w

    def _apply_style(self) -> None:
        self.setStyleSheet(
            """
            QMainWindow, QWidget { background: #f2f4f6; color: #1a2332; }
            QGroupBox {
                font-weight: 600;
                border: 1px solid #c5ccd6;
                border-radius: 4px;
                margin-top: 10px;
                padding-top: 8px;
            }
            QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
            QPushButton {
                background: #2c4a6e;
                color: #fff;
                border: none;
                border-radius: 3px;
                padding: 6px 12px;
                min-height: 22px;
            }
            QPushButton:hover { background: #3a5f8a; }
            QPushButton:pressed { background: #1f3550; }
            QPushButton:disabled { background: #9aa7b5; }
            QComboBox, QLineEdit, QSpinBox {
                background: #fff;
                border: 1px solid #b8c0cc;
                border-radius: 3px;
                padding: 4px 6px;
                min-height: 22px;
            }
            QTextEdit {
                background: #1e2430;
                color: #d7dde8;
                border: 1px solid #3a4454;
                border-radius: 3px;
            }
            QTabWidget::pane { border: 1px solid #c5ccd6; background: #fff; }
            QTabBar::tab {
                background: #e4e8ee;
                padding: 7px 14px;
                margin-right: 2px;
                border-top-left-radius: 3px;
                border-top-right-radius: 3px;
            }
            QTabBar::tab:selected { background: #fff; font-weight: 600; }
            QStatusBar { background: #e4e8ee; }
            """
        )

    # -------------------------------------------------------------- actions
    def _refresh_ports(self) -> None:
        cur = self.cmb_port.currentText()
        self.cmb_port.clear()
        ports = list_com_ports()
        self.cmb_port.addItems(ports)
        if cur:
            i = self.cmb_port.findText(cur)
            if i >= 0:
                self.cmb_port.setCurrentIndex(i)

    def _toggle_conn(self) -> None:
        if self._link.is_open:
            self._link.close()
            self.btn_conn.setText("连接")
            self.chk_poll.setChecked(False)
            self.statusBar().showMessage(f"V{__version__} · 已断开")
            self._append_log("系统", "已断开", kind="sys")
            return
        label = self.cmb_port.currentText()
        port = port_device(label)
        if not port:
            QMessageBox.warning(self, APP_TITLE, "请选择串口")
            return
        try:
            baud = int(self.cmb_baud.currentText())
            self._link.open(port, baud)
        except Exception as exc:
            QMessageBox.critical(self, APP_TITLE, f"打开失败：{exc}")
            return
        self.btn_conn.setText("断开")
        self._append_log("系统", f"打开 {port}", kind="sys")
        self._send("ping")

    def _on_poll_toggled(self, on: bool) -> None:
        if on:
            if not self._link.is_open:
                self.chk_poll.blockSignals(True)
                self.chk_poll.setChecked(False)
                self.chk_poll.blockSignals(False)
                QMessageBox.information(self, APP_TITLE, "请先连接串口")
                return
            self._poll_timer.start()
            self._auto_poll()
        else:
            self._poll_timer.stop()

    def _auto_poll(self) -> None:
        if not self._link.is_open:
            return
        # 避开长阻塞自检占用时的连发：有 pending 则跳过一轮
        if self._pending:
            return
        for cmd in ("mode.get", "test.adc", "test.rtc", "test.pm"):
            self._send(cmd, quiet=True)

    def _cfg_set(self) -> None:
        try:
            rid = int(self.ed_recv.text().strip())
            did = int(self.ed_dev.text().strip())
        except ValueError:
            QMessageBox.warning(self, APP_TITLE, "recv_id / device_id 须为整数")
            return
        self._send(
            "cfg.set",
            recv_id=rid,
            device_id=did,
            pa_enable=1 if self.chk_pa.isChecked() else 0,
        )

    def _rtc_set(self) -> None:
        try:
            u = int(self.ed_unix.text().strip())
        except ValueError:
            QMessageBox.warning(self, APP_TITLE, "unix 无效")
            return
        self._send("test.rtc", unix=u)

    def _send_raw(self) -> None:
        text = self.ed_raw.toPlainText().strip().splitlines()
        if not text:
            return
        line = text[0].strip()
        if not self._link.is_open:
            QMessageBox.warning(self, APP_TITLE, "未连接")
            return
        try:
            self._link.write_line(line)
            self._append_log("TX", line, kind="tx")
        except Exception as exc:
            QMessageBox.critical(self, APP_TITLE, str(exc))

    def _io_toggled(self, pin: str, checked: bool) -> None:
        if not self._link.is_open:
            chk = self._io_checks.get(pin)
            if chk is not None:
                chk.blockSignals(True)
                chk.setChecked(False)
                chk.blockSignals(False)
            QMessageBox.warning(self, APP_TITLE, "未连接")
            return
        self._send("io.set", pin=pin, val=io_logical_to_hw(pin, checked))

    def _io_read_all(self) -> None:
        if not self._link.is_open:
            QMessageBox.warning(self, APP_TITLE, "未连接")
            return
        for name in self._io_checks:
            self._send("io.get", quiet=True, pin=name)

    def _io_set_check(self, pin: str, on: bool) -> None:
        chk = self._io_checks.get(pin)
        if chk is None:
            return
        chk.blockSignals(True)
        chk.setChecked(bool(on))
        chk.blockSignals(False)

    def _send(self, cmd: str, quiet: bool = False, **fields: Any) -> None:
        if not self._link.is_open:
            if not quiet:
                QMessageBox.warning(self, APP_TITLE, "未连接")
            return
        cmd_id = self._ids.next()
        line = build_cmd(cmd, cmd_id, **fields)
        try:
            self._link.write_line(line)
        except Exception as exc:
            if not quiet:
                QMessageBox.critical(self, APP_TITLE, str(exc))
            return
        self._pending[cmd_id] = {
            "cmd": cmd,
            "deadline": time.monotonic() + default_timeout_ms(cmd) / 1000.0,
            "quiet": quiet,
            "fields": fields,
        }
        if not quiet:
            self._append_log("TX", line, kind="tx")

    def _save_log(self) -> None:
        from PySide6.QtWidgets import QFileDialog

        path, _ = QFileDialog.getSaveFileName(
            self,
            "保存日志",
            f"shy_host_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log",
            "Log (*.log);;All (*.*)",
        )
        if not path:
            return
        Path(path).write_text(self.txt_log.toPlainText(), encoding="utf-8")
        self.statusBar().showMessage(f"已保存 {path}", 4000)

    # -------------------------------------------------------------- rx
    @Slot(str)
    def _on_status(self, msg: str) -> None:
        self.statusBar().showMessage(msg)

    @Slot(str)
    def _on_line(self, line: str) -> None:
        rsp = try_parse_rsp(line)
        if rsp is None:
            self._append_log("LOG", line, kind="log")
            return
        self._append_log("RSP", json.dumps(rsp, ensure_ascii=False), kind="rsp")
        rid = rsp.get("id")
        if isinstance(rid, int) and rid in self._pending:
            meta = self._pending.pop(rid)
            self._apply_rsp(meta.get("cmd", ""), rsp, meta.get("fields") or {})

    def _expire_pending(self) -> None:
        now = time.monotonic()
        dead = [i for i, m in self._pending.items() if now > m["deadline"]]
        for i in dead:
            meta = self._pending.pop(i)
            if not meta.get("quiet"):
                self._append_log("系统", f"超时 id={i} cmd={meta.get('cmd')}", kind="sys")

    def _apply_rsp(
        self, cmd: str, rsp: dict[str, Any], fields: dict[str, Any] | None = None
    ) -> None:
        ok = bool(rsp.get("ok"))
        fields = fields or {}
        if cmd == "mode.get" and ok:
            name = rsp.get("name", "?")
            st = rsp.get("state", "?")
            pt = rsp.get("pt", 0)
            self.lbl_mode.setText(f"{name} ({st})")
            self.lbl_pt.setText(str(pt))
        elif cmd == "test.adc" and ok:
            self.lbl_bat.setText(
                f"{rsp.get('pct', '?')}%  {rsp.get('mv', '?')} mV  "
                f"{rsp.get('level_name', rsp.get('level', '?'))}  "
                f"VDDA={rsp.get('vdda', '?')} mV"
            )
        elif cmd == "test.rtc" and ok:
            unix = rsp.get("unix", 0)
            synced = rsp.get("synced", 0)
            try:
                ts = datetime.fromtimestamp(int(unix)).strftime("%Y-%m-%d %H:%M:%S")
            except Exception:
                ts = "-"
            self.lbl_rtc.setText(f"unix={unix} synced={synced}  ({ts})")
        elif cmd == "test.pm" and ok:
            self.lbl_pm.setText(str(rsp.get("lock_count", "?")))
        elif cmd == "cfg.get" and ok:
            if "recv_id" in rsp:
                self.ed_recv.setText(str(rsp["recv_id"]))
            if "device_id" in rsp:
                self.ed_dev.setText(str(rsp["device_id"]))
            if "pa_enable" in rsp:
                self.chk_pa.setChecked(bool(rsp["pa_enable"]))
        elif cmd == "cfg.set" and ok:
            if "recv_id" in rsp:
                self.ed_recv.setText(str(rsp["recv_id"]))
            if "device_id" in rsp:
                self.ed_dev.setText(str(rsp["device_id"]))
            if "pa_enable" in rsp:
                self.chk_pa.setChecked(bool(rsp["pa_enable"]))
        elif cmd == "io.get":
            pin = str(rsp.get("pin") or fields.get("pin") or "")
            if ok and pin and "val" in rsp:
                hw = int(rsp.get("val") or 0)
                self._io_set_check(pin, io_hw_to_logical(pin, hw))
                chk = self._io_checks.get(pin)
                if chk is not None:
                    lit = "亮" if io_hw_to_logical(pin, hw) else "灭"
                    chk.setToolTip(f"{pin} GPIO={hw} ({lit})")
        elif cmd == "io.set":
            pin = str(rsp.get("pin") or fields.get("pin") or "")
            if ok and pin and "val" in rsp:
                self._io_set_check(pin, io_hw_to_logical(pin, int(rsp.get("val") or 0)))
            elif (not ok) and pin:
                # 失败回退：恢复为本次下发前的相反逻辑态
                want = fields.get("val")
                if want is not None:
                    self._io_set_check(pin, not io_hw_to_logical(pin, int(want)))
        elif cmd == "io.list" and ok:
            ios = rsp.get("ios")
            if isinstance(ios, list):
                for item in ios:
                    if not isinstance(item, dict):
                        continue
                    name = str(item.get("name", ""))
                    if name not in self._io_checks:
                        continue
                    ready = int(item.get("ready", 0) or 0)
                    self._io_ready[name] = ready
                    chk = self._io_checks[name]
                    tip = f"{name} dir={item.get('dir')} ready={ready}"
                    if name in IO_ACTIVE_LOW_PINS:
                        tip += " active-low"
                    chk.setToolTip(tip)
                    # 未就绪仍可勾选尝试；标题后加标记
                    base = name
                    if name in IO_ACTIVE_LOW_PINS:
                        base = f"{name} (低亮)"
                    if not ready:
                        base = f"{base} *"
                    chk.setText(base)

    def _append_log(self, tag: str, text: str, kind: str = "log") -> None:
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        colors = {
            "log": "#a8b4c4",
            "rsp": "#7dcea0",
            "tx": "#5dade2",
            "sys": "#f5b041",
        }
        color = colors.get(kind, "#d7dde8")
        safe = (
            text.replace("&", "&amp;")
            .replace("<", "&lt;")
            .replace(">", "&gt;")
        )
        html = (
            f'<span style="color:#6c7a89">[{ts}]</span> '
            f'<span style="color:{color}">[{tag}] {safe}</span>'
        )
        self.txt_log.append(html)
        if self.chk_autoscroll.isChecked():
            self.txt_log.moveCursor(QTextCursor.MoveOperation.End)

    def closeEvent(self, event) -> None:  # noqa: N802
        self._poll_timer.stop()
        self._expire_timer.stop()
        self._link.close()
        super().closeEvent(event)
