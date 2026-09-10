"""Archive already-verified native packages; never builds or claims UI acceptance."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import zipfile

from check_shipping import inspect

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--development", type=Path, required=True)
    parser.add_argument("--shipping", type=Path, required=True)
    parser.add_argument("--development-verification", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    packages = {"Development": args.development.resolve(), "Shipping": args.shipping.resolve()}
    binaries = {
        name: inspect(path / "AegisArena/Binaries/Win64" /
                      ("AegisArena.exe" if name == "Development" else "AegisArena-Win64-Shipping.exe"))
        for name, path in packages.items()
    }
    if not all(binaries["Development"]["markers"].values()) or any(binaries["Shipping"]["markers"].values()):
        raise ValueError("Development/Shipping marker boundary failed")
    verification = json.loads(args.development_verification.read_text(encoding="utf-8"))
    if verification.get("passed") is not True or verification.get("binarySha256") != binaries["Development"]["sha256"]:
        raise ValueError("Development verification is missing or belongs to another executable")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    policy = json.loads((ROOT / "evidence/unreal/evaluation/report.json").read_text(encoding="utf-8"))
    manifest = {
        "release": "v1.0.0-native", "createdAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "runtimeSourceSha256": policy["sourceSha256"], "contentSha256": policy["contentSha256"],
        "engine": "UE 5.8.2 CL 56702186", "binaries": binaries, "archives": [],
        "boundary": "Archive CRC/binary identity check; actual UI acceptance is separately recorded in docs/release.md",
    }
    notice = (
        "Aegis Arena is a small AI systems portfolio lab. Project source code is MIT licensed.\n"
        "This packaged build also contains Unreal Engine runtime, cooked Engine content and\n"
        "third-party dependencies, which retain their respective licenses and copyrights.\n"
        "The repository MIT license does not relicense those components.\n"
        "Unreal Engine is developed by Epic Games, Inc.\n"
    )
    for name, package in packages.items():
        if not (package / "AegisArena.exe").is_file():
            raise ValueError("Packaged launcher missing")
        archive = output / f"AegisArena-v1.0.0-{name}-Win64.zip"
        names = []
        with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as target:
            for path in sorted(package.rglob("*")):
                if not path.is_file():
                    continue
                relative = path.relative_to(package)
                if path.suffix.lower() == ".pdb" or any(part in ("Saved", "Intermediate", "DerivedDataCache") for part in relative.parts) or path.name.startswith("Manifest_"):
                    continue
                path.resolve().relative_to(package)
                entry = "Windows/" + relative.as_posix()
                target.write(path, entry)
                names.append(entry)
            target.writestr("README.md", (ROOT / "docs/release.md").read_text(encoding="utf-8"))
            target.writestr("NOTICES.txt", notice)
            target.writestr("runtime-build.json", json.dumps({"target": name, "binary": binaries[name],
                "runtimeSourceSha256": policy["sourceSha256"], "contentSha256": policy["contentSha256"]}, indent=2))
        with zipfile.ZipFile(archive) as source:
            bad_file = source.testzip()
            if bad_file:
                raise ValueError(f"Archive CRC failed: {bad_file}")
        manifest["archives"].append({"file": archive.name, "bytes": archive.stat().st_size,
                                      "sha256": digest(archive), "crcPassed": True, "runtimeFiles": names})
        print(f"Verified archive: {archive.name} ({archive.stat().st_size} bytes)")
    (output / "release-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (output / "SHA256SUMS.txt").write_text("".join(f"{row['sha256']}  {row['file']}\n" for row in manifest["archives"]), encoding="utf-8")


if __name__ == "__main__":
    main()
