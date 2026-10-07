"""Create a concise v2.2投递精剪版 from the verified v2.1 Aegis Arena film.

The source film is a real 30 fps native capture with engine-event audio. This
script only keeps time-ordered sections; it does not invent gameplay frames or
claim a new ruleset. The EDL records the source ranges and the output hash.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import hashlib


SEGMENTS = [
    ("intro", 0.0, 5.0, "作品集导览"),
    ("briefing", 5.0, 9.0, "中继目标与撤离条件"),
    ("stage1", 9.0, 21.2, "占领：射击、蓄能与超频"),
    ("upgrade1", 21.2, 29.733333, "升级：把资源换成打法"),
    ("stage2", 29.733333, 42.6, "转移：目标推进与队友协作"),
    ("upgrade2", 53.433333, 62.0, "二次升级：保留前置选择"),
    ("stage3-start", 62.0, 75.0, "撤离阶段：清敌后才能离场"),
    ("stage3-combat", 88.0, 99.0, "战斗节选：预警、命中与击破"),
    ("extraction-result", 113.0, 126.866667, "撤离与结算"),
    ("outro", 128.866667, 133.866667, "投递信息"),
]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--ffmpeg", type=Path, required=True)
    p.add_argument("--edl", type=Path, required=True)
    args = p.parse_args()
    src = args.input.resolve()
    out = args.output.resolve()
    edl_path = args.edl.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    edl_path.parent.mkdir(parents=True, exist_ok=True)
    if not src.is_file() or not args.ffmpeg.is_file():
        raise SystemExit("input or ffmpeg is missing")

    filters = []
    concat_inputs = []
    for i, (_, start, end, _) in enumerate(SEGMENTS):
        filters.append(f"[0:v]trim=start={start}:end={end},setpts=PTS-STARTPTS[v{i}]")
        filters.append(f"[0:a]atrim=start={start}:end={end},asetpts=PTS-STARTPTS[a{i}]")
        concat_inputs.extend([f"[v{i}]", f"[a{i}]"])
    filters.append("".join(concat_inputs) + f"concat=n={len(SEGMENTS)}:v=1:a=1[v][a]")
    cmd = [
        str(args.ffmpeg), "-hide_banner", "-y", "-nostdin", "-threads", "2",
        "-filter_complex_threads", "2", "-i", str(src),
        "-filter_complex", ";".join(filters), "-map", "[v]", "-map", "[a]",
        "-c:v", "libx264", "-preset", "medium", "-crf", "20",
        "-pix_fmt", "yuv420p", "-r", "30", "-fps_mode", "cfr",
        "-c:a", "aac", "-b:a", "160k", "-ar", "48000",
        "-movflags", "+faststart", str(out),
    ]
    log = out.with_suffix(".encode.log")
    result = subprocess.run(cmd, stdout=log.open("w", encoding="utf-8"), stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit(f"ffmpeg failed; see {log}")
    if not out.is_file() or out.stat().st_size < 100_000:
        raise SystemExit("encoded output is missing or unexpectedly small")
    duration = sum(end - start for _, start, end, _ in SEGMENTS)
    record = {
        "schemaVersion": 1,
        "editorialVersion": "2.2",
        "sourcePresentation": "Aegis Arena PRISM FALL v2.1 verified native capture film",
        "source": {"path": str(src), "bytes": src.stat().st_size, "sha256": sha256(src)},
        "output": {"path": str(out), "bytes": out.stat().st_size, "sha256": sha256(out),
                   "expectedSeconds": duration, "fps": 30, "resolution": [1920, 1080]},
        "disclosure": "Real recorded gameplay sections kept in chronological order; diagnostics and long traversal removed. This is an editorial v2.2 submission cut, not a new ruleset or human playtest result.",
        "segments": [
            {"id": key, "sourceStart": start, "sourceEnd": end,
             "outputStart": sum(SEGMENTS[j][2]-SEGMENTS[j][1] for j in range(i)),
             "outputEnd": sum(SEGMENTS[j][2]-SEGMENTS[j][1] for j in range(i+1)),
             "reason": reason}
            for i, (key, start, end, reason) in enumerate(SEGMENTS)
        ],
    }
    edl_path.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(record["output"], ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
