"""Build/run only the standalone Portfolio economy ledger; never starts Unreal."""
from __future__ import annotations
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--compiler")
    args = parser.parse_args()
    compiler = args.compiler or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        bundled = sorted((ROOT / ".tools").glob("zig-*/zig.exe"))
        compiler = str(bundled[-1]) if bundled else None
    if not compiler:
        parser.error("C++ compiler unavailable; no downloads performed")
    args.output.mkdir(parents=True, exist_ok=True)
    prefix = [compiler] + (["c++"] if pathlib.Path(compiler).stem == "zig" else [])
    executable = args.output / ("portfolio_tests.exe" if sys.platform == "win32" else "portfolio_tests")
    flags = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    compiled = subprocess.run(prefix + flags + ["-Icore/include", "core/tests/portfolio_tests.cpp", "-o", str(executable)],
                              cwd=ROOT, text=True, capture_output=True)
    (args.output / "compile.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
    if compiled.returncode:
        print(compiled.stderr, file=sys.stderr)
        return compiled.returncode
    result = subprocess.run([str(executable)], text=True, capture_output=True)
    (args.output / "tests.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    report = {"engine": "portable-cpp-ledger", "unrealCompiled": False, "unrealRuntime": False,
              "compiler": compiler, "flags": flags, "exitCode": result.returncode,
              "sources": {str(p): hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in
                          (pathlib.Path("core/include/aegis/portfolio.hpp"), pathlib.Path("core/tests/portfolio_tests.cpp"))},
              "tests": json.loads(result.stdout)}
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
