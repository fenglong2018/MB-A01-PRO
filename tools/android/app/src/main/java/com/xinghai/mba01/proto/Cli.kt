package com.xinghai.mba01.proto

import org.json.JSONObject

object Cli {
    fun buildCmd(cmd: String, id: Int, fields: Map<String, Any?> = emptyMap()): String {
        val obj = JSONObject()
        obj.put("id", id)
        obj.put("cmd", cmd)
        for ((k, v) in fields) {
            if (v == null) continue
            obj.put(k, v)
        }
        return obj.toString()
    }

    fun tryParseRsp(line: String): JSONObject? {
        val s = line.trim()
        if (!s.startsWith("{")) return null
        return try {
            val obj = JSONObject(s)
            if (obj.optString("type") == "rsp") obj else null
        } catch (_: Exception) {
            null
        }
    }

    fun timeoutMs(cmd: String): Long = when (cmd) {
        "test.gnss.fix" -> 45_000L
        "test.rdss.send" -> 60_000L
        "test.rdss.card" -> 15_000L
        "test.session.once" -> 90_000L
        else -> 5_000L
    }
}
