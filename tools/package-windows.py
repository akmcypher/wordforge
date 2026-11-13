#!/usr/bin/env python3
"""Build the portable Windows package of a Release build (MSYS2 CLANG64 or CLANGARM64).

Stages Wordforge.exe with the Qt plugins windeployqt selects, every DLL it needs from the MSYS2
prefix (found by walking PE imports until nothing new appears), Hunspell dictionaries, licences
and build information, then refuses to package if any import is neither in the package nor a
Windows system DLL. Output: Wordforge-<version>-windows-<arch>.zip and its .sha256 file.
"""
import argparse
import datetime
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

DICTIONARIES = ["en_US", "en_GB", "en_AU", "en_CA", "en_IN"]

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build-dir", type=Path, required=True, help="folder holding wordforge.exe")
parser.add_argument("--prefix", type=Path, required=True,
                    help="MSYS2 environment prefix, e.g. C:/msys64/clang64")
parser.add_argument("--output-dir", type=Path, required=True)
parser.add_argument("--arch", choices=["x64", "arm64"], required=True)
parser.add_argument("--build-id", default="")
args = parser.parse_args()

source = Path(__file__).resolve().parent.parent
prefix = args.prefix.resolve()
bin_dir = prefix / "bin"
msys_root = prefix.parent
version = re.search(r"project\(Wordforge VERSION ([0-9.]+)",
                    (source / "CMakeLists.txt").read_text()).group(1)
build_id = args.build_id or subprocess.run(
    ["git", "-C", str(source), "rev-parse", "--short=12", "HEAD"],
    capture_output=True, text=True).stdout.strip() or "local"
name = f"Wordforge-{version}-windows-{args.arch}"
stage = args.output_dir.resolve() / name
if stage.exists():
    shutil.rmtree(stage)
stage.mkdir(parents=True)

program = args.build_dir / "wordforge.exe"
if not program.is_file():
    parser.error(f"{program} not found; build the release preset first")
shutil.copy2(program, stage / "Wordforge.exe")

env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ.get("PATH", ""))
windeployqt = next((bin_dir / n for n in ("windeployqt6.exe", "windeployqt.exe")
                    if (bin_dir / n).is_file()), None)
if windeployqt is None:
    sys.exit("windeployqt not found in the MSYS2 prefix (install qt6-tools)")
subprocess.run([str(windeployqt), "--no-translations", "--no-system-d3d-compiler",
                "--no-opengl-sw", "--no-compiler-runtime",
                # Touch input, network status and TLS plugins would pull in Qt6Network unused.
                "--skip-plugin-types", "generic,networkinformation,tls", "--dir", str(stage),
                str(stage / "Wordforge.exe")], check=True, env=env)

objdump = shutil.which("llvm-objdump", path=str(bin_dir)) or shutil.which("objdump", path=str(bin_dir))
if objdump is None:
    sys.exit("llvm-objdump not found in the MSYS2 prefix")


def imports(binary: Path) -> list[str]:
    output = subprocess.run([objdump, "-p", str(binary)], capture_output=True, text=True,
                            check=True).stdout
    return re.findall(r"DLL Name:\s*(\S+)", output)


available = {p.name.lower(): p for p in bin_dir.glob("*.dll")}
system_dirs = [Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"]


def is_system(dll: str) -> bool:
    lower = dll.lower()
    return lower.startswith(("api-ms-win-", "ext-ms-")) or any(
        (d / dll).is_file() for d in system_dirs)


copied_from_prefix: list[Path] = []
unresolved: dict[str, set[str]] = {}
seen: set[Path] = set()
while True:
    binaries = [p for p in stage.rglob("*") if p.suffix.lower() in (".exe", ".dll")]
    pending = [p for p in binaries if p not in seen]
    if not pending:
        break
    for binary in pending:
        seen.add(binary)
        for dll in imports(binary):
            lower = dll.lower()
            if (stage / dll).is_file() or any(p.name.lower() == lower for p in stage.glob("*.dll")):
                continue
            if lower in available:
                shutil.copy2(available[lower], stage / available[lower].name)
                copied_from_prefix.append(available[lower])
            elif not is_system(dll):
                unresolved.setdefault(dll, set()).add(binary.relative_to(stage).as_posix())
if unresolved:
    for dll, users in sorted(unresolved.items()):
        print(f"unresolved import {dll} (needed by {', '.join(sorted(users))})", file=sys.stderr)
    sys.exit("package is incomplete")


def unguarded_vector_code(binary: Path) -> tuple[int, list[str]]:
    """Functions using AVX2 (ymm) or AVX-512 (zmm, mask) registers outside Skia's CPU-checked
    namespaces, and the number of named functions seen (0 means the binary has no symbols)."""
    process = subprocess.Popen([objdump, "-d", "-C", "--no-show-raw-insn", str(binary)],
                               stdout=subprocess.PIPE, text=True, errors="replace")
    label = re.compile(r"^[0-9a-f]+ <(.*)>:$")
    vector = re.compile(r"%(ymm|zmm)\d+|%k[1-7]\b")
    allowed = re.compile(r"(^|[^A-Za-z_])(avx|hsw|skx)::")
    functions, current, offenders = 0, "", set()
    for line in process.stdout:
        match = label.match(line)
        if match:
            current, functions = match.group(1), functions + 1
        elif current not in offenders and vector.search(line) and not allowed.search(current):
            offenders.add(current)
    process.wait()
    return functions, sorted(offenders)


# Skia's per-CPU files once leaked AVX-512 into shared code (tools/build-skia-msys2.sh): an
# x64 package must run on every x86-64 CPU, so check the rebuilt DLL, which keeps symbols.
if args.arch == "x64":
    skia = stage / "libskia.dll"
    functions, offenders = unguarded_vector_code(skia)
    if functions < 1000:
        sys.exit(f"{skia.name} has no symbols; install the rebuilt Skia from tools/build-skia-msys2.sh")
    if offenders:
        print("\n".join(f"unguarded AVX2/AVX-512 code: {name}" for name in offenders[:40]), file=sys.stderr)
        sys.exit(f"{skia.name} would crash on CPUs without AVX2/AVX-512 ({len(offenders)} functions)")
    strip = shutil.which("llvm-strip", path=str(bin_dir)) or shutil.which("strip", path=str(bin_dir))
    if strip is None:
        sys.exit("llvm-strip not found in the MSYS2 prefix")
    subprocess.run([strip, "--strip-unneeded", str(skia)], check=True)
    print(f"{skia.name}: {functions} functions, no unguarded AVX2/AVX-512 code")

# Plugins windeployqt copied also come from the prefix; record them for licence lookup.
plugin_root = prefix / "share/qt6/plugins"
for plugin in stage.rglob("*.dll"):
    relative = plugin.relative_to(stage)
    if len(relative.parts) == 2 and (plugin_root / relative).is_file():
        copied_from_prefix.append(plugin_root / relative)
    elif len(relative.parts) == 1 and (bin_dir / plugin.name).is_file():
        copied_from_prefix.append(bin_dir / plugin.name)

dictionary_dir = stage / "dictionaries"
dictionary_dir.mkdir()
for language in DICTIONARIES:
    for extension in (".aff", ".dic"):
        file = prefix / "share/hunspell" / (language + extension)
        if not file.is_file():
            sys.exit(f"missing dictionary {file} (install hunspell-en)")
        shutil.copy2(file, dictionary_dir / file.name)
        copied_from_prefix.append(file)

# Map copied files to their MSYS2 packages through the local pacman database.
database = msys_root / "var/lib/pacman/local"
owners: dict[str, dict] = {}
for entry in database.iterdir():
    files_list, desc = entry / "files", entry / "desc"
    if not files_list.is_file() or not desc.is_file():
        continue
    fields = dict(re.findall(r"%(\w+)%\n((?:.+\n?)*)", desc.read_text(encoding="utf-8")))
    package = {"name": fields.get("NAME", "").strip(), "version": fields.get("VERSION", "").strip(),
               "license": " ".join(fields.get("LICENSE", "").split()), "url": fields.get("URL", "").strip(),
               "files": [l for l in files_list.read_text(encoding="utf-8").splitlines() if l and not l.startswith("%")]}
    for line in package["files"]:
        owners[line.lower()] = package
used: dict[str, dict] = {}
for file in copied_from_prefix:
    key = file.resolve().relative_to(msys_root).as_posix().lower()
    if key in owners:
        used[owners[key]["name"]] = owners[key]
environment = prefix.name  # clang64 / clangarm64
package_prefix = {"clang64": "mingw-w64-clang-x86_64-", "clangarm64": "mingw-w64-clang-aarch64-"}.get(environment, "")

licences = stage / "licenses"
notices = ["Wordforge is distributed under the Apache License 2.0 (LICENSE.txt).",
           "It bundles the following third-party components, built by the MSYS2 project.",
           "Each component's licence texts are in licenses/<component>/.",
           "Libraries are dynamically linked DLLs and can be replaced. Their source code is",
           "available from https://packages.msys2.org and the project sites listed below.", ""]
for package in sorted(used.values(), key=lambda p: p["name"]):
    short = package["name"].removeprefix(package_prefix)
    notices.append(f"{short} {package['version']}  licence: {package['license'] or 'see folder'}  {package['url']}")
    for line in package["files"]:
        if "/share/licenses/" in line and not line.endswith("/"):
            target = licences / short / Path(line).name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(msys_root / line, target)
shutil.copy2(source / "LICENSE", stage / "LICENSE.txt")
(stage / "THIRD-PARTY-NOTICES.txt").write_text("\n".join(notices) + "\n", encoding="utf-8")

built = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
(stage / "BUILD-INFO.txt").write_text(
    f"Wordforge {version}\nBuild: {build_id}\nArchitecture: windows-{args.arch}\n"
    f"Built: {built}\nToolchain: MSYS2 {environment}\n", encoding="utf-8")
(stage / "README.txt").write_text(
    f"Wordforge {version} developer preview for Windows ({args.arch})\n\n"
    "Unzip this folder anywhere and run Wordforge.exe. Nothing else needs to be installed.\n"
    "Settings and recovery drafts are kept in %APPDATA%\\Wordforge.\n\n"
    "Check an installation: Wordforge.exe --self-test report.txt\n"
    "Source, issues and release notes: https://github.com/akmcypher/wordforge\n", encoding="utf-8")

archive = args.output_dir.resolve() / f"{name}.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
    for file in sorted(stage.rglob("*")):
        if file.is_file():
            bundle.write(file, Path(name) / file.relative_to(stage))
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
# LF endings so `sha256sum -c` also works on Linux and macOS.
(archive.parent / f"{archive.name}.sha256").write_text(f"{digest}  {archive.name}\n", newline="\n")
size = sum(f.stat().st_size for f in stage.rglob("*") if f.is_file())
print(f"built: {archive}\nSHA256: {digest}\nfiles: {sum(1 for f in stage.rglob('*') if f.is_file())}, "
      f"unpacked {size / 1e6:.1f} MB, zip {archive.stat().st_size / 1e6:.1f} MB")
