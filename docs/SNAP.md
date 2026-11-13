# Ubuntu Snap preview

2026-11-12 local date. Wordforge is a developer preview, not a stable
replacement for Microsoft Word. The candidate Snap name `wordforge` has not
been registered or verified available. No Store upload has been performed.

## Build

Use native Ubuntu 24.04 on the target architecture (amd64 or arm64). First
follow UBUNTU.md to build Skia and Wordforge and run the tests. Additionally:

```sh
sudo apt-get install snapd squashfs-tools qt6-wayland xkb-data libx11-data hunspell-en-us
python3 tools/package-snap.py \
  --build-dir "$HOME/wordforge-build" \
  --skia-dir "$HOME/.local/share/wordforge-deps/skia" \
  --output-dir out/packages
```

The script uses `snap pack`, not remote builds. It bundles the native Qt
runtime/plugins, shared-library closure, DejaVu/Noto fonts, XKB/Compose data
and dependency copyright notices, and writes a package inventory, executable
hash and source-state record inside the payload. It validates the metadata
before packing. Outputs include a `.snap`, SHA256 sidecar and retained staging
directory. Each run uses a new stage; the same version's output file is replaced.
Run it from a clean checkout after building that checkout for release evidence.

The metadata is in `packaging/snap/snap.yaml.in`: core24, strict confinement,
stable grade (since 0.6.1; the version still says preview), no network access. It declares desktop/X11/Wayland/home/cups and
optional removable-media access. The latter may require an explicit connection.
The desktop interface provides the host fonts at /usr/share/fonts; bundled
fallback fonts and the en_US dictionary are read from $SNAP.

## Required installation checks

On a standalone Ubuntu desktop with full Snap confinement:

```sh
sha256sum --check wordforge_0.6.0-preview_amd64.snap.sha256
sudo snap install --dangerous ./wordforge_0.6.0-preview_amd64.snap
snap debug confinement
snap connections wordforge
snap run wordforge --version
snap run wordforge --sample
```

Use `arm64` filenames on ARM64. Test X11 and Wayland independently, keyboard
navigation, international/dead-key composition, clipboard, file dialogs,
formatted save/reopen, PDF export, recovery after killing an edited process,
and refresh/revert preserving drafts. If removable media is needed, connect
`wordforge:removable-media` and test it explicitly. Do not use devmode to claim
that strict confinement passed.

## Executed evidence and limits

* 2026-11-13: tried to get strict confinement in WSL. With `.wslconfig`
  `kernelCommandLine = apparmor=1 security=apparmor` and securityfs mounted,
  AppArmor runs (105 profiles), but snapd still reports `partial`: "apparmor is
  enabled but some kernel features are missing: network". The WSL kernel
  (6.18.58.1-microsoft-standard-WSL2) lacks Ubuntu's AppArmor network mediation,
  and `snap debug sandbox-features` lists only `classic devmode` confinement
  options. **Strict confinement cannot be validated in WSL**; use a real Ubuntu
  kernel (VM, live USB or installed desktop).
* 2026-11-13 (later): repacked with Qt's CUPS print plugin, libhunspell and the
  hunspell-en-us dictionary (64.6 MB). It installs and `--version` runs. The new
  `cups` plug stayed unconnected on WSL, which has no CUPS; printing and spell
  check inside the installed Snap still need testing on a real Ubuntu desktop.
* 2026-11-13: packed the 0.6.0-preview arm64 Snap (64 MB) from 560b931 plus the
  release-prep changes on native Ubuntu 24.04 ARM64 under WSL2. `snap install
  --dangerous` succeeded (core24 rev 2308 downloaded from the Store), all plugs
  except removable-media auto-connected, `snap run wordforge --version` printed
  0.6.0 and `--sample` ran offscreen for 15 s without error. Confinement on WSL is
  `partial` (no AppArmor), so **strict confinement is still unvalidated**, and no
  amd64 Snap has been built.
* Built the 0.3.1 arm64 Snap on native Ubuntu 24.04.5 ARM64 under WSL2. Metadata
  validation and SquashFS packing pass. The unpacked bundle launches through
  its own wrapper and captures a native X11/WSLg sample window.
  The source recipe now targets 0.4; no newly packed/installed 0.4 artifact is claimed.
* All five CTest suites pass on Windows ARM64 and Ubuntu ARM64. A child-process
  regression kills a real process owning a recovery lock, then reads its draft.
* Installed snapd and enabled systemd only in the dedicated WordforgeUbuntu
  distro. `snap debug confinement` reports `partial`; AppArmor is unavailable.
* `snap install core24` fails with `unable to contact snap store`. Both Windows
  and Ubuntu HTTPS requests to api.snapcraft.io time out; DNS resolves. Therefore
  an installed Snap and strict confinement have **not** been validated here.
* The Ubuntu amd64 CI job now packages and uploads a build artifact after tests.
  It has not run remotely; no amd64 success is claimed.

The unsigned local package is useful for further installation testing. It is
not proof of Store acceptance or production readiness. Full DOCX/ODT interoperability,
tables/images, broader desktop testing and recovery across upgrades remain open.

## Publication record

* 2026-11-13: name `wordforge` registered. First upload (revision 1) was rejected
  by the Store: `desktop` is not allowed in a packed snap's `meta/snap.yaml`; the
  launcher now ships as `meta/gui/wordforge.desktop` (d344e7e). Released to
  `latest/edge`: arm64 revision 2 (local build of d344e7e) and amd64 revision 3
  (CI artifact of PR #1's merge ref). Strict confinement on a real Ubuntu desktop
  is still untested.

## Automatic edge releases

`.github/workflows/snap-release.yml` builds the amd64 and arm64 snaps on every
push to `main` that changes more than docs, runs the tests, and uploads both to
`latest/edge` with `snapcraft upload --release=edge`. Installed copies pick the
new revision up on snapd's next refresh. The Store login is the repository
secret `SNAPCRAFT_STORE_CREDENTIALS` (from `snapcraft export-login`); without it
the workflow builds but skips the upload. Pull requests that touch the snap
packaging build both snaps and never upload. Promotion to `stable` stays manual.

## Publication gate

After the checks above pass, authenticate to the intended publisher account,
register an available name, synchronize that name in metadata, rebuild and
submit only to the edge channel. Obtain the user's approval of the concrete
artifact and listing before public release.

2026-11-14: the user chose to publish 0.6.1 on `latest/stable` so Wordforge appears
in Snap Store search (only stable releases are listed), accepting that strict
confinement has not yet been tested on a real Ubuntu desktop. Stable revisions are
promoted by hand from edge with `snapcraft release wordforge <revision> stable`
after the edge build is installed and launched.

Canonical references: [snap pack](https://snapcraft.io/docs/reference/commands/pack/),
[register a snap](https://ubuntu.com/docs/snapcraft/latest/how-to/publishing/register-a-snap/),
[network requirements](https://snapcraft.io/docs/reference/administration/network-requirements/).
