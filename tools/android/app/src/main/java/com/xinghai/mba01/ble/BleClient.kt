package com.xinghai.mba01.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import java.io.ByteArrayOutputStream
import java.nio.charset.StandardCharsets
import java.util.ArrayDeque

data class ScanDev(
    val address: String,
    val name: String,
    val rssi: Int,
    val device: BluetoothDevice,
)

/**
 * Nations UKEY：Service 0xFEE7 / Char 0xFEC1（Write Command + Notify）。
 * 一行 JSON + `\n`，无 NS_BlueTooth 2 字节长度头。Notify 按 `\n` 拼行。
 */
class BleClient(
    private val context: Context,
    private val onScan: (List<ScanDev>) -> Unit,
    private val onLine: (String) -> Unit,
    private val onStatus: (String) -> Unit,
    private val onReady: (Boolean) -> Unit,
) {
    private val main = Handler(Looper.getMainLooper())
    private val scanner by lazy {
        android.bluetooth.BluetoothAdapter.getDefaultAdapter()?.bluetoothLeScanner
    }
    private val found = LinkedHashMap<String, ScanDev>()
    private var heard = 0
    private var gatt: BluetoothGatt? = null
    private var charac: BluetoothGattCharacteristic? = null
    private var mtuPayload = 20
    private var ready = false
    private val txQ = ArrayDeque<ByteArray>()
    private var txBusy = false
    private val rx = ByteArrayOutputStream()
    private var scanning = false

    private val scanCb = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            accept(result)
        }

        override fun onBatchScanResults(results: MutableList<ScanResult>) {
            results.forEach { accept(it) }
        }

        override fun onScanFailed(errorCode: Int) {
            main.post { onStatus("扫描失败 $errorCode") }
        }
    }

    @SuppressLint("MissingPermission")
    private fun accept(result: ScanResult) {
        heard++
        val rec = result.scanRecord
        val name = rec?.deviceName
            ?: parseAdvName(rec?.bytes)
            ?: result.device.name
            ?: ""
        val hasFee7 = rec?.serviceUuids?.any { it.uuid == BleUuids.SERVICE } == true
        val hit = name.startsWith(BleUuids.NAME_PREFIX) || hasFee7
        if (hit) {
            val shown = name.ifEmpty { "${BleUuids.NAME_PREFIX}? ${result.device.address}" }
            found[result.device.address] = ScanDev(
                address = result.device.address,
                name = shown,
                rssi = result.rssi,
                device = result.device,
            )
            main.post { onScan(found.values.toList()) }
        }
        if (heard == 1 || heard % 20 == 0) {
            main.post { onStatus("扫描中 已听${heard}个 命中${found.size}") }
        }
    }

    private val gattCb = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                main.post { onStatus("已连接，协商 MTU") }
                g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                if (!g.requestMtu(247)) {
                    g.discoverServices()
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                drop("断开 ($status)")
            }
        }

        @SuppressLint("MissingPermission")
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            mtuPayload = (mtu - 3).coerceAtLeast(20)
            main.post { onStatus("MTU=$mtu") }
            g.discoverServices()
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(BleUuids.SERVICE)
            val ch = svc?.getCharacteristic(BleUuids.CHAR)
            if (ch == null) {
                drop("找不到 0xFEE7/0xFEC1")
                return
            }
            charac = ch
            if (!g.setCharacteristicNotification(ch, true)) {
                drop("打开 Notify 失败")
                return
            }
            val cccd = ch.getDescriptor(BleUuids.CCCD)
            if (cccd == null) {
                markReady()
                return
            }
            cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            if (!g.writeDescriptor(cccd)) {
                drop("写 CCCD 失败")
            }
        }

        override fun onDescriptorWrite(
            g: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int,
        ) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                drop("CCCD status=$status")
                return
            }
            markReady()
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            onNotify(characteristic.value ?: return)
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            onNotify(value)
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            /* Write Command 用延时踢队列，避免与回调重复发下一包 */
        }
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        found.clear()
        heard = 0
        onScan(emptyList())
        scanning = true
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .setReportDelay(0)
            .setCallbackType(ScanSettings.CALLBACK_TYPE_ALL_MATCHES)
            .setMatchMode(ScanSettings.MATCH_MODE_AGGRESSIVE)
            .setNumOfMatches(ScanSettings.MATCH_NUM_MAX_ADVERTISEMENT)
            .build()
        try {
            /* 空过滤 + 低延迟；不要按名字 ScanFilter（缩短名 0x08 对不上） */
            scanner?.startScan(emptyList(), settings, scanCb)
            onStatus("扫描中（请打开定位）…")
        } catch (e: Exception) {
            onStatus("扫描异常：${e.message}")
        }
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        if (!scanning) return
        scanning = false
        try {
            scanner?.stopScan(scanCb)
        } catch (_: Exception) {
        }
    }

    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) {
        stopScan()
        closeGatt()
        ready = false
        onReady(false)
        onStatus("连接 ${device.address}")
        gatt = if (Build.VERSION.SDK_INT >= 23) {
            device.connectGatt(context, false, gattCb, BluetoothDevice.TRANSPORT_LE)
        } else {
            device.connectGatt(context, false, gattCb)
        }
    }

    fun disconnect() {
        drop("主动断开")
    }

    fun isReady(): Boolean = ready

    fun writeLine(text: String) {
        if (!ready) throw IllegalStateException("未就绪")
        val payload = (text.trimEnd('\r', '\n') + "\n").toByteArray(StandardCharsets.UTF_8)
        val chunk = mtuPayload
        var off = 0
        synchronized(txQ) {
            while (off < payload.size) {
                val n = minOf(chunk, payload.size - off)
                txQ.addLast(payload.copyOfRange(off, off + n))
                off += n
            }
        }
        main.post { if (!txBusy) nextTx() }
    }

    private fun onNotify(value: ByteArray) {
        synchronized(rx) {
            rx.write(value)
            val all = rx.toByteArray()
            var start = 0
            for (i in all.indices) {
                val b = all[i]
                if (b == '\n'.code.toByte() || b == '\r'.code.toByte()) {
                    if (i > start) {
                        val line = String(all, start, i - start, StandardCharsets.UTF_8)
                        if (line.isNotEmpty()) main.post { onLine(line) }
                    }
                    start = i + 1
                }
            }
            rx.reset()
            if (start < all.size) {
                rx.write(all, start, all.size - start)
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun nextTx() {
        val ch = charac
        val g = gatt
        val pkt: ByteArray?
        synchronized(txQ) {
            pkt = txQ.pollFirst()
            txBusy = pkt != null
        }
        if (pkt == null || ch == null || g == null) {
            txBusy = false
            return
        }
        ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        ch.value = pkt
        if (!g.writeCharacteristic(ch)) {
            synchronized(txQ) {
                txQ.addFirst(pkt)
            }
            main.postDelayed({ nextTx() }, 30)
            return
        }
        /* Write Command 有的机子不回调 onCharacteristicWrite，定时踢下一包 */
        main.postDelayed({
            if (txBusy) nextTx()
        }, 40)
    }

    private fun markReady() {
        ready = true
        main.post {
            onStatus("GATT 就绪")
            onReady(true)
        }
    }

    @SuppressLint("MissingPermission")
    private fun drop(why: String) {
        ready = false
        synchronized(txQ) { txQ.clear() }
        txBusy = false
        synchronized(rx) { rx.reset() }
        charac = null
        closeGatt()
        main.post {
            onStatus(why)
            onReady(false)
        }
    }

    @SuppressLint("MissingPermission")
    private fun closeGatt() {
        try {
            gatt?.disconnect()
        } catch (_: Exception) {
        }
        try {
            gatt?.close()
        } catch (_: Exception) {
        }
        gatt = null
    }

    companion object {
        /** 官方例程用 0x08 缩短名，安卓 getDeviceName() 经常只认 0x09 */
        fun parseAdvName(raw: ByteArray?): String? {
            if (raw == null) return null
            var i = 0
            var shortened: String? = null
            while (i + 1 < raw.size) {
                val len = raw[i].toInt() and 0xFF
                if (len == 0 || i + len >= raw.size) break
                val type = raw[i + 1].toInt() and 0xFF
                if (type == 0x08 || type == 0x09) {
                    val n = (len - 1).coerceAtLeast(0)
                    if (n > 0) {
                        val s = String(raw, i + 2, n, StandardCharsets.UTF_8).trim()
                        if (s.isNotEmpty()) {
                            if (type == 0x09) return s
                            shortened = s
                        }
                    }
                }
                i += len + 1
            }
            return shortened
        }
    }
}
