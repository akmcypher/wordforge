#!/usr/bin/env python3
"""Bundle a native Ubuntu 24.04 build; snap pack itself needs no running snapd.

Only run on the matching build architecture. Output is an unsigned preview,
not evidence of installation, confinement, Store review or publication.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def run(*args):
    return subprocess.check_output(args, text=True).strip()


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build-dir", type=Path, required=True)
parser.add_argument("--skia-dir", type=Path, required=True)
parser.add_argument("--output-dir", type=Path, required=True)
args = parser.parse_args()
release = dict(line.split("=", 1) for line in Path("/etc/os-release").read_text().splitlines() if "=" in line)
if release.get("ID", "").strip('"') != "ubuntu" or release.get("VERSION_ID", "").strip('"') != "24.04":
    parser.error("Build the core24 payload on Ubuntu 24.04")
arch = run("dpkg", "--print-architecture")
if arch not in ("arm64", "amd64"):
    parser.error("Only native arm64 and amd64 payloads are supported")
source = Path(__file__).resolve().parents[1]
assets = source / "packaging/snap"
args.output_dir.mkdir(parents=True, exist_ok=True)
stage = Path(tempfile.mkdtemp(prefix="wordforge-snap-", dir=args.output_dir.resolve()))
stage.chmod(0o755)


def copy(src, relative):
    src = Path(src)
    dest = stage / relative
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dest, follow_symlinks=True)


binary = args.build_dir.resolve() / "wordforge"
machine = run("readelf", "-h", str(binary))
if ("AArch64" if arch == "arm64" else "X86-64") not in machine:
    parser.error("Executable architecture does not match the packaging host")
copy(binary, "usr/bin/wordforge")
copy(assets / "wordforge-launch", "bin/wordforge-launch")
(stage / "bin/wordforge-launch").chmod(0o755)
# In a packed snap the launcher lives in meta/gui/<app>.desktop; `desktop:` is snapcraft.yaml-only.
copy(assets / "wordforge.desktop", "meta/gui/wordforge.desktop")
copy(assets / "wordforge.svg", "meta/gui/wordforge.svg")
# The Snap Store takes the listing icon from meta/gui/icon.svg on upload.
copy(assets / "wordforge.svg", "meta/gui/icon.svg")
triplet = run("dpkg-architecture", "-qDEB_HOST_MULTIARCH")
plugins = Path("/usr/lib") / triplet / "qt6/plugins"
for required in ("libqxcb.so", "libqwayland-generic.so"):
    if not (plugins / "platforms" / required).is_file():
        raise RuntimeError(f"Missing Qt platform plugin: {required}; install qt6-wayland and qt6-base-dev")
elfs = [binary]
for group in ("platforms", "platforminputcontexts", "printsupport", "imageformats", "xcbglintegrations", "wayland-shell-integration", "wayland-decoration-client", "wayland-graphics-integration-client"):
    for plugin in (plugins / group).glob("*.so"):
        copy(plugin, f"usr/plugins/{group}/{plugin.name}")
        elfs.append(plugin)
# ldd resolves the complete transitive shared-library closure, including Qt's plugins.
libraries = set()
for elf in elfs:
    linkage = run("ldd", str(elf))
    if "not found" in linkage:
        raise RuntimeError(f"Unresolved dependency of {elf}:\n{linkage}")
    for path in re.findall(r"(?:=>\s+|^\s*)(/\S+)", linkage, re.MULTILINE):
        library = Path(path)
        if library.name.startswith("ld-linux"):
            continue  # core24 supplies its ELF interpreter.
        libraries.add(library)
for library in sorted(libraries):
    copy(library, f"usr/lib/{library.name}")
for group in ("xkb", "locale"):
    shutil.copytree(Path("/usr/share/X11") / group, stage / "usr/share/X11" / group)
fonts = Path("/usr/share/fonts/truetype")
for family in ("dejavu", "noto"):
    for font in (fonts / family).glob("*.ttf"):
        copy(font, f"usr/share/fonts/truetype/{family}/{font.name}")
if not list((stage / "usr/share/fonts").rglob("*.ttf")):
    raise RuntimeError("Install fonts-dejavu-core and fonts-noto-core before packaging")
# Spelling dictionaries; the app also reads the host's /usr/share/hunspell.
for dictionary in Path("/usr/share/hunspell").glob("en_US.*"):
    copy(dictionary, f"usr/share/hunspell/{dictionary.name}")
if not (stage / "usr/share/hunspell/en_US.dic").is_file():
    raise RuntimeError("Install hunspell-en-us before packaging")
config = stage / "etc/fonts/fonts.conf"
config.parent.mkdir(parents=True)
config.write_text('''<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig><dir prefix="relative">../../usr/share/fonts</dir>
<dir>/usr/share/fonts</dir><dir>/usr/local/share/fonts</dir>
<cachedir prefix="xdg">fontconfig</cachedir></fontconfig>
''')
notices = stage / "usr/share/doc/wordforge"
notices.mkdir(parents=True)
copy(source / "LICENSE", "usr/share/doc/wordforge/LICENSE")
copy(args.skia_dir / "LICENSE", "usr/share/doc/wordforge/skia-LICENSE")
# Preserve Skia's bundled notices as well as the native distribution's notices.
for notice in (args.skia_dir / "third_party").rglob("*"):
    if notice.is_file() and (notice.name.startswith(("LICENSE", "COPYING", "NOTICE")) or notice.name == "FTL.TXT"):
        copy(notice, "usr/share/doc/wordforge/skia-third-party/" + str(notice.relative_to(args.skia_dir / "third_party")))
packages = {"fonts-dejavu-core", "fonts-noto-core", "fonts-noto-color-emoji", "xkb-data", "libx11-data",
            "hunspell-en-us"}
for path in libraries:
    ownership = run("dpkg-query", "-S", str(path.resolve()))
    packages.update(line.split(": ", 1)[0] for line in ownership.splitlines())
inventory = []
for package in sorted(packages):
    inventory.append(run("dpkg-query", "-W", "-f=${Package} ${Version} ${Architecture}", package))
    name = package.split(":")[0]
    copyright = Path("/usr/share/doc") / name / "copyright"
    copy(copyright, f"usr/share/doc/wordforge/dependencies/{name}/copyright")
(notices / "packages.txt").write_text("\n".join(inventory) + "\n")
(notices / "build.txt").write_text(
    f"Architecture: {arch}\nSource commit: {run('git', '-C', str(source), 'rev-parse', 'HEAD')}\n"
    f"Uncommitted changes: {bool(run('git', '-C', str(source), 'status', '--porcelain', '--', '.'))}\n"
    f"Executable SHA256: {hashlib.file_digest(binary.open('rb'), 'sha256').hexdigest()}\n"
)
(stage / "meta/snap.yaml").write_text((assets / "snap.yaml.in").read_text().replace("@ARCH@", arch))
run("snap", "pack", "--check-skeleton", str(stage))
print(run("snap", "pack", str(stage), str(args.output_dir.resolve())), flush=True)
version = re.search(r"^version: '([^']+)'$", (stage / "meta/snap.yaml").read_text(), re.MULTILINE).group(1)
snap = args.output_dir / f"wordforge_{version}_{arch}.snap"
digest = hashlib.file_digest(snap.open("rb"), "sha256").hexdigest()
snap.with_suffix(".snap.sha256").write_text(f"{digest}  {snap.name}\n")
print(f"Stage: {stage}\nSHA256: {digest}", flush=True)
