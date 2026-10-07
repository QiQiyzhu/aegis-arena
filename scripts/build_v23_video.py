"""Build a reviewable v2.3 delivery from one verified native capture.

This is an editorial delivery script. It does not alter the game or the
capture pipeline: native frames and issued audio events are read from a
preserved capture, selected in chronological EDL order, and written with
provenance. The default EDL keeps about 111 seconds of source gameplay and
adds five seconds of title plus seven seconds of result card, for a delivery
of about 123 seconds. ``--source-version`` makes the distinction explicit
when a v2.1 gameplay capture supplies the v2.3 visual/language presentation.
``--target-mb 50`` optionally budgets a roughly 50,000,000-byte delivery with
two passes over the original PNG/card inputs; no intermediate lossy video is
used. The existing single-pass CRF mode remains the default.
"""
from __future__ import annotations

import argparse
from bisect import bisect_left, bisect_right
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time
import wave

from PIL import Image, ImageDraw, ImageFont

from run_v2_capture import validate_system_capture
import v21_audio
from v21_audio import mix_audio, validate_music

FPS = 30
RATE = 48000
DEFAULT_FFMPEG = Path(
    "D:/AegisWork/Tools/decision-video/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe"
)
NAVY = "#0b1822"
PANEL = "#142b37"
MINT = "#79e4c8"
AMETHYST = "#b99cff"
WHITE = "#eef5f3"
MUTED = "#9cb4b5"
AMBER = "#edc477"
AUDIO_BITRATE = 160_000
MUX_RESERVE = .02

# Source seconds are deliberately kept as a visible, reviewable EDL. The
# diagnostics interval 37.60--48.43 is excluded; every other interval below
# stays chronological and uses the original native frame clock. No source
# frame is generated, re-ordered, or speed changed.
DEFAULT_EDL = [
    ("briefing_and_stage1", 0.00, 16.20, "中文默认与 L 英文切换；进入目标并推进中继"),
    ("upgrade_1", 16.20, 24.73, "恢复中文并完成第一次升级；准备下一段路线"),
    ("stage2_charge", 24.73, 37.60, "蓄力射击、脉冲打断、超频与中继推进"),
    ("upgrade_2", 48.43, 57.00, "第二次升级；调整火力与资源节奏"),
    ("stage3_open", 57.00, 70.00, "第三阶段开局；处理威胁并与队友协同"),
    ("stage3_route", 70.00, 83.00, "沿地图路线转移；在输出与修复之间取舍"),
    ("stage3_mid", 83.00, 94.00, "掩体走位、火力交换与队友状态反馈"),
    ("stage3_recover", 94.00, 108.00, "修复资源并为最后的撤离保留余量"),
    ("stage3_finish", 108.00, 121.90, "清敌、进入撤离区并完成结算闭环"),
]


def require(ok: bool, message: str):
    if not ok:
        raise ValueError(message)


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def strict_json(path: Path):
    def pairs(rows):
        result = {}
        for key, value in rows:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result

    return json.loads(
        Path(path).read_text(encoding="utf-8-sig"),
        object_pairs_hook=pairs,
        parse_constant=lambda x: (_ for _ in ()).throw(ValueError(f"non-finite JSON: {x}")),
    )


def finite(value, label: str) -> float:
    require(
        isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value),
        f"{label} must be finite",
    )
    return float(value)


def validate_source(capture: dict, source_version: str, preview: bool) -> float:
    require(capture.get("presentationVersion") == source_version,
            f"capture presentationVersion must be {source_version}")
    require(capture.get("validRun") is True, "capture did not pass native completion")
    require(capture.get("scriptedPlayer") is True and capture.get("fixtureDamage") is False
            and capture.get("humanPlaytest") is False,
            "unsupported input/provenance flags")
    require(capture.get("outcome") in ("won", "lost"), "capture has no natural outcome")
    rows = capture.get("frameTimes", [])
    require(len(rows) >= 2 and capture.get("frames") == len(rows) == capture.get("completedFrames"),
            "missing or incomplete native frames")
    times = [finite(row.get("videoSeconds"), "frame time") for row in rows]
    require(times[0] >= 0 and all(b > a for a, b in zip(times, times[1:])),
            "frame times are not strictly increasing")
    interval = times[1] - times[0]
    require(all(abs((b - a) - interval) < 1e-5 for a, b in zip(times, times[1:])),
            "native frame intervals are irregular")
    if not preview:
        require(abs(interval - 1 / FPS) < 1e-5, "final film requires native 1/30 frame spacing")
    for i, row in enumerate(rows):
        require(row.get("file") == f"frame-{i:06d}.png", "frame order/path is not canonical")
        finite(row.get("worldSeconds"), "world time")
    samples = capture.get("samples", [])
    require(samples, "missing native phase samples")
    sample_times = [finite(s.get("videoSeconds"), "sample time") for s in samples]
    require(all(b >= a for a, b in zip(sample_times, sample_times[1:])), "sample clock regressed")
    for sample in samples:
        require(sample.get("phase") in range(5) and sample.get("wave") in range(4),
                "unknown native phase/wave")
        require(isinstance(sample.get("diagnostics"), bool), "missing diagnostic state")
    events = capture.get("audioEvents", [])
    require(preview or bool(events), "final delivery requires genuine audioEvents")
    previous = -1.0
    for event in events:
        t = finite(event.get("videoSeconds"), "audio clock")
        require(previous <= t <= times[-1] + interval + .1 and t >= 0, "invalid audio clock")
        previous = t
        name = event.get("asset", "")
        require(re.fullmatch(r"S_[A-Za-z0-9_]+", name or "") is not None, "unsafe SFX asset")
        require(event.get("assetPath") == f"/Game/Aegis/V21/Audio/{name}.{name}",
                "SFX asset is not bound to the original V21 source")
        require(0 <= finite(event.get("volume"), "SFX volume") <= 4, "invalid SFX volume")
        require(finite(event.get("pitch"), "SFX pitch") == 1, "non-unit pitch is unsupported")
    validate_music(capture.get("musicEvents"), times[-1] + interval)
    return interval


def validate_ledger(ledger: dict, capture: dict):
    require(ledger.get("ledgerBalanced") is True and ledger.get("traceComplete") is True
            and ledger.get("completed") is True, "system ledger is incomplete")
    require(ledger.get("outcome") == capture.get("outcome"), "ledger/capture outcomes disagree")
    require(ledger.get("mode") == "aegis-prism-fall-v2.0", "unexpected gameplay ledger mode")
    for key in ("energyInitial", "energyFinal", "energyEarned", "energySpent", "energyOverflow",
                "pulsesUsed", "repairsUsed", "enemiesRewarded", "stagesRewarded", "eventCount",
                "upgradeMask"):
        require(type(ledger.get(key)) is int and ledger[key] >= 0, f"invalid ledger counter {key}")
    for key in ("trialElapsedSeconds", "repairPlayerActualHealing", "repairCompanionActualHealing",
                "alliedActualDamageDealt", "playerActualDamageTaken", "companionActualDamageTaken"):
        require(finite(ledger.get(key), key) >= 0, f"negative ledger value {key}")
    require(ledger["energyInitial"] + ledger["energyEarned"] - ledger["energySpent"] == ledger["energyFinal"],
            "unbalanced energy ledger")
    require(ledger["energyEarned"] + ledger["energyOverflow"] ==
            ledger["enemiesRewarded"] * 12 + ledger["stagesRewarded"] * 25,
            "income does not match native rewards")


def validate_editorial_capture(capture: dict, provenance: dict, ledger: dict,
                               editorial_version: str):
    """Require the v2.3 language pass and record only source-backed signals.

    The source files remain untouched. These checks prevent a v2.3 delivery
    from silently falling back to an older capture, while the returned object
    makes the real gameplay counters reviewable beside the encoded video.
    """
    if editorial_version != "2.3":
        return {"checked": False, "reason": "editorial version is not 2.3"}
    require(capture.get("visualDeliveryVersion") == "2.3",
            "v2.3 delivery requires capture visualDeliveryVersion=2.3")
    require(capture.get("languageDemo") is True,
            "v2.3 delivery requires the native Chinese/English language demo")
    require(provenance.get("visualDeliveryVersion") == "2.3",
            "v2.3 delivery requires provenance visualDeliveryVersion=2.3")
    counters = {
        "routeSelected": isinstance(ledger.get("northRouteFirst"), bool),
        "chargedShot": ledger.get("chargedShots", 0) > 0,
        "pulse": ledger.get("pulsesUsed", 0) > 0,
        "repair": ledger.get("repairsUsed", 0) > 0,
        "overclock": ledger.get("overclocks", 0) > 0,
        "companionStateTransitions": ledger.get("companionStateChanges", 0),
        "companionSurvived": ledger.get("companionSurvived") is True,
        "stagesCompleted": ledger.get("stagesRewarded", 0),
    }
    require(all(counters[name] for name in
                ("routeSelected", "chargedShot", "pulse", "repair", "overclock",
                 "companionSurvived")),
            "v2.3 capture does not contain every required gameplay signal")
    require(counters["stagesCompleted"] >= 3,
            "v2.3 capture did not complete all three stages")
    if capture.get("gameplayVersion") == "2.3":
        samples = capture.get("samples", [])
        require(samples and isinstance(samples[-1], dict),
                "v2.3 gameplay delivery requires a final survey sample")
        last = samples[-1]
        for key in ("surveyKeys", "surveySupplies", "surveyBoosts"):
            value = finite(last.get(key), key)
            require(value == int(value) and value >= 1,
                    "v2.3 gameplay delivery is missing native " + key)
            counters[key] = int(value)
        mask = finite(last.get("surveyClaimedMask"), "surveyClaimedMask")
        require(mask == 3, "v2.3 gameplay delivery must demonstrate both survey caches")
        counters["surveyClaimedMask"] = int(mask)
    return {
        "checked": True,
        "capturePresentationVersion": capture.get("presentationVersion"),
        "captureVisualDeliveryVersion": capture.get("visualDeliveryVersion"),
        "provenanceVisualDeliveryVersion": provenance.get("visualDeliveryVersion"),
        "languageDemo": capture.get("languageDemo"),
        "captureGameplayVersion": capture.get("gameplayVersion"),
        "inputFlags": {
            "scriptedPlayer": capture.get("scriptedPlayer"),
            "fixtureDamage": capture.get("fixtureDamage"),
            "humanPlaytest": capture.get("humanPlaytest"),
        },
        "gameplaySignals": counters,
        "outcome": ledger.get("outcome"),
    }


def frame_indices(rows, start: float, end: float):
    times = [r["videoSeconds"] for r in rows]
    first = bisect_left(times, start)
    last = bisect_left(times, end)
    require(last > first, f"EDL range has no native frames: {start}--{end}")
    return first, last


def validate_edl(edl, source_end: float):
    previous_end = 0.0
    for index, (label, start, end, reason) in enumerate(edl):
        start, end = finite(start, f"EDL[{index}] start"), finite(end, f"EDL[{index}] end")
        require(isinstance(label, str) and bool(label) and isinstance(reason, str),
                f"EDL[{index}] label/reason is invalid")
        require(start >= previous_end - 1e-6,
                f"EDL is out of order or overlapping at {label}")
        require(end > start, f"EDL range is empty at {label}")
        require(end <= source_end + 1e-4,
                f"EDL range exceeds native capture at {label}")
        previous_end = end


def excluded_intervals(edl, source_end: float):
    gaps = []
    cursor = 0.0
    for _, start, end, _ in edl:
        if start > cursor + 1e-4:
            gaps.append([round(cursor, 6), round(start, 6)])
        cursor = max(cursor, end)
    if source_end > cursor + 1e-4:
        gaps.append([round(cursor, 6), round(source_end, 6)])
    return gaps


def sample_for(capture: dict, second: float):
    samples = capture["samples"]
    times = [s["videoSeconds"] for s in samples]
    return samples[max(0, bisect_right(times, second) - 1)]


def short_copy(capture: dict, second: float, label: str):
    editorial_copy = {
        "mission_briefing": ("行动简报", "中文默认 / L English\n切换语言并选择路线"),
        "stage1_open": ("01 / 探索", "接近场景中的缓存\n观察威胁与行动路线"),
        "stage1_objective": ("中继推进", "密钥接入当前中继\n持续占领完成推进"),
        "upgrade_first": ("第一次升级", "比较效果与资源代价\n选择下一阶段的构筑"),
        "stage2_transfer": ("02 / 转移", "沿路线转移位置\n推进两座数据中继"),
        "upgrade_second": ("第二次升级", "调整下一阶段构筑\n准备最后撤离流程"),
        "survey_key": ("扫描与密钥", "G 连续扫描\n密钥加速中继"),
        "survey_supply": ("探索与补给", "H 选择补给\n实际治疗与绕路收益"),
        "briefing_and_stage1": ("目标与占领", "中文默认 / L English\n先读目标，再推进中继"),
        "upgrade_1": ("第一次升级", "恢复中文并调整构筑\n队友状态进入下一段"),
        "stage2_charge": ("02 / 转移", "蓄力射击 + Q 脉冲\nF 超频推进中继"),
        "upgrade_2": ("第二次升级", "改变火力与资源节奏\n为撤离段保留余量"),
        "stage3_open": ("03 / 撤离", "先处理场上威胁\n与队友保持协同位置"),
        "stage3_route": ("路线与协作", "沿地图动线转移\n队友反馈保持可见"),
        "stage3_mid": ("走位与火力", "利用掩体交换火力\n观察队友状态"),
        "stage3_recover": ("修复与资源", "E 修复实际生命\n为撤离保留余量"),
        "stage3_finish": ("撤离与结算", "清敌后进入撤离区\n完成流程闭环"),
    }
    if label in editorial_copy:
        return editorial_copy[label]
    sample = sample_for(capture, second)
    phase, wave_no = sample.get("phase"), sample.get("wave")
    if "upgrade" in label:
        return ("构筑选择", "读效果与代价，再做选择")
    if "charge" in label:
        return ("蓄能爆发", "蓄力寻找开火时机\n松手释放爆发")
    if phase == 0:
        return ("行动简报", "先读目标，再开火")
    if phase in (3, 4):
        return ("行动结算", "清敌后进入撤离区\n完成关卡目标")
    if wave_no == 1:
        return ("01 / 占领", "清敌后推进，站位决定速度")
    if wave_no == 2:
        return ("02 / 转移", "路线改变下一段战斗空间")
    return ("03 / 撤离", "保留资源，完成最后收束")


class Art:
    def __init__(self, font_dir: Path):
        self.regular = font_dir / "msyh.ttc"
        self.bold = font_dir / "msyhbd.ttc"
        require(self.regular.is_file() and self.bold.is_file(), "Microsoft YaHei fonts unavailable")

    def font(self, size: int, bold=False):
        return ImageFont.truetype(str(self.bold if bold else self.regular), size)

    def text(self, draw, xy, text, size, color=WHITE, bold=False):
        draw.text(xy, text, font=self.font(size, bold), fill=color)

    def base(self):
        image = Image.new("RGB", (1920, 1080), NAVY)
        d = ImageDraw.Draw(image)
        for x in range(-300, 2200, 88):
            d.line((x, 0, x + 320, 1080), fill="#102632", width=1)
        d.rectangle((28, 34, 35, 94), fill=MINT)
        d.line((28, 1014, 1888, 1014), fill="#274754", width=2)
        return image, d

    def gameplay(self, title, subtitle, destination: Path):
        image, d = self.base()
        self.text(d, (52, 34), "AEGIS ARENA", 42, WHITE, True)
        self.text(d, (365, 45), "PRISM FALL v2.3  /  实机节选 · 玩法流程", 24, MINT)
        self.text(d, (52, 1030), "Unreal Engine 实机  ·  脚本输入演示", 18, MUTED)
        self.text(d, (1738, 1030), "DEMO v2.3", 18, AMBER)
        # 1580x889 preserves 16:9 and leaves a narrow annotation rail.
        d.rectangle((28, 110, 1608, 1008), outline="#587c83", width=2)
        d.rectangle((1638, 110, 1888, 1008), fill=PANEL)
        d.rectangle((1638, 110, 1643, 1008), fill=AMETHYST)
        self.text(d, (1660, 146), title, 27, WHITE, True)
        # Two short annotations; validate their width before rendering.
        for line_index, line in enumerate(subtitle.splitlines()):
            require(d.textlength(line, font=self.font(20)) <= 212,
                    "chapter annotation exceeds the rail width")
            self.text(d, (1660, 216 + 38 * line_index), line, 20, MINT)
        d.line((1660, 300, 1868, 300), fill="#3b5b68", width=1)
        image.save(destination)

    def intro(self, destination: Path, gameplay_v23=False):
        image, d = self.base()
        self.text(d, (84, 116), "可玩流程演示", 30, AMBER)
        self.text(d, (84, 226), "AEGIS ARENA", 94, WHITE, True)
        self.text(d, (90, 357), "PRISM FALL / DEMO v2.3", 44, MINT, True)
        features = ("扫描探索  ·  中英界面  ·  路线选择  ·  阶段升级  ·  同伴协作" if gameplay_v23 else
                    "中英界面  ·  路线选择  ·  阶段升级  ·  同伴协作")
        self.text(d, (94, 535), features, 32, WHITE)
        self.text(d, (94, 938), "UNREAL ENGINE  ·  原生实机  ·  脚本输入演示", 20, MUTED)
        image.save(destination)

    def outro(self, ledger: dict, destination: Path):
        image, d = self.base()
        self.text(d, (84, 118), "关卡收束", 30, AMBER)
        self.text(d, (84, 228), "探索判断 → 阶段升级 → 撤离结算", 56, WHITE, True)
        outcome = "完成撤离" if ledger.get("outcome") == "won" else "行动失败"
        self.text(d, (90, 346), f"{outcome}  ·  {ledger.get('stagesRewarded', 0)} / 3 阶段", 32, MINT)
        self.text(d, (90, 510), "设计重点：让共享能量在输出、恢复与目标推进之间形成取舍。", 30, WHITE)
        self.text(d, (90, 938), "AEGIS ARENA  ·  DEMO v2.3  ·  可玩流程与系统演示", 20, MUTED)
        image.save(destination)


def quote_concat(path: Path) -> str:
    return str(path.resolve()).replace("\\", "/").replace("'", "'\\''")


def run(command, log: Path, timeout=2400):
    with log.open("w", encoding="utf-8") as f:
        result = subprocess.run([str(x) for x in command], stdout=f, stderr=subprocess.STDOUT,
                                timeout=timeout)
    require(result.returncode == 0, f"ffmpeg failed; see {log}")


def encoding_plan(duration: float, crf: int = 20, target_mb: float | None = None):
    """Budget decimal MB for video, AAC and MP4 overhead without an intermediate MP4."""
    duration = finite(duration, "delivery duration")
    require(duration > 0, "delivery duration must be positive")
    if target_mb is None:
        require(crf in (18, 20, 22), "unsupported CRF")
        return {"mode": "crf", "crf": crf, "audioBitrate": AUDIO_BITRATE,
                "source": "native PNG frames plus editorial cards and event-mixed PCM"}
    target_mb = finite(target_mb, "target MB")
    require(0 < target_mb < 300, "target MB must be between 0 and 300 (decimal MB)")
    target_bytes = round(target_mb * 1_000_000)
    video_bitrate = math.floor((target_bytes * 8 * (1 - MUX_RESERVE) / duration
                               - AUDIO_BITRATE) / 1000) * 1000
    require(video_bitrate >= 500_000,
            "target size leaves less than 500 kb/s for 1080p video; increase --target-mb")
    return {"mode": "two-pass-target-size", "targetMB": target_mb,
            "targetBytes": target_bytes, "videoBitrate": video_bitrate,
            "audioBitrate": AUDIO_BITRATE, "muxReserveFraction": MUX_RESERVE,
            "source": "native PNG frames plus editorial cards and event-mixed PCM",
            "sizeNote": "Target is approximate; simple scenes may use fewer bits. No padding or final-video re-encode."}


def encoding_commands(ffmpeg: Path, out: Path, audio: Path, video: Path,
                      expected: float, filters: str, plan: dict):
    """Both passes consume the same uncompressed source graph and frame count."""
    source = [ffmpeg, "-hide_banner", "-y", "-threads", "2", "-filter_complex_threads", "2",
              "-f", "concat", "-safe", "0", "-i", out / "selected-frames.txt",
              "-f", "concat", "-safe", "0", "-i", out / "backgrounds.txt",
              "-loop", "1", "-framerate", "30", "-i", out / "intro.png",
              "-loop", "1", "-framerate", "30", "-i", out / "outro.png",
              "-i", audio, "-filter_complex", filters]
    picture = ["-map", "[v]", "-c:v", "libx264", "-preset", "medium",
               "-pix_fmt", "yuv420p", "-r", "30", "-fps_mode", "cfr",
               "-frames:v", str(round(expected * FPS))]
    soundtrack = ["-map", "4:a:0", "-c:a", "aac", "-b:a", str(plan["audioBitrate"]),
                  "-ar", "48000", "-ac", "2", "-movflags", "+faststart", "-shortest"]
    if plan["mode"] == "crf":
        return [("encode.log", source + picture + ["-crf", str(plan["crf"])] + soundtrack + [video])]
    rate = ["-b:v", str(plan["videoBitrate"]), "-passlogfile", out / "x264-pass"]
    return [
        ("encode-pass1.log", source + picture + rate + ["-pass", "1", "-an", "-f", "null", os.devnull]),
        ("encode.log", source + picture + rate + ["-pass", "2"] + soundtrack + [video]),
    ]


def concat_audio(full_mix: Path, ranges, destination: Path, intro=5, outro=7):
    """Copy exact PCM ranges and put silent editorial cards around them."""
    with wave.open(str(full_mix), "rb") as src:
        params = src.getparams()
        raw = src.readframes(src.getnframes())
    channels, width, rate = params.nchannels, params.sampwidth, params.framerate
    require((channels, width, rate) == (2, 2, RATE), "unexpected reconstructed audio format")
    frame_bytes = channels * width
    silence_intro = b"\0" * round(intro * RATE) * frame_bytes
    silence_outro = b"\0" * round(outro * RATE) * frame_bytes
    chunks = [silence_intro]
    for start, end in ranges:
        a, b = round(start * RATE) * frame_bytes, round(end * RATE) * frame_bytes
        require(0 <= a < b <= len(raw), "EDL audio range exceeds the absolute capture clock")
        chunks.append(raw[a:b])
    chunks.append(silence_outro)
    with wave.open(str(destination), "wb") as out:
        out.setparams(params)
        out.writeframes(b"".join(chunks))
    return {"sampleRate": rate, "channels": channels, "sha256": sha(destination),
            "introSilenceSeconds": intro, "outroSilenceSeconds": outro}


def prepare_capture_audio(capture: dict, ranges, full_mix: Path, destination: Path):
    """Keep absolute capture timestamps until the one EDL-to-output mapping.

    The first PNG normally occurs at 1/30 second, not zero. Subtracting that
    origin in the mixer and then slicing absolute EDL times shifts audio twice.
    """
    frames = capture["frameTimes"]
    interval = frames[1]["videoSeconds"] - frames[0]["videoSeconds"]
    source_end = frames[-1]["videoSeconds"] + interval
    native = mix_audio(capture.get("audioEvents", []), capture.get("musicEvents", []),
                       full_mix, source_end, 0, source_end, intro=0)
    native["timelineOriginSeconds"] = 0
    native["timelineEndSeconds"] = source_end
    delivery = concat_audio(full_mix, ranges, destination, intro=5, outro=7)
    return native, delivery


def concat_png_entry(path: Path, duration: float | None = None):
    """Give the PNG demuxer a 30 Hz clock before concat quantizes timestamps."""
    entry = f"file '{quote_concat(path)}'\noption framerate {FPS}\n"
    if duration is not None:
        entry += f"duration {duration:.9f}\n"
    return entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-version", default="2.1")
    parser.add_argument("--editorial-version", default="2.3")
    parser.add_argument("--ffmpeg", type=Path, default=DEFAULT_FFMPEG)
    parser.add_argument("--font-dir", type=Path, default=Path("C:/Windows/Fonts"))
    parser.add_argument("--edl", type=Path, help="optional JSON list of {label,start,end,reason}")
    parser.add_argument("--preview", action="store_true", help="allow sparse source only; never a final delivery")
    parser.add_argument("--prepare-only", action="store_true")
    quality = parser.add_mutually_exclusive_group()
    quality.add_argument("--crf", type=int, choices=(18, 20, 22), default=20)
    quality.add_argument("--target-mb", type=float,
                         help="approximate decimal MB using two native-frame passes (e.g. 50); excludes --crf")
    args = parser.parse_args()
    capture_dir, out = args.capture.resolve(), args.output.resolve()
    require(capture_dir != out and capture_dir not in out.parents,
            "output must be outside preserved capture directory")
    out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    report = {
        "schemaVersion": 2, "passed": False, "sourcePresentationVersion": args.source_version,
        "editorialPresentationVersion": args.editorial_version, "preview": args.preview,
        "humanPlaytest": False, "fixtureDamage": False, "sourceFramesUnmodified": True,
        "nativeGameplayUncropped": False, "sourceFramesContinuousWithinSegments": True,
        "script": {"path": str(Path(__file__).resolve()), "sha256": sha(Path(__file__))},
        "mixerScript": {"path": str(Path(v21_audio.__file__).resolve()), "sha256": sha(Path(v21_audio.__file__))},
    }
    try:
        capture_path = capture_dir / "capture.json"
        capture = strict_json(capture_path)
        interval = validate_source(capture, args.source_version, args.preview)
        provenance_path = capture_dir / "provenance.json"
        provenance = strict_json(provenance_path)
        require(provenance.get("passed") is True, "capture wrapper did not pass")
        reports = list((capture_dir / "system").glob("portfolio-*.json"))
        traces = list((capture_dir / "system").glob("portfolio-*.jsonl"))
        require(len(reports) == len(traces) == 1, "require one exact system ledger and event stream")
        ledger = strict_json(reports[0])
        validate_ledger(ledger, capture)
        editorial_evidence = validate_editorial_capture(capture, provenance, ledger,
                                                        args.editorial_version)
        validate_system_capture(capture_dir, capture, (capture_dir / "engine.log").read_text(encoding="utf-8-sig"))
        frame_dir = capture_dir / "frames"
        require(sorted(f.name for f in frame_dir.glob("*.png")) ==
                [row["file"] for row in capture["frameTimes"]], "extra or missing native PNG frame")
        native_size = None
        native_manifest = []
        for row in capture["frameTimes"]:
            frame = frame_dir / row["file"]
            with Image.open(frame) as image:
                image.verify()
                size = image.size
            if native_size is None:
                native_size = size
            require(size == native_size and size == (1920, 1080), "source frames must be 1920x1080")
            native_manifest.append({"file": row["file"], "sha256": sha(frame), "bytes": frame.stat().st_size,
                                   "videoSeconds": row["videoSeconds"], "worldSeconds": row["worldSeconds"]})
        (out / "native-frame-manifest.json").write_text(json.dumps(native_manifest, indent=2), encoding="utf-8")
        (out / "audio-events.json").write_text(json.dumps(capture.get("audioEvents", []), ensure_ascii=False, indent=2), encoding="utf-8")
        (out / "music-events.json").write_text(json.dumps(capture.get("musicEvents", []), ensure_ascii=False, indent=2), encoding="utf-8")
        edl = DEFAULT_EDL
        if args.edl:
            raw_edl = strict_json(args.edl)
            edl = [(r["label"], float(r["start"]), float(r["end"]), r.get("reason", "custom EDL")) for r in raw_edl]
        source_end = capture["frameTimes"][-1]["videoSeconds"] + interval
        require(edl, "invalid EDL")
        validate_edl(edl, source_end)
        ranges = []
        segments = []
        frame_lines = []
        background_lines = []
        art = Art(args.font_dir)
        for index, (label, start, end, reason) in enumerate(edl):
            first, last = frame_indices(capture["frameTimes"], start, end)
            # Use actual frame boundaries; this keeps source frame and output audio clocks identical.
            actual_start = capture["frameTimes"][first]["videoSeconds"]
            actual_end = capture["frameTimes"][last - 1]["videoSeconds"] + interval
            selected = native_manifest[first:last]
            segment_hash = hashlib.sha256("".join(x["sha256"] for x in selected).encode()).hexdigest()
            key_title, subtitle = short_copy(capture, actual_start, label)
            background = out / f"chapter-{index:02d}.png"
            art.gameplay(key_title, subtitle, background)
            for item in selected:
                frame_lines.append(concat_png_entry(frame_dir / item['file'], interval))
            background_lines.append(concat_png_entry(background, (last-first)*interval))
            segments.append({
                "index": index, "label": label, "reason": reason,
                "sourceFrameStart": first, "sourceFrameEndExclusive": last,
                "sourceFrameEndInclusive": last - 1,
                "sourceFrameCount": last - first, "sourceVideoStart": actual_start,
                "sourceVideoEnd": actual_end, "outputGameplayStart": 5 + sum((s["sourceFrameCount"] * interval) for s in segments),
                "outputGameplayEnd": 5 + sum((s["sourceFrameCount"] * interval) for s in segments) + (last-first)*interval,
                "sourceFrameManifestSha256": segment_hash,
                "sourceFirstFrameSha256": selected[0]["sha256"], "sourceLastFrameSha256": selected[-1]["sha256"],
                "chapterTitle": key_title, "chapterSubtitle": subtitle,
            })
            ranges.append((actual_start, actual_end))
        frame_lines.append(concat_png_entry(frame_dir / selected[-1]['file']))
        background_lines.append(concat_png_entry(background))
        (out / "selected-frames.txt").write_text("".join(frame_lines), encoding="utf-8")
        (out / "backgrounds.txt").write_text("".join(background_lines), encoding="utf-8")
        (out / "edit-list.json").write_text(json.dumps(segments, ensure_ascii=False, indent=2), encoding="utf-8")
        full_mix = out / "native-event-mix.wav"
        final_audio = out / "event-mix-v23.wav"
        audio_meta, final_audio_meta = prepare_capture_audio(capture, ranges, full_mix, final_audio)
        art.intro(out / "intro.png", gameplay_v23=capture.get("gameplayVersion") == "2.3")
        art.outro(ledger, out / "outro.png")
        source_inputs = [capture_path, provenance_path, reports[0], traces[0], capture_dir / "engine.log"]
        gameplay_frames = sum(s["sourceFrameCount"] for s in segments)
        gameplay_seconds = gameplay_frames * interval
        expected = 5 + gameplay_seconds + 7
        plan = encoding_plan(expected, args.crf, args.target_mb)
        report.update({
            "capturePath": str(capture_dir),
            "inputs": {str(path): {"sha256": sha(path), "bytes": path.stat().st_size} for path in source_inputs},
            "sourceBinary": provenance.get("binary"), "knownEngineWarnings": provenance.get("knownEngineWarnings", []),
            "nativeFrameManifestSha256": sha(out / "native-frame-manifest.json"),
            "selectedFrameCount": gameplay_frames, "selectedGameplaySeconds": gameplay_seconds,
            "nativeFrameRate": 1 / interval, "nativeSize": native_size,
            "edl": segments, "excludedSourceIntervals": excluded_intervals(edl, source_end),
            "fullRunLedger": ledger, "outcome": ledger.get("outcome"),
            "nativeEvidence": editorial_evidence,
            "audio": {"nativeMix": audio_meta, "deliveryMix": final_audio_meta,
                      "audioEventsSha256": sha(out / "audio-events.json"),
                      "musicEventsSha256": sha(out / "music-events.json")},
            "layout": {"output": [1920, 1080], "gameplay": [28, 110, 1580, 889], "rail": [1638, 110, 250, 898]},
            "expectedDurationSeconds": expected, "prepared": True,
            "encoding": plan,
            "provenanceStatement": (
                "Source is one validated scripted native capture of v2.3 gameplay and bilingual UI; survey keys, relay boosts and actual supply healing are bound to its native ledger. This is not a human playtest claim."
                if capture.get("gameplayVersion") == "2.3" else
                "Source is one validated scripted native capture; v2.3 denotes the visual, language and delivery update, not a human playtest claim."),
        })
        if args.prepare_only:
            report["encodePending"] = True
        else:
            require(args.ffmpeg.is_file(), "ffmpeg unavailable")
            video = out / ("Aegis-Arena-v2.3-PREVIEW.mp4" if args.preview else "Aegis-Arena-v2.3-Demo.mp4")
            selected_frames = gameplay_frames
            # 0=selected source frames, 1=per-segment backgrounds, 2=intro, 3=outro, 4=delivery audio.
            filters = ";".join([
                f"[0:v]fps=30,scale=1580:889:flags=lanczos,setsar=1,setpts=PTS-STARTPTS,trim=end_frame={selected_frames}[native]",
                f"[1:v]fps=30,setsar=1,setpts=PTS-STARTPTS,trim=end_frame={selected_frames}[bg]",
                "[bg][native]overlay=28:110:eof_action=repeat,trim=end_frame=" + str(selected_frames) + ",setpts=PTS-STARTPTS[game]",
                "[2:v]fps=30,trim=duration=5,setpts=PTS-STARTPTS[intro]",
                "[3:v]fps=30,trim=duration=7,setpts=PTS-STARTPTS[outro]",
                "[intro][game][outro]concat=n=3:v=1:a=0,format=yuv420p[v]",
            ])
            commands = encoding_commands(args.ffmpeg, out, final_audio, video, expected, filters, plan)
            report["ffmpeg"] = {"path": str(args.ffmpeg), "sha256": sha(args.ffmpeg),
                                "command": [str(x) for x in commands[-1][1]],
                                "passes": [{"log": name, "command": [str(x) for x in command]}
                                           for name, command in commands]}
            for entry, (name, command) in zip(report["ffmpeg"]["passes"], commands):
                run(command, out / name)
                entry["logSha256"] = sha(out / name)
                entry["completed"] = True
            require(video.stat().st_size < 300_000_000, "video exceeds 300 MB delivery limit")
            if args.target_mb is not None:
                plan["actualBytes"] = video.stat().st_size
                plan["actualToTargetRatio"] = video.stat().st_size / plan["targetBytes"]
                plan["withinFivePercent"] = .95 <= plan["actualToTargetRatio"] <= 1.05
                require(plan["actualToTargetRatio"] <= 1.05,
                        "video exceeds target by more than 5%; see encoding provenance")
            info = subprocess.run([str(args.ffmpeg), "-hide_banner", "-i", str(video)], capture_output=True, text=True)
            (out / "stream-info.txt").write_text(info.stderr, encoding="utf-8")
            require(re.search(r"Video: h264.*yuv420p.*1920x1080.*30 fps", info.stderr), "unexpected encoded video format")
            require(re.search(r"Audio: aac.*48000 Hz.*stereo", info.stderr), "unexpected encoded audio format")
            duration_match = re.search(r"Duration: (\d+):(\d+):(\d+\.\d+)", info.stderr)
            require(duration_match is not None, "missing muxed duration")
            hours, minutes, seconds = duration_match.groups()
            encoded_duration = int(hours) * 3600 + int(minutes) * 60 + float(seconds)
            require(abs(encoded_duration - expected) < .2, "encoded duration does not match EDL")
            decoded = subprocess.run([str(args.ffmpeg), "-hide_banner", "-v", "error", "-i", str(video),
                                      "-map", "0:v:0", "-map", "0:a:0", "-f", "null", "-"],
                                     capture_output=True, text=True, timeout=1200)
            (out / "full-decode.log").write_text(decoded.stdout + decoded.stderr, encoding="utf-8")
            require(decoded.returncode == 0 and not decoded.stderr.strip(), "full video/audio decode failed")
            report.update(passed=True, encodedDurationSeconds=encoded_duration,
                          video={"file": video.name, "bytes": video.stat().st_size, "sha256": sha(video),
                                 "codec": "H.264", "audioCodec": "AAC", "pixelFormat": "yuv420p",
                                 "fps": 30, "encodingMode": plan["mode"],
                                 **({"crf": args.crf} if args.target_mb is None else
                                    {"targetMB": args.target_mb, "videoBitrate": plan["videoBitrate"]})},
                          fullDecodePassed=True)
    except Exception as exc:
        report["failure"] = str(exc)
    finally:
        report["wallSeconds"] = time.monotonic() - started
        (out / "video-provenance.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("passed", "prepared", "encodePending", "failure", "video", "wallSeconds") if k in report}, ensure_ascii=False, indent=2))
    return 0 if report.get("passed") or (args.prepare_only and report.get("prepared")) else 2


if __name__ == "__main__":
    raise SystemExit(main())

