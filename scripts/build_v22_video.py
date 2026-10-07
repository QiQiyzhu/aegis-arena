"""Build the compact v2.2 editorial cut from a verified native capture.

This is an editorial delivery script.  It does not alter the game or v2.1
pipeline: the source frames and issued audio events are read from a preserved
capture, selected in chronological EDL order, and written with provenance.
The default EDL is the agreed v2.2 cut (about 84 seconds of source gameplay,
plus five seconds of title and seven seconds of result card, about 96 seconds
total).  ``--source-version`` makes the distinction explicit when a v2.1
native capture is used to produce the v2.2 editorial delivery.
"""
from __future__ import annotations

import argparse
from bisect import bisect_left, bisect_right
import hashlib
import json
import math
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

# Source seconds are deliberately kept as a visible, reviewable EDL.  The
# diagnostics interval 37.60--48.43 and two long traversal intervals are
# excluded; no source frame is generated, re-ordered, or speed changed.
DEFAULT_EDL = [
    ("briefing_and_stage1", 0.00, 16.20, "开场读目标，进入第一段战斗"),
    ("upgrade_1", 16.20, 24.73, "第一次构筑选择"),
    ("stage2_charge", 24.73, 37.60, "转移与蓄能窗口"),
    ("upgrade_2", 48.43, 57.00, "第二次构筑确认"),
    ("stage3_open", 57.00, 70.00, "撤离段开局压力"),
    ("stage3_mid", 83.00, 94.00, "路线与火力交换"),
    ("stage3_finish", 108.00, 121.87, "清敌后进入结算"),
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


def frame_indices(rows, start: float, end: float):
    times = [r["videoSeconds"] for r in rows]
    first = bisect_left(times, start)
    last = bisect_left(times, end)
    require(last > first, f"EDL range has no native frames: {start}--{end}")
    return first, last


def sample_for(capture: dict, second: float):
    samples = capture["samples"]
    times = [s["videoSeconds"] for s in samples]
    return samples[max(0, bisect_right(times, second) - 1)]


def short_copy(capture: dict, second: float, label: str):
    editorial_copy = {
        "briefing_and_stage1": ("目标与占领", "先读目标，再进入战斗\n清敌后推进据点"),
        "upgrade_1": ("构筑选择", "阶段间调整打法\n为下一轮战斗做准备"),
        "stage2_charge": ("02 / 转移", "蓄力寻找开火时机\n超频加速据点充能"),
        "upgrade_2": ("构筑选择", "比较强化效果\n围绕当前打法取舍"),
        "stage3_open": ("03 / 撤离", "优先处理场上威胁\n为撤离创造窗口"),
        "stage3_route": ("路线与协作", "沿地图动线转移\n让队友保持在有效位置"),
        "stage3_mid": ("走位与协作", "利用掩体转移火线\n与同伴协同输出"),
        "stage3_recover": ("资源与恢复", "在输出与修复之间取舍\n保留进入撤离区的余量"),
        "stage3_finish": ("撤离与结算", "清敌后进入撤离区\n闭合关卡体验流程"),
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
        self.text(d, (365, 45), "PRISM FALL v2.2  /  实机节选 · 战斗关卡策划", 24, MINT)
        self.text(d, (52, 1030), "Unreal Engine 实机  ·  脚本输入演示", 18, MUTED)
        self.text(d, (1738, 1030), "DEMO v2.2", 18, AMBER)
        # 1580x889 preserves 16:9 and leaves a narrow annotation rail.
        d.rectangle((28, 110, 1608, 1008), outline="#587c83", width=2)
        d.rectangle((1638, 110, 1888, 1008), fill=PANEL)
        d.rectangle((1638, 110, 1643, 1008), fill=AMETHYST)
        self.text(d, (1660, 146), title, 27, WHITE, True)
        # Two short design observations; validate their width before rendering.
        for line_index, line in enumerate(subtitle.splitlines()):
            require(d.textlength(line, font=self.font(20)) <= 212,
                    "chapter annotation exceeds the rail width")
            self.text(d, (1660, 216 + 38 * line_index), line, 20, MINT)
        d.line((1660, 300, 1868, 300), fill="#3b5b68", width=1)
        self.text(d, (1660, 334), "设计观察", 18, MUTED)
        image.save(destination)

    def intro(self, destination: Path):
        image, d = self.base()
        self.text(d, (84, 116), "战斗 / 关卡策划作品", 30, AMBER)
        self.text(d, (84, 226), "AEGIS ARENA", 94, WHITE, True)
        self.text(d, (90, 357), "PRISM FALL / DEMO v2.2", 44, MINT, True)
        self.text(d, (94, 535), "共享能量取舍  ·  三阶段推进  ·  同伴协作", 32, WHITE)
        self.text(d, (94, 938), "UNREAL ENGINE  ·  原生实机  ·  脚本输入演示", 20, MUTED)
        image.save(destination)

    def outro(self, ledger: dict, destination: Path):
        image, d = self.base()
        self.text(d, (84, 118), "关卡收束", 30, AMBER)
        self.text(d, (84, 228), "战斗决策 → 阶段升级 → 撤离结算", 56, WHITE, True)
        outcome = "完成撤离" if ledger.get("outcome") == "won" else "行动失败"
        self.text(d, (90, 346), f"{outcome}  ·  {ledger.get('stagesRewarded', 0)} / 3 阶段", 32, MINT)
        self.text(d, (90, 510), "设计重点：让共享能量在输出、恢复与目标推进之间形成取舍。", 30, WHITE)
        self.text(d, (90, 938), "AEGIS ARENA  ·  DEMO v2.2  ·  战斗 / 关卡策划作品", 20, MUTED)
        image.save(destination)


def quote_concat(path: Path) -> str:
    return str(path.resolve()).replace("\\", "/").replace("'", "'\\''")


def run(command, log: Path, timeout=2400):
    with log.open("w", encoding="utf-8") as f:
        result = subprocess.run([str(x) for x in command], stdout=f, stderr=subprocess.STDOUT,
                                timeout=timeout)
    require(result.returncode == 0, f"ffmpeg failed; see {log}")


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
        chunks.append(raw[a:b])
    chunks.append(silence_outro)
    with wave.open(str(destination), "wb") as out:
        out.setparams(params)
        out.writeframes(b"".join(chunks))
    return {"sampleRate": rate, "channels": channels, "sha256": sha(destination),
            "introSilenceSeconds": intro, "outroSilenceSeconds": outro}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-version", default="2.1")
    parser.add_argument("--editorial-version", default="2.2")
    parser.add_argument("--ffmpeg", type=Path, default=DEFAULT_FFMPEG)
    parser.add_argument("--font-dir", type=Path, default=Path("C:/Windows/Fonts"))
    parser.add_argument("--edl", type=Path, help="optional JSON list of {label,start,end,reason}")
    parser.add_argument("--preview", action="store_true", help="allow sparse source only; never a final delivery")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--crf", type=int, choices=(18, 20, 22), default=20)
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
        require(edl and all(start >= 0 and end > start for _, start, end, _ in edl), "invalid EDL")
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
                frame_lines.append(f"file '{quote_concat(frame_dir / item['file'])}'\nduration {interval:.9f}\n")
            background_lines.append(f"file '{quote_concat(background)}'\nduration {(last-first)*interval:.9f}\n")
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
        frame_lines.append(frame_lines[-1])
        background_lines.append(background_lines[-1])
        (out / "selected-frames.txt").write_text("".join(frame_lines), encoding="utf-8")
        (out / "backgrounds.txt").write_text("".join(background_lines), encoding="utf-8")
        (out / "edit-list.json").write_text(json.dumps(segments, ensure_ascii=False, indent=2), encoding="utf-8")
        native_duration = len(capture["frameTimes"]) * interval
        full_mix = out / "native-event-mix.wav"
        audio_meta = mix_audio(capture.get("audioEvents", []), capture.get("musicEvents", []), full_mix,
                               native_duration, capture["frameTimes"][0]["videoSeconds"], native_duration, intro=0)
        final_audio = out / "event-mix-v22.wav"
        final_audio_meta = concat_audio(full_mix, ranges, final_audio, intro=5, outro=7)
        art.intro(out / "intro.png")
        art.outro(ledger, out / "outro.png")
        source_inputs = [capture_path, provenance_path, reports[0], traces[0], capture_dir / "engine.log"]
        gameplay_frames = sum(s["sourceFrameCount"] for s in segments)
        gameplay_seconds = gameplay_frames * interval
        expected = 5 + gameplay_seconds + 7
        report.update({
            "capturePath": str(capture_dir),
            "inputs": {str(path): {"sha256": sha(path), "bytes": path.stat().st_size} for path in source_inputs},
            "sourceBinary": provenance.get("binary"), "knownEngineWarnings": provenance.get("knownEngineWarnings", []),
            "nativeFrameManifestSha256": sha(out / "native-frame-manifest.json"),
            "selectedFrameCount": gameplay_frames, "selectedGameplaySeconds": gameplay_seconds,
            "nativeFrameRate": 1 / interval, "nativeSize": native_size,
            "edl": segments, "excludedSourceIntervals": [[37.60, 48.43], [70.00, 83.00], [94.00, 108.00]],
            "fullRunLedger": ledger, "outcome": ledger.get("outcome"),
            "audio": {"nativeMix": audio_meta, "deliveryMix": final_audio_meta,
                      "audioEventsSha256": sha(out / "audio-events.json"),
                      "musicEventsSha256": sha(out / "music-events.json")},
            "layout": {"output": [1920, 1080], "gameplay": [28, 110, 1580, 889], "rail": [1638, 110, 250, 898]},
            "expectedDurationSeconds": expected, "prepared": True,
            "provenanceStatement": "Source is one validated scripted native capture; v2.2 denotes editorial delivery version, not a claim of a new gameplay build or human playtest.",
        })
        if args.prepare_only:
            report["encodePending"] = True
        else:
            require(args.ffmpeg.is_file(), "ffmpeg unavailable")
            video = out / ("Aegis-Arena-v2.2-PREVIEW.mp4" if args.preview else "Aegis-Arena-v2.2-Demo.mp4")
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
            command = [args.ffmpeg, "-hide_banner", "-y", "-threads", "2", "-filter_complex_threads", "2",
                       "-f", "concat", "-safe", "0", "-i", out / "selected-frames.txt",
                       "-f", "concat", "-safe", "0", "-i", out / "backgrounds.txt",
                       "-loop", "1", "-framerate", "30", "-i", out / "intro.png",
                       "-loop", "1", "-framerate", "30", "-i", out / "outro.png",
                       "-i", final_audio, "-filter_complex", filters, "-map", "[v]", "-map", "4:a:0",
                       "-c:v", "libx264", "-preset", "medium", "-crf", str(args.crf), "-pix_fmt", "yuv420p",
                       "-r", "30", "-fps_mode", "cfr", "-frames:v", str(round(expected * FPS)),
                       "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2", "-movflags", "+faststart",
                       "-shortest", video]
            report["ffmpeg"] = {"path": str(args.ffmpeg), "sha256": sha(args.ffmpeg), "command": [str(x) for x in command]}
            run(command, out / "encode.log")
            require(video.stat().st_size < 300_000_000, "video exceeds 300 MB delivery limit")
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
                                 "fps": 30, "crf": args.crf}, fullDecodePassed=True)
    except Exception as exc:
        report["failure"] = str(exc)
    finally:
        report["wallSeconds"] = time.monotonic() - started
        (out / "video-provenance.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("passed", "prepared", "encodePending", "failure", "video", "wallSeconds") if k in report}, ensure_ascii=False, indent=2))
    return 0 if report.get("passed") or (args.prepare_only and report.get("prepared")) else 2


if __name__ == "__main__":
    raise SystemExit(main())
