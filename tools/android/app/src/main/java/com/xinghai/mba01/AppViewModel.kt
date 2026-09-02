package com.xinghai.mba01

import android.app.Application
import android.os.Handler
import android.os.Looper
import androidx.lifecycle.AndroidViewModel
import com.xinghai.mba01.ble.BleClient
import com.xinghai.mba01.ble.ScanDev
import com.xinghai.mba01.proto.Cli
import com.xinghai.mba01.radio.GnssSnap
import com.xinghai.mba01.radio.RadioParser
import com.xinghai.mba01.radio.RdssSnap
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

data class LogLine(val ts: String, val tag: String, val text: String, val kind: String)

data class IoPin(
    val name: String,
    val dir: String,
    val on: Boolean = false,
    val ready: Int = 0,
)

data class UiState(
    val scanning: Boolean = false,
    val devices: List<ScanDev> = emptyList(),
    val connectedName: String = "",
    val ready: Boolean = false,
    val status: String = "未连接",
    val autoPoll: Boolean = false,
    val hideNmea: Boolean = true,
    val ptMute: Boolean = true,
    val cdcHeld: String = "held=- mute=-",
    val logs: List<LogLine> = emptyList(),
    val mode: String = "-",
    val pt: String = "-",
    val bat: String = "-",
    val rtc: String = "-",
    val pm: String = "-",
    val recvId: String = "13500001",
    val deviceId: String = "1325000001",
    val paEnable: Boolean = false,
    val offsetMv: String = "100",
    val hwVer: String = "",
    val swVer: String = "-",
    val upgradeUnix: String = "-",
    val bdCard: String = "-",
    val firstFix: String = "-",
    val rtcUnix: String = "",
    val raw: String = "{\"id\":1,\"cmd\":\"ping\"}",
    val gnssSum: String = "可见 -  使用 -  质量 -",
    val rdssSum: String = "PWI时间 -  波束 -  有效 -",
    val gnss: GnssSnap = GnssSnap(),
    val rdss: RdssSnap = RdssSnap(),
    val ioOut: List<IoPin> = IoDefs.outPins,
    val ioIn: List<IoPin> = IoDefs.inPins,
)

object IoDefs {
    val activeLow = setOf("LED1", "LED2", "LED3")
    val outPins = listOf(
        "EN_5V", "EN_PLNA_POW", "EN_LNA_GNSS", "EN_PGNSS",
        "EN_LNA_RDSS", "EN_PRDSS", "EN_BLE", "LED1", "LED2", "LED3",
    ).map { IoPin(it, "out") }
    val inPins = listOf("USB_IN", "SOS_KEY", "FALL_KEY").map { IoPin(it, "in") }

    fun logicalToHw(pin: String, on: Boolean): Int =
        if (pin in activeLow) if (on) 0 else 1 else if (on) 1 else 0

    fun hwToLogical(pin: String, hw: Int): Boolean =
        if (pin in activeLow) hw == 0 else hw != 0
}

private data class Pending(
    val cmd: String,
    val deadline: Long,
    val quiet: Boolean,
    val fields: Map<String, Any?>,
)

class AppViewModel(app: Application) : AndroidViewModel(app) {
    private val _ui = MutableStateFlow(UiState())
    val ui: StateFlow<UiState> = _ui
    private val radio = RadioParser()
    private val pending = LinkedHashMap<Int, Pending>()
    private var nextId = 1
    private var selectedName = ""
    private val main = Handler(Looper.getMainLooper())
    private val ble = BleClient(
        context = app,
        onScan = { list -> _ui.update { it.copy(devices = list) } },
        onLine = { onLine(it) },
        onStatus = { msg -> _ui.update { it.copy(status = msg) } },
        onReady = { ready ->
            _ui.update { it.copy(ready = ready, connectedName = if (ready) selectedName else "") }
            if (ready) {
                log("系统", "GATT 就绪，握手", "sys")
                send("ping")
                send("log.cdc", quiet = true)
                send("cfg.get", quiet = true)
            } else {
                _ui.update { it.copy(autoPoll = false) }
            }
        },
    )

    private val expireTick = object : Runnable {
        override fun run() {
            expire()
            main.postDelayed(this, 200)
        }
    }
    private val pollTick = object : Runnable {
        override fun run() {
            if (_ui.value.autoPoll && _ui.value.ready && pending.isEmpty()) {
                send("mode.get", quiet = true)
                send("test.adc", quiet = true)
                send("test.rtc", quiet = true)
                send("test.pm", quiet = true)
            }
            main.postDelayed(this, 2000)
        }
    }

    init {
        main.post(expireTick)
        main.post(pollTick)
    }

    override fun onCleared() {
        main.removeCallbacks(expireTick)
        main.removeCallbacks(pollTick)
        ble.stopScan()
        ble.disconnect()
        super.onCleared()
    }

    fun startScan() {
        _ui.update { it.copy(scanning = true) }
        ble.startScan()
    }

    fun stopScan() {
        ble.stopScan()
        _ui.update { it.copy(scanning = false) }
    }

    fun connect(dev: ScanDev) {
        selectedName = dev.name
        ble.connect(dev.device)
        _ui.update { it.copy(scanning = false, status = "连接 ${dev.name}") }
    }

    fun disconnect() {
        ble.disconnect()
    }

    fun setAutoPoll(on: Boolean) {
        if (on && !_ui.value.ready) {
            log("系统", "请先连接设备", "sys")
            return
        }
        _ui.update { it.copy(autoPoll = on) }
    }

    fun setHideNmea(on: Boolean) {
        _ui.update { it.copy(hideNmea = on) }
    }

    fun setPtMute(on: Boolean) {
        _ui.update { it.copy(ptMute = on) }
        if (_ui.value.ready) send("log.cdc", fields = mapOf("passthru_mute" to if (on) 1 else 0))
    }

    fun setField(update: UiState.() -> UiState) {
        _ui.update { it.update() }
    }

    fun send(
        cmd: String,
        fields: Map<String, Any?> = emptyMap(),
        quiet: Boolean = false,
    ) {
        if (!_ui.value.ready) {
            if (!quiet) log("系统", "未连接", "sys")
            return
        }
        val id = nextId++
        if (nextId > 1_000_000) nextId = 1
        val line = Cli.buildCmd(cmd, id, fields)
        try {
            ble.writeLine(line)
        } catch (e: Exception) {
            log("系统", e.message ?: "发送失败", "sys")
            return
        }
        synchronized(pending) {
            pending[id] = Pending(
                cmd = cmd,
                deadline = System.currentTimeMillis() + Cli.timeoutMs(cmd),
                quiet = quiet,
                fields = fields,
            )
        }
        if (!quiet) log("TX", line, "tx")
    }

    fun sendRaw() {
        val line = _ui.value.raw.trim().lineSequence().firstOrNull()?.trim().orEmpty()
        if (line.isEmpty()) return
        if (!_ui.value.ready) {
            log("系统", "未连接", "sys")
            return
        }
        try {
            ble.writeLine(line)
            log("TX", line, "tx")
        } catch (e: Exception) {
            log("系统", e.message ?: "发送失败", "sys")
        }
    }

    fun fillRtcNow() {
        _ui.update { it.copy(rtcUnix = (System.currentTimeMillis() / 1000).toString()) }
    }

    fun rtcWrite() {
        val u = _ui.value.rtcUnix.trim().toLongOrNull()
        if (u == null) {
            log("系统", "unix 无效", "sys")
            return
        }
        send("test.rtc", fields = mapOf("unix" to u))
    }

    fun cfgSet() {
        val rid = _ui.value.recvId.trim().toLongOrNull()
        val did = _ui.value.deviceId.trim().toLongOrNull()
        if (rid == null || did == null) {
            log("系统", "recv_id / device_id 须为整数", "sys")
            return
        }
        val offset = _ui.value.offsetMv.trim().toIntOrNull() ?: 100
        val fields = mutableMapOf<String, Any?>(
            "recv_id" to rid,
            "device_id" to did,
            "pa_enable" to if (_ui.value.paEnable) 1 else 0,
            "charge_offset_mv" to offset.coerceIn(0, 500),
        )
        val hw = _ui.value.hwVer.trim()
        if (hw.isNotEmpty()) fields["hw_ver"] = hw
        send("cfg.set", fields)
    }

    fun ioToggle(pin: String, on: Boolean) {
        if (!_ui.value.ready) {
            log("系统", "未连接", "sys")
            patchIo(pin) { it.copy(on = false) }
            return
        }
        patchIo(pin) { it.copy(on = on) }
        send("io.set", fields = mapOf("pin" to pin, "val" to IoDefs.logicalToHw(pin, on)))
    }

    fun ioReadAll() {
        (_ui.value.ioOut + _ui.value.ioIn).forEach { send("io.get", fields = mapOf("pin" to it.name), quiet = true) }
    }

    fun clearLog() {
        _ui.update { it.copy(logs = emptyList()) }
    }

    private fun onLine(line: String) {
        val s = line.trim()
        if (s.startsWith("$")) {
            if (radio.feed(s)) {
                val g = radio.gnss
                val d = radio.rdss
                _ui.update {
                    it.copy(
                        gnss = g.copy(sats = g.sats.toList()),
                        rdss = d.copy(beams = d.beams.toList()),
                        gnssSum = "可见 ${g.inView}  使用 ${g.inUse ?: "-"}  质量 ${g.quality ?: "-"}",
                        rdssSum = "PWI时间 ${d.pwiTime?.toInt() ?: "-"}  波束 ${d.beamN}  ${if (d.good) "通过" else "未过"}",
                    )
                }
            }
            if (_ui.value.hideNmea) return
        }
        val rsp = Cli.tryParseRsp(line)
        if (rsp == null) {
            log("LOG", line, "log")
            return
        }
        log("RSP", rsp.toString(), "rsp")
        val rid = rsp.optInt("id", -1)
        val meta = synchronized(pending) { pending.remove(rid) }
        applyRsp(meta?.cmd ?: rsp.optString("cmd"), rsp, meta?.fields ?: emptyMap())
    }

    private fun expire() {
        val now = System.currentTimeMillis()
        val dead = synchronized(pending) {
            val ids = pending.filter { now > it.value.deadline }.keys.toList()
            ids.associateWith { pending.remove(it) }
        }
        dead.forEach { (id, meta) ->
            if (meta != null && !meta.quiet) {
                log("系统", "超时 id=$id cmd=${meta.cmd}", "sys")
            }
        }
    }

    private fun applyRsp(cmd: String, rsp: JSONObject, fields: Map<String, Any?>) {
        val ok = rsp.optInt("ok", 0) != 0
        when {
            cmd == "mode.get" && ok -> _ui.update {
                it.copy(
                    mode = "${rsp.optString("name", "?")} (${rsp.opt("state")})",
                    pt = rsp.opt("pt").toString(),
                )
            }
            cmd == "test.adc" && ok -> _ui.update {
                it.copy(
                    bat = "${rsp.opt("pct")}%  ${rsp.opt("mv")} mV  lookup=${rsp.opt("lookup_mv")}  " +
                        "chg=${rsp.opt("charge")}  off=${rsp.opt("offset_mv")}  " +
                        "${rsp.optString("level_name", rsp.opt("level").toString())}",
                )
            }
            cmd == "test.rtc" && ok -> {
                val unix = rsp.optLong("unix", 0)
                _ui.update {
                    it.copy(rtc = "unix=$unix synced=${rsp.opt("synced")}  (${fmtUnix(unix)})")
                }
            }
            cmd == "test.pm" && ok -> _ui.update { it.copy(pm = rsp.opt("lock_count").toString()) }
            cmd == "cfg.get" && ok -> applyCfg(rsp)
            cmd == "cfg.set" && ok -> applyCfg(rsp)
            cmd == "log.cdc" && ok -> {
                val mute = rsp.optInt("passthru_mute", 1)
                val held = rsp.optInt("held", 0)
                _ui.update {
                    it.copy(ptMute = mute != 0, cdcHeld = "held=$held  mute=$mute")
                }
            }
            cmd == "stream.set" && ok -> {
                radio.reset()
                _ui.update {
                    it.copy(gnss = GnssSnap(), rdss = RdssSnap(), gnssSum = "可见 -  使用 -  质量 -", rdssSum = "PWI时间 -  波束 -  有效 -")
                }
                send("log.cdc", quiet = true)
            }
            cmd == "io.get" -> {
                val pin = rsp.optString("pin").ifEmpty { fields["pin"]?.toString().orEmpty() }
                if (ok && pin.isNotEmpty() && rsp.has("val")) {
                    val hw = rsp.optInt("val")
                    patchIo(pin) { it.copy(on = IoDefs.hwToLogical(pin, hw)) }
                }
            }
            cmd == "io.set" -> {
                val pin = rsp.optString("pin").ifEmpty { fields["pin"]?.toString().orEmpty() }
                if (ok && pin.isNotEmpty() && rsp.has("val")) {
                    patchIo(pin) { it.copy(on = IoDefs.hwToLogical(pin, rsp.optInt("val"))) }
                } else if (!ok && pin.isNotEmpty()) {
                    val want = fields["val"] as? Int
                    if (want != null) patchIo(pin) { it.copy(on = !IoDefs.hwToLogical(pin, want)) }
                }
            }
            cmd == "io.list" && ok -> {
                val ios = rsp.optJSONArray("ios") ?: return
                for (i in 0 until ios.length()) {
                    val item = ios.optJSONObject(i) ?: continue
                    val name = item.optString("name")
                    val ready = item.optInt("ready", 0)
                    patchIo(name) { it.copy(ready = ready) }
                }
            }
        }
    }

    private fun applyCfg(rsp: JSONObject) {
        _ui.update {
            it.copy(
                recvId = if (rsp.has("recv_id")) rsp.opt("recv_id").toString() else it.recvId,
                deviceId = if (rsp.has("device_id")) rsp.opt("device_id").toString() else it.deviceId,
                paEnable = if (rsp.has("pa_enable")) rsp.optInt("pa_enable") != 0 else it.paEnable,
                offsetMv = if (rsp.has("charge_offset_mv")) rsp.opt("charge_offset_mv").toString() else it.offsetMv,
                hwVer = if (rsp.has("hw_ver")) rsp.optString("hw_ver") else it.hwVer,
                swVer = if (rsp.has("sw_ver")) rsp.optString("sw_ver") else it.swVer,
                upgradeUnix = if (rsp.has("upgrade_unix")) fmtUnix(rsp.optLong("upgrade_unix")) else it.upgradeUnix,
                bdCard = if (rsp.has("bd_card")) rsp.opt("bd_card").toString() else it.bdCard,
                firstFix = if (rsp.has("first_fix_unix")) fmtUnix(rsp.optLong("first_fix_unix")) else it.firstFix,
            )
        }
    }

    private fun patchIo(pin: String, fn: (IoPin) -> IoPin) {
        _ui.update { st ->
            st.copy(
                ioOut = st.ioOut.map { if (it.name == pin) fn(it) else it },
                ioIn = st.ioIn.map { if (it.name == pin) fn(it) else it },
            )
        }
    }

    private fun log(tag: String, text: String, kind: String) {
        val ts = SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault()).format(Date())
        _ui.update {
            val next = it.logs + LogLine(ts, tag, text, kind)
            it.copy(logs = if (next.size > 400) next.takeLast(400) else next)
        }
    }

    private fun fmtUnix(u: Long): String {
        if (u <= 0L) return "0"
        return try {
            val ts = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date(u * 1000))
            "$u ($ts)"
        } catch (_: Exception) {
            u.toString()
        }
    }
}
