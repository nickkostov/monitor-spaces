"""Package an explicit allowlist; never include local tooling or credentials."""

import hashlib
import json
from pathlib import Path
import zipfile

from inspect_pe import inspect


ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "MonitorSpaces.exe"
DIST = ROOT / "dist"
NAME = "monitor-spaces-windows-x64"


def main():
    report = inspect(EXE)
    DIST.mkdir(exist_ok=True)
    artifact = DIST / f"{NAME}.zip"
    sources = [ROOT / name for name in ("Makefile", "README.md", "VERIFICATION.md", ".gitignore")]
    for directory in ("src", "tests", "resources", "tools"):
        sources.extend(path for path in (ROOT / directory).rglob("*")
                       if path.is_file() and path.suffix in {".c", ".h", ".py", ".rc", ".manifest"})
    # Fixed timestamps and sorted files make identical inputs produce one ZIP.
    with zipfile.ZipFile(artifact, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        def write(name, data):
            info = zipfile.ZipInfo(f"{NAME}/{name}", date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)

        write("MonitorSpaces.exe", EXE.read_bytes())
        write("BUILD-INFO.json", (json.dumps(report, indent=2) + "\n").encode())
        for source in sorted(sources):
            write(source.relative_to(ROOT).as_posix(), source.read_bytes())

    with zipfile.ZipFile(artifact) as archive:
        if archive.testzip() is not None:
            raise RuntimeError("ZIP integrity check failed")
        if archive.read(f"{NAME}/MonitorSpaces.exe") != EXE.read_bytes():
            raise RuntimeError("Packaged executable does not match inspected executable")
    digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
    checksum = DIST / f"{NAME}.zip.sha256"
    checksum.write_text(f"{digest}  {artifact.name}\n", encoding="ascii")
    print(f"Packaged {artifact.relative_to(ROOT)} ({artifact.stat().st_size:,} bytes)")
    print(f"SHA-256: {digest}")


if __name__ == "__main__":
    main()
