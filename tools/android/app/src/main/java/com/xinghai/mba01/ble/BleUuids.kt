package com.xinghai.mba01.ble

import java.util.UUID

object BleUuids {
    const val NAME_PREFIX = "MBA01"
    val SERVICE: UUID = UUID.fromString("0000fee7-0000-1000-8000-00805f9b34fb")
    val CHAR: UUID = UUID.fromString("0000fec1-0000-1000-8000-00805f9b34fb")
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
}
