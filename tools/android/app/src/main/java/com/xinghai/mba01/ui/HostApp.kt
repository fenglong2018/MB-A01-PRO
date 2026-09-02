package com.xinghai.mba01.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.ScrollableTabRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.xinghai.mba01.AppViewModel
import com.xinghai.mba01.IoDefs
import com.xinghai.mba01.LogLine
import com.xinghai.mba01.UiState
import com.xinghai.mba01.radio.GnssSat
import com.xinghai.mba01.radio.RdssBeam

private val Navy = Color(0xFF2C4A6E)
private val PageBg = Color(0xFFF2F4F6)
private val TxBlue = Color(0xFF1565C0)
private val RspGreen = Color(0xFF2E7D32)
private val SysOrange = Color(0xFFEF6C00)
private val LogGray = Color(0xFF607D8B)

private val tabs = listOf("监视", "配置", "自检", "射频", "IO", "原始")

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HostApp(vm: AppViewModel, onScanClick: () -> Unit) {
    val ui by vm.ui.collectAsStateWithLifecycle()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    MaterialTheme {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = { Text("shySOFT Host V0.2") },
                    colors = TopAppBarDefaults.topAppBarColors(
                        containerColor = Navy,
                        titleContentColor = Color.White,
                    ),
                )
            },
        ) { pad ->
            Column(
                Modifier
                    .fillMaxSize()
                    .padding(pad)
                    .background(PageBg),
            ) {
                ConnBar(ui, vm, onScanClick)
                ScrollableTabRow(
                    selectedTabIndex = tab,
                    containerColor = Color.White,
                    edgePadding = 8.dp,
                ) {
                    tabs.forEachIndexed { i, title ->
                        Tab(
                            selected = tab == i,
                            onClick = { tab = i },
                            text = { Text(title) },
                        )
                    }
                }
                Column(
                    Modifier
                        .weight(1f)
                        .verticalScroll(rememberScrollState())
                        .padding(10.dp),
                ) {
                    when (tab) {
                        0 -> MonitorTab(ui, vm)
                        1 -> CfgTab(ui, vm)
                        2 -> TestTab(ui, vm)
                        3 -> RadioTab(ui, vm)
                        4 -> IoTab(ui, vm)
                        else -> RawTab(ui, vm)
                    }
                }
                LogBox(ui, vm)
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ConnBar(ui: UiState, vm: AppViewModel, onScanClick: () -> Unit) {
    Column(Modifier.padding(10.dp, 8.dp)) {
        Text(ui.status, fontSize = 13.sp, color = Navy, fontWeight = FontWeight.Medium)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Button(
                onClick = {
                    if (ui.scanning) vm.stopScan() else onScanClick()
                },
                colors = ButtonDefaults.buttonColors(containerColor = Navy),
            ) { Text(if (ui.scanning) "停扫" else "扫描") }
            Spacer(Modifier.width(8.dp))
            if (ui.ready) {
                OutlinedButton(onClick = vm::disconnect) { Text("断开") }
                Spacer(Modifier.width(8.dp))
                Text(ui.connectedName, fontWeight = FontWeight.SemiBold)
            }
            Spacer(Modifier.weight(1f))
            Button(
                onClick = { vm.send("ping") },
                enabled = ui.ready,
                colors = ButtonDefaults.buttonColors(containerColor = Navy),
            ) { Text("Ping") }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Checkbox(ui.autoPoll, onCheckedChange = vm::setAutoPoll, enabled = ui.ready)
            Text("自动监视 2s", fontSize = 13.sp)
        }
        if (ui.devices.isNotEmpty() && !ui.ready) {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                ui.devices.forEach { d ->
                    FilterChip(
                        selected = false,
                        onClick = { vm.connect(d) },
                        label = { Text("${d.name}  ${d.rssi}dBm") },
                    )
                }
            }
        }
    }
}

@Composable
private fun MonitorTab(ui: UiState, vm: AppViewModel) {
    Kv("MODE", ui.mode)
    Kv("透传 flags", ui.pt)
    Kv("电量", ui.bat)
    Kv("RTC", ui.rtc)
    Kv("PM lock", ui.pm)
    Spacer(Modifier.height(8.dp))
    FlowBtns(
        listOf(
            "刷新 MODE" to { vm.send("mode.get") },
            "刷新 ADC" to { vm.send("test.adc") },
            "刷新 RTC" to { vm.send("test.rtc") },
            "刷新 PM" to { vm.send("test.pm") },
        ),
    )
    Tip("连接后勾选「自动监视」将周期拉取 mode/adc/rtc/pm。插 USB 才能搜到 MBA01-。")
}

@Composable
private fun CfgTab(ui: UiState, vm: AppViewModel) {
    Edit("recv_id", ui.recvId) { vm.setField { copy(recvId = it) } }
    Edit("device_id", ui.deviceId) { vm.setField { copy(deviceId = it) } }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Checkbox(ui.paEnable, { vm.setField { copy(paEnable = it) } })
        Text("有效波束后开 PA")
    }
    Edit("charge_offset_mv", ui.offsetMv) { vm.setField { copy(offsetMv = it) } }
    Edit("hw_ver", ui.hwVer) { vm.setField { copy(hwVer = it) } }
    Kv("sw_ver", ui.swVer)
    Kv("upgrade_unix", ui.upgradeUnix)
    Kv("bd_card", ui.bdCard)
    Kv("first_fix_unix", ui.firstFix)
    FlowBtns(
        listOf(
            "读取 cfg.get" to { vm.send("cfg.get") },
            "写入 cfg.set" to vm::cfgSet,
        ),
    )
}

@Composable
private fun TestTab(ui: UiState, vm: AppViewModel) {
    val tests = listOf(
        "test.list" to emptyMap<String, Any?>(),
        "test.key" to emptyMap(),
        "test.adc" to emptyMap(),
        "test.led mode=2" to mapOf("mode" to 2),
        "test.led pct=50" to mapOf("pct" to 50),
        "test.gnss.fix" to emptyMap(),
        "test.rdss.card" to emptyMap(),
        "test.rdss.send" to emptyMap(),
        "test.session.once" to emptyMap(),
        "test.rtc" to emptyMap(),
        "test.pm" to emptyMap(),
        "test.pm hold 3s" to mapOf("hold_ms" to 3000),
    )
    FlowBtns(tests.map { (label, fields) ->
        label to { vm.send(label.split(" ")[0], fields) }
    })
    Spacer(Modifier.height(8.dp))
    Row(verticalAlignment = Alignment.CenterVertically) {
        OutlinedTextField(
            ui.rtcUnix,
            { vm.setField { copy(rtcUnix = it) } },
            label = { Text("unix 秒") },
            modifier = Modifier.weight(1f),
            singleLine = true,
        )
        Spacer(Modifier.width(8.dp))
        OutlinedButton(onClick = vm::fillRtcNow) { Text("本机时间") }
        Spacer(Modifier.width(8.dp))
        Button(
            onClick = vm::rtcWrite,
            colors = ButtonDefaults.buttonColors(containerColor = Navy),
        ) { Text("写入") }
    }
    Tip("gnss/rdss/session 自检会阻塞板端 CLI，等待期间勿连发。")
}

@Composable
private fun RadioTab(ui: UiState, vm: AppViewModel) {
    FlowBtns(
        listOf(
            "GNSS 开" to { vm.send("stream.set", mapOf("name" to "gnss", "enable" to 1)) },
            "GNSS 关" to { vm.send("stream.set", mapOf("name" to "gnss", "enable" to 0)) },
            "RDSS 开" to { vm.send("stream.set", mapOf("name" to "rdss", "enable" to 1)) },
            "RDSS 关" to { vm.send("stream.set", mapOf("name" to "rdss", "enable" to 0)) },
        ),
    )
    Row(verticalAlignment = Alignment.CenterVertically) {
        Checkbox(ui.ptMute, vm::setPtMute)
        Text("静音 ulog", fontSize = 13.sp)
        Checkbox(ui.hideNmea, vm::setHideNmea)
        Text("日志不刷 NMEA", fontSize = 13.sp)
    }
    Text(ui.cdcHeld, fontSize = 12.sp, color = LogGray)
    Text("GNSS  ${ui.gnssSum}", fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 8.dp))
    SatTable(ui.gnss.sats)
    Text("RDSS  ${ui.rdssSum}", fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 8.dp))
    BeamTable(ui.rdss.beams)
    Tip("须先开对应透传。一次只开一个通道。关透传发 JSON enable:0。")
}

@Composable
private fun IoTab(ui: UiState, vm: AppViewModel) {
    FlowBtns(
        listOf(
            "刷新列表" to { vm.send("io.list") },
            "读取全部电平" to vm::ioReadAll,
        ),
    )
    Text("输出（勾选=有效/点亮）", fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 8.dp))
    ui.ioOut.forEach { pin ->
        val label = buildString {
            append(pin.name)
            if (pin.name in IoDefs.activeLow) append(" (低亮)")
            if (pin.ready == 0) append(" *")
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Checkbox(pin.on, { vm.ioToggle(pin.name, it) }, enabled = ui.ready)
            Text(label, fontSize = 14.sp)
        }
    }
    Text("输入（只读）", fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 8.dp))
    ui.ioIn.forEach { pin ->
        Row(verticalAlignment = Alignment.CenterVertically) {
            Checkbox(pin.on, {}, enabled = false)
            Text(pin.name, fontSize = 14.sp)
        }
    }
    Tip("LED1/2/3 低电平点亮。板端 io.* 未接 GPIO 会 not_ready。")
}

@Composable
private fun RawTab(ui: UiState, vm: AppViewModel) {
    OutlinedTextField(
        ui.raw,
        { vm.setField { copy(raw = it) } },
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = 100.dp),
        label = { Text("一行 JSON") },
    )
    Spacer(Modifier.height(8.dp))
    Button(
        onClick = vm::sendRaw,
        colors = ButtonDefaults.buttonColors(containerColor = Navy),
    ) { Text("发送一行") }
}

@Composable
private fun LogBox(ui: UiState, vm: AppViewModel) {
    val listState = rememberLazyListState()
    LaunchedEffect(ui.logs.size) {
        if (ui.logs.isNotEmpty()) listState.animateScrollToItem(ui.logs.lastIndex)
    }
    Surface(shadowElevation = 4.dp, color = Color(0xFF1A2332)) {
        Column(Modifier.height(200.dp).fillMaxWidth()) {
            Row(
                Modifier.padding(8.dp, 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("日志 / 应答", color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.weight(1f))
                OutlinedButton(onClick = vm::clearLog) { Text("清空", color = Color.White) }
            }
            LazyColumn(state = listState, modifier = Modifier.padding(8.dp, 0.dp, 8.dp, 8.dp)) {
                items(ui.logs) { line -> LogRow(line) }
            }
        }
    }
}

@Composable
private fun LogRow(line: LogLine) {
    val c = when (line.kind) {
        "tx" -> TxBlue
        "rsp" -> RspGreen
        "sys" -> SysOrange
        else -> LogGray
    }
    Text(
        "[${line.ts}] [${line.tag}] ${line.text}",
        color = c,
        fontSize = 11.sp,
        fontFamily = FontFamily.Monospace,
        modifier = Modifier.padding(bottom = 2.dp),
    )
}

@Composable
private fun Kv(k: String, v: String) {
    Text("$k  $v", fontSize = 14.sp, modifier = Modifier.padding(vertical = 2.dp))
}

@Composable
private fun Edit(label: String, value: String, on: (String) -> Unit) {
    OutlinedTextField(
        value,
        on,
        label = { Text(label) },
        modifier = Modifier
            .fillMaxWidth()
            .padding(bottom = 6.dp),
        singleLine = true,
    )
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun FlowBtns(items: List<Pair<String, () -> Unit>>) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        modifier = Modifier.padding(vertical = 6.dp),
    ) {
        items.forEach { (title, act) ->
            Button(
                onClick = act,
                colors = ButtonDefaults.buttonColors(containerColor = Navy),
            ) { Text(title, fontSize = 13.sp) }
        }
    }
}

@Composable
private fun Tip(s: String) {
    Text(s, fontSize = 12.sp, color = LogGray, modifier = Modifier.padding(top = 8.dp))
}

@Composable
private fun SatTable(sats: List<GnssSat>) {
    Row(Modifier.horizontalScroll(rememberScrollState())) {
        Column {
            HeadRow(listOf("系统", "PRN", "仰角", "方位", "SNR"))
            sats.take(24).forEach { s ->
                val bg = when {
                    s.snr == null -> Color.Transparent
                    s.snr >= 35 -> Color(0xFFD5F5E3)
                    s.snr >= 20 -> Color(0xFFFDEBD0)
                    else -> Color(0xFFFADBD8)
                }
                Row(Modifier.background(bg).padding(vertical = 2.dp)) {
                    Cell(s.sys)
                    Cell(s.prn.toString())
                    Cell(s.elv?.toString() ?: "")
                    Cell(s.az?.toString() ?: "")
                    Cell(s.snr?.toString() ?: "")
                }
            }
        }
    }
}

@Composable
private fun BeamTable(beams: List<RdssBeam>) {
    Column {
        HeadRow(listOf("波束", "S2C_d", "有效"))
        beams.forEach { b ->
            val ok = b.s2c != null && b.s2c > 40
            val bg = if (ok) Color(0xFFD5F5E3) else Color(0xFFFADBD8)
            Row(Modifier.background(bg).padding(vertical = 2.dp)) {
                Cell(b.bid.toString())
                Cell(b.s2c?.toString() ?: "")
                Cell(if (ok) "是" else "")
            }
        }
    }
}

@Composable
private fun HeadRow(cols: List<String>) {
    Row {
        cols.forEach { Text(it, fontWeight = FontWeight.Bold, modifier = Modifier.width(56.dp), fontSize = 12.sp) }
    }
}

@Composable
private fun Cell(s: String) {
    Text(s, modifier = Modifier.width(56.dp), fontSize = 12.sp)
}
