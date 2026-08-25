"""从透传行解析 GNSS GSV/GGA 与 RDSS $BDPWI。"""

from __future__ import annotations

from dataclasses import dataclass, field


def _fields(line: str) -> list[str] | None:
    s = line.strip()
    if not s.startswith("$"):
        return None
    if "*" in s:
        s = s.split("*", 1)[0]
    return s[1:].split(",")


def _talker_kind(head: str) -> tuple[str, str]:
    """'GPGSV' -> ('GP', 'GSV')"""
    h = (head or "").upper()
    if len(h) >= 3:
        return h[:-3], h[-3:]
    return "", h


def _to_int(s: str, default: int | None = None) -> int | None:
    try:
        if s == "":
            return default
        return int(float(s))
    except (TypeError, ValueError):
        return default


def _to_float(s: str, default: float | None = None) -> float | None:
    try:
        if s == "":
            return default
        return float(s)
    except (TypeError, ValueError):
        return default


@dataclass
class GnssSat:
    prn: int
    sys: str
    elv: int | None = None
    az: int | None = None
    snr: int | None = None


@dataclass
class GnssSnap:
    in_view: int = 0
    in_use: int | None = None
    quality: int | None = None
    sats: list[GnssSat] = field(default_factory=list)


@dataclass
class RdssBeam:
    bid: int
    s2c: int | None


@dataclass
class RdssSnap:
    pwi_time: float | None = None
    beam_n: int = 0
    beams: list[RdssBeam] = field(default_factory=list)
    good: bool = False


class RadioParser:
    """累积 GSV 多句；每句 PWI 覆盖波束表。"""

    def __init__(self) -> None:
        self.gnss = GnssSnap()
        self.rdss = RdssSnap()
        self._gsv_acc: dict[str, dict[int, GnssSat]] = {}
        self._gsv_expect: dict[str, int] = {}
        self._gsv_seen: dict[str, int] = {}

    def reset(self) -> None:
        self.gnss = GnssSnap()
        self.rdss = RdssSnap()
        self._gsv_acc.clear()
        self._gsv_expect.clear()
        self._gsv_seen.clear()

    def feed(self, line: str) -> bool:
        f = _fields(line)
        if not f:
            return False
        talker, kind = _talker_kind(f[0])
        if kind == "GSV":
            self._on_gsv(talker, f)
            return True
        if kind == "GGA":
            self._on_gga(f)
            return True
        if f[0].upper() == "BDPWI":
            self._on_pwi(f)
            return True
        return False

    def _on_gga(self, f: list[str]) -> None:
        # GGA: 6=quality 7=numSV
        if len(f) > 6:
            self.gnss.quality = _to_int(f[6])
        if len(f) > 7:
            self.gnss.in_use = _to_int(f[7])

    def _on_gsv(self, sys: str, f: list[str]) -> None:
        nmsg = _to_int(f[1], 0) or 0
        msgi = _to_int(f[2], 0) or 0
        if msgi <= 1:
            self._gsv_acc[sys] = {}
            self._gsv_seen[sys] = 0
            self._gsv_expect[sys] = nmsg
        bucket = self._gsv_acc.setdefault(sys, {})
        i = 4
        while i + 3 < len(f):
            prn = _to_int(f[i])
            if prn:
                bucket[prn] = GnssSat(
                    prn=prn,
                    sys=sys,
                    elv=_to_int(f[i + 1]),
                    az=_to_int(f[i + 2]),
                    snr=_to_int(f[i + 3]),
                )
            i += 4
        self._gsv_seen[sys] = self._gsv_seen.get(sys, 0) + 1
        if nmsg and self._gsv_seen[sys] >= nmsg:
            self._commit_gsv()

    def _commit_gsv(self) -> None:
        sats: list[GnssSat] = []
        view = 0
        for sys, bucket in self._gsv_acc.items():
            sats.extend(bucket.values())
            view += len(bucket)
        sats.sort(key=lambda s: (s.snr is None, -(s.snr or 0), s.sys, s.prn))
        self.gnss.sats = sats
        if view:
            self.gnss.in_view = view

    def _on_pwi(self, f: list[str]) -> None:
        # idx: 0=BDPWI 1=time 3=beam_n; 4+i*3 id, 5+i*3 S2C_d
        t = _to_float(f[1]) if len(f) > 1 else None
        beam_n = _to_int(f[3], 0) if len(f) > 3 else 0
        beam_n = beam_n or 0
        if beam_n > 10:
            beam_n = 10
        beams: list[RdssBeam] = []
        good = False
        for i in range(beam_n):
            id_i = 4 + i * 3
            cnr_i = 5 + i * 3
            bid = _to_int(f[id_i]) if len(f) > id_i else None
            s2c = _to_int(f[cnr_i]) if len(f) > cnr_i else None
            if bid is None:
                continue
            beams.append(RdssBeam(bid=bid, s2c=s2c))
            if s2c is not None and s2c > 40:
                good = True
        time_ok = t is not None and t > 20
        self.rdss = RdssSnap(
            pwi_time=t,
            beam_n=len(beams),
            beams=beams,
            good=bool(beams) and time_ok and good,
        )
