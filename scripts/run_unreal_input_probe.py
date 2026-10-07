"""Run rendered, synthetic in-engine input checks and retain untouched native PNGs.

This probe does not perform OS-level input or establish human usability. Every
run gets a new output directory; raw engine JSON, log and images are preserved.
"""
import argparse
from collections import Counter
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import struct
import subprocess
import time
import zlib

from run_unreal_functional import snapshot_inputs
from run_unreal_trial import OFFLINE_ARGUMENT

ROOT = Path(__file__).resolve().parents[1]
MAP = "/Game/Aegis/Maps/AegisArena"
SCREENSHOTS = ("briefing.png", "active.png", "paused.png", "newattempt.png")
DIRECT_EVENT = re.compile(r"^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_INPUT_[^\r\n]+)$")
ASSERTIONS = (
    "game_world_begun", "briefing_initial_population", "briefing_blocks_movement",
    "briefing_blocks_fire", "briefing_blocks_dash", "enter_deploys_guided_wave",
    "w_moves_screen_forward", "space_starts_dash_cooldown", "dash_moves_character",
    "dash_cooldown_blocks_repeat", "dash_restores_walk_speed", "ground_point_projects_to_viewport",
    "cursor_deprojects_to_ground_xy", "cursor_sets_character_heading", "held_lmb_repeats_after_cooldown",
    "lmb_release_stops_repeat", "p_enters_pause", "pause_freezes_game_clock",
    "pause_blocks_movement_and_fire", "p_resumes_game_clock", "restart_fixture_has_held_inputs",
    "r_creates_fresh_briefing_pawn", "restart_resets_cooldowns_and_shots", "restart_flushes_held_inputs",
    "restart_retires_old_population", "four_rendered_screenshots_saved",
    "briefing_has_clickable_quit", "escape_opens_exit_menu", "menu_has_resume_restart_quit",
    "quit_button_requests_normal_engine_exit",
)


def inspect_png(path, expected_size=(1280, 720)):
    """Check native PNG structure, CRCs, dimensions and decoded scanline size.

    Validation reads bytes only; it neither edits nor resaves the screenshot.
    Visual content still requires a separate inspection of the original file.
    """
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Invalid PNG signature: {Path(path).name}")
    offset, header, compressed, ended = 8, None, bytearray(), False
    while offset < len(data):
        if len(data) - offset < 12:
            raise ValueError("Truncated PNG chunk")
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        end = offset + 12 + length
        if end > len(data):
            raise ValueError("PNG chunk exceeds file length")
        body = data[offset + 8:offset + 8 + length]
        crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        if zlib.crc32(kind + body) & 0xffffffff != crc:
            raise ValueError("PNG chunk CRC mismatch")
        if header is None and kind != b"IHDR":
            raise ValueError("PNG must begin with IHDR")
        if kind == b"IHDR":
            if header is not None or length != 13:
                raise ValueError("Invalid or repeated PNG header")
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed.extend(body)
        elif kind == b"IEND":
            if length != 0 or end != len(data):
                raise ValueError("Invalid PNG end or trailing bytes")
            ended = True
            break
        offset = end
    if not header or not compressed or not ended:
        raise ValueError("PNG image data or end chunk is missing")
    width, height, bits, color, compression, filtering, interlace = header
    if (width, height) != expected_size:
        raise ValueError(f"Expected PNG {expected_size}, got {(width, height)}")
    if bits != 8 or color not in (2, 6) or (compression, filtering, interlace) != (0, 0, 0):
        raise ValueError("Expected noninterlaced 8-bit RGB or RGBA native screenshot")
    stride = 1 + width * (3 if color == 2 else 4)
    expected_bytes = stride * height
    decoder = zlib.decompressobj()
    decoded = decoder.decompress(compressed, expected_bytes + 1)
    if (len(decoded) != expected_bytes or not decoder.eof or decoder.unused_data or
            decoder.unconsumed_tail or any(decoded[row * stride] > 4 for row in range(height))):
        raise ValueError("PNG scanline data is incomplete or invalid")
    return {"file": Path(path).name, "bytes": len(data), "width": width, "height": height,
            "sha256": hashlib.sha256(data).hexdigest(), "structureVerified": True}


def validate(report, log, output):
    if not isinstance(report, dict):
        raise ValueError("Input probe report must be an object")
    required = {"schemaVersion": 1, "engine": "unreal-runtime", "syntheticInput": True,
                "renderOffscreen": True, "humanUsabilityTest": False, "passed": True,
                "fixtureDamage": False, "worldType": 1, "begunPlay": True, "renderingEnabled": True}
    if any(type(report.get(key)) is not type(value) or report[key] != value
           for key, value in required.items()):
        raise ValueError("Unexpected input probe schema, scope or outcome")
    seconds = report.get("wallSeconds")
    if (type(seconds) not in (int, float) or not math.isfinite(seconds) or
            seconds < 0 or seconds > 30):
        raise ValueError("Probe did not complete within its 30-second actor deadline")
    for key in ("screenshotRequests", "screenshotProcessed"):
        if type(report.get(key)) is not int or report[key] != len(SCREENSHOTS):
            raise ValueError("All four screenshot requests must finish")
    assertions = report.get("assertions")
    if (not isinstance(assertions, list) or len(assertions) != len(ASSERTIONS) or
            any(not isinstance(item, dict) for item in assertions)):
        raise ValueError("Missing input assertions")
    if (Counter(item.get("name") for item in assertions) != Counter(ASSERTIONS) or
            any(item.get("passed") is not True for item in assertions)):
        raise ValueError("Input assertion labels or outcomes differ from the required set")
    events = [match.group(1).strip() for line in log.splitlines()
              if (match := DIRECT_EVENT.fullmatch(line))]
    completions = [event for event in events
                   if event.startswith(("AEGIS_INPUT_PROBE_PASS", "AEGIS_INPUT_PROBE_FAIL"))]
    if len(completions) != 1:
        raise ValueError("Missing unique original input probe success marker")
    completion = re.fullmatch(
        rf"AEGIS_INPUT_PROBE_PASS checks={len(ASSERTIONS)} screenshots=4 wall=([0-9]+(?:\.[0-9]+)?) reason=(.+)",
        completions[0])
    if (not completion or abs(float(completion.group(1)) - seconds) > 0.001 or
            completion.group(2) != report.get("reason")):
        raise ValueError("Input completion log counts, duration or reason differ from the result")
    actual = Counter()
    for event in events:
        if event.startswith("AEGIS_INPUT_ASSERT "):
            match = re.fullmatch(r"AEGIS_INPUT_ASSERT (.*?) \| pass=1(?: \| .*)?", event)
            if not match:
                raise ValueError("Input assertion log includes a failure or malformed outcome")
            actual[match.group(1)] += 1
    if actual != Counter(ASSERTIONS):
        raise ValueError("Log does not prove every required input check executed exactly once")
    if any(marker in log for marker in ("Handled ensure", "Ensure condition failed", "Fatal error:",
                                        "Assertion failed", "AEGIS_INPUT_PROBE_FAIL")):
        raise ValueError("Rendered input probe log contains a failure or ensure")
    output = Path(output).resolve()
    paths = report.get("screenshots")
    if not isinstance(paths, list) or len(paths) != len(SCREENSHOTS):
        raise ValueError("Expected exactly four native screenshot paths")
    expected = {output / name for name in SCREENSHOTS}
    if (any(not isinstance(path, str) or not Path(path).is_absolute() for path in paths) or
            {Path(path).resolve() for path in paths} != expected):
        raise ValueError("Screenshot paths differ from the exact files in this run's output")
    return [inspect_png(output / name) for name in SCREENSHOTS]


def build_command(engine_root, output, packaged_exe=None):
    """Select one launch target; packaged runs never receive an editor/project argument."""
    engine_root, output = Path(engine_root).resolve(), Path(output).resolve()
    if packaged_exe is not None:
        prefix = [str(Path(packaged_exe).resolve()), MAP]
    else:
        editor = engine_root / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
        prefix = [str(editor), str(ROOT / "AegisArena.uproject"), MAP, "-game"]
    return prefix + [
        "-RenderOffscreen", "-AegisInputProbe", "-AegisQuit", f"-AegisProbeOutput={output}",
        "-ResX=1280", "-ResY=720", "-windowed", "-unattended", "-nop4", OFFLINE_ARGUMENT,
        "-stdout", "-FullStdOutLogOutput", f"-abslog={output / 'engine.log'}",
    ]


def hash_executable(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache-root", type=Path)
    parser.add_argument("--packaged-exe", type=Path,
                        help="Standalone Development game executable; engine-root supplies build metadata only")
    args = parser.parse_args()
    engine_root = args.engine_root.resolve()
    executable = (args.packaged_exe.resolve() if args.packaged_exe else
                  engine_root / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
    if not executable.is_file():
        parser.error("Packaged Development executable is missing" if args.packaged_exe else
                     "UnrealEditor-Cmd.exe is missing")
    if args.packaged_exe and executable.name.casefold().startswith("unrealeditor"):
        parser.error("--packaged-exe must name the standalone game, not an Unreal Editor executable")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = build_command(engine_root, output, args.packaged_exe)
    environment = os.environ.copy()
    if args.cache_root:
        cache = args.cache_root.resolve()
        (cache / "Temp").mkdir(parents=True, exist_ok=True)
        environment.update(TEMP=str(cache / "Temp"), TMP=str(cache / "Temp"))
        environment["UE-LocalDataCachePath"] = str(cache / "DerivedDataCache")
    provenance = {
        "engine": "unreal-runtime",
        "launch": "packaged-development" if args.packaged_exe else "UnrealEditor-Cmd -game",
        "unrealEditorLaunched": args.packaged_exe is None, "world": "Game",
        "executable": str(executable),
        "engineRootRole": "build-metadata-only" if args.packaged_exe else "editor-runtime-and-build-metadata",
        "rendering": "RenderOffscreen", "resolution": [1280, 720], "command": command,
        "startedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": platform.platform(), "passed": False,
        "syntheticInEngineInput": True, "humanUsabilityTest": False, "osInputUsed": False,
        "fixtureDamage": False,
        "scope": "Synthetic PlayerController input, virtual cursor, original rendered screenshots",
        "expectedAssertions": list(ASSERTIONS), "editorHomeScreenOverride": OFFLINE_ARGUMENT,
        "sourceDigestScope": "Source, core/include, Config, AegisArena.uproject; sorted relative path + bytes",
        "contentDigestScope": "Content/Aegis; sorted relative path + bytes",
        "gateSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    started = time.monotonic()
    try:
        executable_hash = hash_executable(executable)
        provenance.update(executableSha256=executable_hash, executableBytes=executable.stat().st_size)
        provenance["engineBuild"] = json.loads((engine_root / "Engine/Build/Build.version").read_text())
        provenance["gitHead"] = subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
        provenance["gitStatus"] = subprocess.check_output(
            ["git", "-C", str(ROOT), "status", "--porcelain"], text=True).splitlines()
        inputs = snapshot_inputs()
        provenance.update(inputs)
        with (output / "stdout.log").open("w", encoding="utf-8") as target:
            result = subprocess.run(command, stdout=target, stderr=subprocess.STDOUT,
                                    env=environment, timeout=120, check=False,
                                    cwd=executable.parent if args.packaged_exe else None)
        provenance["exitCode"] = result.returncode
        if result.returncode:
            raise RuntimeError(f"Unreal input probe exited {result.returncode}")
        report = json.loads((output / "input-probe.json").read_text(encoding="utf-8-sig"))
        log = (output / "engine.log").read_text(encoding="utf-8-sig", errors="replace")
        pngs = validate(report, log, output)
        if snapshot_inputs() != inputs:
            raise RuntimeError("Source or content changed during the rendered input probe")
        if hash_executable(executable) != executable_hash:
            raise RuntimeError("Launched executable changed during the rendered input probe")
        provenance.update(passed=True, nativeAssertions=len(ASSERTIONS), screenshots=pngs)
        print(f"Rendered input probe PASS: {len(ASSERTIONS)} checks and four native PNGs; {output}")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, zlib.error) as error:
        provenance["failure"] = str(error)
        raise
    finally:
        provenance["wallSeconds"] = time.monotonic() - started
        for name in ("input-probe.json", "engine.log", "stdout.log") + SCREENSHOTS:
            path = output / name
            if path.is_file():
                provenance[name + "Sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
