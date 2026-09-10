"""Build portable C++ only. Does not compile or certify the Unreal module."""
from __future__ import annotations
import argparse
import json
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", help="g++, clang++, or zig executable; defaults to available compiler")
    args = parser.parse_args()
    compiler = args.compiler or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        candidates = sorted((ROOT / ".tools").glob("zig-*/zig.exe"))
        compiler = str(candidates[-1]) if candidates else None
    if not compiler:
        parser.error("No C++ compiler. Install a compiler or pass --compiler; no tools are downloaded automatically.")
    prefix = [compiler] + (["c++"] if pathlib.Path(compiler).stem == "zig" else [])
    output = ROOT / "build"
    output.mkdir(exist_ok=True)
    ext = ".exe" if sys.platform == "win32" else ""
    for target, source in (("aegis_tests", "core/tests/tests.cpp"), ("aegis_sim", "core/src/main.cpp")):
        subprocess.run(prefix + ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic", "-Icore/include", source, "-o", str(output / (target + ext))], cwd=ROOT, check=True)
    result = subprocess.run([str(output / ("aegis_tests" + ext))], check=True, capture_output=True, text=True)
    version = subprocess.run(prefix + ["--version"], check=True, capture_output=True, text=True).stdout.splitlines()[0]
    report = {"engine": "portable-cpp-model", "compiler": version, "flags": "C++17 -O2 -Wall -Wextra -Werror -pedantic", "tests": json.loads(result.stdout), "unreal_compiled": False}
    (output / "build-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
