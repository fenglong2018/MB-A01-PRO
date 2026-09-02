package com.xinghai.mba01.radio

data class GnssSat(
    val prn: Int,
    val sys: String,
    val elv: Int?,
    val az: Int?,
    val snr: Int?,
)

data class GnssSnap(
    var inView: Int = 0,
    var inUse: Int? = null,
    var quality: Int? = null,
    var sats: List<GnssSat> = emptyList(),
)

data class RdssBeam(val bid: Int, val s2c: Int?)

data class RdssSnap(
    var pwiTime: Double? = null,
    var beamN: Int = 0,
    var beams: List<RdssBeam> = emptyList(),
    var good: Boolean = false,
)

/** 与 tools/host_pc/shy_host/radio_parse.py 同一套 GSV/GGA/$BDPWI。 */
class RadioParser {
    var gnss = GnssSnap()
        private set
    var rdss = RdssSnap()
        private set

    private val gsvAcc = mutableMapOf<String, MutableMap<Int, GnssSat>>()
    private val gsvExpect = mutableMapOf<String, Int>()
    private val gsvSeen = mutableMapOf<String, Int>()

    fun reset() {
        gnss = GnssSnap()
        rdss = RdssSnap()
        gsvAcc.clear()
        gsvExpect.clear()
        gsvSeen.clear()
    }

    fun feed(line: String): Boolean {
        val f = fields(line) ?: return false
        val (talker, kind) = talkerKind(f[0])
        when {
            kind == "GSV" -> {
                onGsv(talker, f)
                return true
            }
            kind == "GGA" -> {
                onGga(f)
                return true
            }
            f[0].uppercase() == "BDPWI" -> {
                onPwi(f)
                return true
            }
        }
        return false
    }

    private fun onGga(f: List<String>) {
        if (f.size > 6) gnss.quality = toInt(f[6])
        if (f.size > 7) gnss.inUse = toInt(f[7])
    }

    private fun onGsv(sys: String, f: List<String>) {
        val nmsg = toInt(f.getOrNull(1), 0) ?: 0
        val msgi = toInt(f.getOrNull(2), 0) ?: 0
        if (msgi <= 1) {
            gsvAcc[sys] = mutableMapOf()
            gsvSeen[sys] = 0
            gsvExpect[sys] = nmsg
        }
        val bucket = gsvAcc.getOrPut(sys) { mutableMapOf() }
        var i = 4
        while (i + 3 < f.size) {
            val prn = toInt(f[i])
            if (prn != null && prn != 0) {
                bucket[prn] = GnssSat(
                    prn = prn,
                    sys = sys,
                    elv = toInt(f[i + 1]),
                    az = toInt(f[i + 2]),
                    snr = toInt(f[i + 3]),
                )
            }
            i += 4
        }
        gsvSeen[sys] = (gsvSeen[sys] ?: 0) + 1
        if (nmsg != 0 && (gsvSeen[sys] ?: 0) >= nmsg) {
            commitGsv()
        }
    }

    private fun commitGsv() {
        val sats = gsvAcc.values.flatMap { it.values }.sortedWith(
            compareBy<GnssSat> { it.snr == null }
                .thenByDescending { it.snr ?: 0 }
                .thenBy { it.sys }
                .thenBy { it.prn },
        )
        gnss.sats = sats
        if (sats.isNotEmpty()) gnss.inView = sats.size
    }

    private fun onPwi(f: List<String>) {
        val t = if (f.size > 1) toDouble(f[1]) else null
        var beamN = if (f.size > 3) toInt(f[3], 0) ?: 0 else 0
        if (beamN > 10) beamN = 10
        val beams = mutableListOf<RdssBeam>()
        var good = false
        for (i in 0 until beamN) {
            val idI = 4 + i * 3
            val cnrI = 5 + i * 3
            val bid = if (f.size > idI) toInt(f[idI]) else null
            val s2c = if (f.size > cnrI) toInt(f[cnrI]) else null
            if (bid == null) continue
            beams.add(RdssBeam(bid, s2c))
            if (s2c != null && s2c > 40) good = true
        }
        val timeOk = t != null && t > 20
        rdss = RdssSnap(
            pwiTime = t,
            beamN = beams.size,
            beams = beams,
            good = beams.isNotEmpty() && timeOk && good,
        )
    }

    companion object {
        private fun fields(line: String): List<String>? {
            var s = line.trim()
            if (!s.startsWith("$")) return null
            if ("*" in s) s = s.split("*", limit = 2)[0]
            return s.substring(1).split(",")
        }

        private fun talkerKind(head: String): Pair<String, String> {
            val h = head.uppercase()
            return if (h.length >= 3) h.dropLast(3) to h.takeLast(3) else "" to h
        }

        private fun toInt(s: String?, default: Int? = null): Int? {
            if (s.isNullOrEmpty()) return default
            return s.toDoubleOrNull()?.toInt() ?: default
        }

        private fun toDouble(s: String?): Double? {
            if (s.isNullOrEmpty()) return null
            return s.toDoubleOrNull()
        }
    }
}
