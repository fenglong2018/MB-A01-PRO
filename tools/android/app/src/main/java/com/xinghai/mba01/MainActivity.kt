package com.xinghai.mba01

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.content.pm.PackageManager
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.core.content.ContextCompat
import com.xinghai.mba01.ui.HostApp

class MainActivity : ComponentActivity() {
    private val vm: AppViewModel by viewModels()

    private val permLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { granted ->
        val need = neededPerms().filter { it != Manifest.permission.ACCESS_COARSE_LOCATION }
        val ok = need.all { p ->
            granted[p] == true ||
                ContextCompat.checkSelfPermission(this, p) == PackageManager.PERMISSION_GRANTED
        }
        if (ok) {
            ensureBtOnThenScan()
        } else {
            vm.setField { copy(status = "需要蓝牙和定位权限才能扫描") }
        }
    }

    private val enableBt = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult(),
    ) {
        if (btOn()) ensureBtOnThenScan() else vm.setField { copy(status = "请打开蓝牙") }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            HostApp(
                vm = vm,
                onScanClick = { requestThenScan() },
            )
        }
    }

    private fun requestThenScan() {
        val missing = neededPerms().filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            permLauncher.launch(missing.toTypedArray())
            return
        }
        ensureBtOnThenScan()
    }

    private fun ensureBtOnThenScan() {
        if (!btOn()) {
            enableBt.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            return
        }
        if (!locationOn()) {
            vm.setField { copy(status = "请打开系统定位后再扫描（国产机不开放扫不到 BLE）") }
            startActivity(Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS))
            return
        }
        vm.startScan()
    }

    private fun btOn(): Boolean {
        val bm = getSystemService(BluetoothManager::class.java)
        return bm.adapter?.isEnabled == true
    }

    private fun locationOn(): Boolean {
        val lm = getSystemService(LocationManager::class.java) ?: return true
        return if (Build.VERSION.SDK_INT >= 28) {
            lm.isLocationEnabled
        } else {
            lm.isProviderEnabled(LocationManager.GPS_PROVIDER) ||
                lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)
        }
    }

    private fun neededPerms(): List<String> = buildList {
        if (Build.VERSION.SDK_INT >= 31) {
            add(Manifest.permission.BLUETOOTH_SCAN)
            add(Manifest.permission.BLUETOOTH_CONNECT)
        }
        add(Manifest.permission.ACCESS_FINE_LOCATION)
        add(Manifest.permission.ACCESS_COARSE_LOCATION)
    }
}
