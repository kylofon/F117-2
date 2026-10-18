#!/usr/bin/env python3
"""Build the release zip (F117-SDL3-v<version>-win64.zip) from a clean Release build with the launcher.

  python tools/make_release.py

Steps: a fresh configure of f117port/build-release (-DF117_LAUNCHER=ON), the build, ctest (any failure stops
the script), the staged folder release/F117-SDL3-v<version>/ and the zip beside it. The MSYS2 mingw64 tools are
put first on PATH if they are in C:\\msys64 (override with MSYS2_MINGW64_BIN).

No original game file is ever staged or zipped: the names are taken from the launcher's own list (MissingGameFile
in f117port/launcher/game.cpp) and from every file in the game folder (Game/, a junction to F-117ANi/); a file
with one of those names, or with the same size and SHA-256 as one of them, stops the script. The zip is checked
the same way after it is written.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION = "0.1.0"
NAME = f"F117-SDL3-v{VERSION}"
RELEASE = ROOT / "release"
STAGE = RELEASE / NAME
ZIP = RELEASE / f"{NAME}-win64.zip"
BUILD = ROOT / "f117port" / "build-release"
GAME_DIR = Path(os.path.realpath(ROOT / "Game"))
MSYS2_BIN = Path(os.environ.get("MSYS2_MINGW64_BIN", r"C:\msys64\mingw64\bin"))

# The game's own file names are the launcher's list, read from its source so there is one list.
LAUNCHER_GAME_CPP = ROOT / "f117port" / "launcher" / "game.cpp"


def fail(message):
    print(f"make_release: {message}", file=sys.stderr)
    sys.exit(1)


def launcher_game_names():
    text = LAUNCHER_GAME_CPP.read_text(encoding="utf-8")
    body = text[text.index("MissingGameFile(const wxString& dir)"):]
    body = body[:body.index("return wxString();")]
    names = {n.upper() for n in re.findall(r'"([A-Za-z0-9_.]+)"', body)}
    if "F117.COM" not in names or len(names) < 10:
        fail(f"could not read the launcher's game file list from {LAUNCHER_GAME_CPP}")
    return names


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def game_index():
    """(names, {size: [(name, path)]}) for every file under Game/ (read only)."""
    names, by_size = set(), {}
    if GAME_DIR.is_dir():
        for dirpath, _dirs, files in os.walk(GAME_DIR):
            for f in files:
                p = Path(dirpath) / f
                names.add(f.upper())
                by_size.setdefault(p.stat().st_size, []).append((f, p))
    return names, by_size


class GameGuard:
    def __init__(self):
        self.launcher = launcher_game_names()
        self.game_names, self.by_size = game_index()
        self.hashes = {}
        self.forbidden = self.launcher | self.game_names

    def check_file(self, path, arcname):
        """Refuse a file that is an original game file by name, or by size and content."""
        if arcname.upper().split("/")[-1] in self.forbidden:
            fail(f"refusing to stage {arcname}: its name is an original game file")
        same_size = self.by_size.get(path.stat().st_size, [])
        if same_size:
            digest = sha256(path)
            for _name, gpath in same_size:
                if gpath not in self.hashes:
                    self.hashes[gpath] = sha256(gpath)
                if self.hashes[gpath] == digest:
                    fail(f"refusing to stage {arcname}: same content as the game file {gpath.name}")


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    result = subprocess.run([str(c) for c in cmd], **kw)
    if result.returncode != 0:
        fail(f"command failed ({result.returncode}): {cmd[0]}")


def build_and_test():
    if BUILD.exists():
        shutil.rmtree(BUILD)
    if MSYS2_BIN.is_dir():
        os.environ["PATH"] = str(MSYS2_BIN) + os.pathsep + os.environ["PATH"]
    cmake = shutil.which("cmake")
    if not cmake:
        fail("cmake is not on PATH")
    run([cmake, "-S", ROOT / "f117port", "-B", BUILD, "-G", "Ninja",
         "-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++", "-DCMAKE_BUILD_TYPE=Release",
         "-DF117_LAUNCHER=ON"])
    run([cmake, "--build", BUILD])
    ctest = shutil.which("ctest")
    result = subprocess.run([ctest, "--test-dir", str(BUILD), "--output-on-failure"], capture_output=True, text=True)
    lines = (result.stdout + result.stderr).splitlines()
    print("\n".join(lines[-6:]))
    if result.returncode != 0:
        fail("ctest failed; no release was made")


def stage(guard):
    if STAGE.exists():
        shutil.rmtree(STAGE)
    STAGE.mkdir(parents=True)
    exe = [BUILD / "f117.exe", BUILD / "F-117A Nighthawk.exe"]
    dlls = sorted(BUILD.glob("*.dll"))
    if not all(p.is_file() for p in exe) or not dlls:
        fail(f"the build did not produce the programs and DLLs in {BUILD}")
    files = []  # (source, path inside the release)
    files += [(p, p.name) for p in exe + dlls]
    files += [(ROOT / "README.md", "README.md"), (ROOT / "CHANGELOG.md", "CHANGELOG.md"),
              (ROOT / "LICENSE", "LICENSE")]
    files += [(p, p.relative_to(ROOT).as_posix()) for p in sorted((ROOT / "licenses").rglob("*")) if p.is_file()]
    for src, rel in files:
        if not src.is_file():
            fail(f"missing {src}")
        guard.check_file(src, rel)
        dest = STAGE / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
    game = STAGE / "Game"
    game.mkdir()
    (game / "PUT_GAME_FILES_HERE.txt").write_text(
        "Put your own copy of the original F-117A Nighthawk Stealth Fighter 2.0 files here, the folder\n"
        "that holds F117.COM, SETUP.EXE, START.EXE and the rest. The game files are not part of this download.\n"
        "See README.md, section How to play.\n", encoding="utf-8")
    return files


def make_zip(files):
    if ZIP.exists():
        ZIP.unlink()
    with zipfile.ZipFile(ZIP, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(STAGE.rglob("*")):
            if path.is_file():
                z.write(path, f"{NAME}/{path.relative_to(STAGE).as_posix()}")
    return ZIP


def check_zip(guard, zip_path):
    """Every name and content in the zip against the game folder and the launcher's list."""
    with zipfile.ZipFile(zip_path) as z:
        entries = [i for i in z.infolist() if not i.is_dir()]
        for info in entries:
            base = info.filename.split("/")[-1].upper()
            if base in guard.forbidden:
                fail(f"the zip holds a game file name: {info.filename}")
            for _n, gpath in guard.by_size.get(info.file_size, []):
                with z.open(info) as f:
                    digest = hashlib.sha256(f.read()).hexdigest()
                if gpath not in guard.hashes:
                    guard.hashes[gpath] = sha256(gpath)
                if guard.hashes[gpath] == digest:
                    fail(f"the zip holds a copy of the game file {gpath.name}")
    return entries


def main():
    guard = GameGuard()
    print(f"game folder: {GAME_DIR} ({len(guard.game_names)} files), launcher list: {len(guard.launcher)} names")
    build_and_test()
    files = stage(guard)
    zip_path = make_zip(files)
    entries = check_zip(guard, zip_path)
    print(f"staged {len(files)} files in {STAGE.relative_to(ROOT)}")
    print(f"wrote {zip_path.relative_to(ROOT)}: {len(entries)} files, {zip_path.stat().st_size / 1048576:.2f} MB")
    print("game-data check: OK (no launcher-list name, no file equal to a file in Game/)")


if __name__ == "__main__":
    main()
