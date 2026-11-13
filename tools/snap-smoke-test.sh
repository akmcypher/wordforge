#!/usr/bin/env bash
# Install a Wordforge snap and check what can be checked automatically, then print
# the manual checklist. Meant for a real Ubuntu desktop (e.g. a 24.04 live USB),
# where strict confinement is available.
# Usage: tools/snap-smoke-test.sh wordforge_<version>_<arch>.snap [--allow-partial]
set -uo pipefail
snap_file=${1:?path to the .snap file required}
allow_partial=${2:-}
failures=0
pass() { printf '  \033[32mPASS\033[0m %s\n' "$*"; }
fail() { printf '  \033[31mFAIL\033[0m %s\n' "$*"; failures=$((failures + 1)); }
note() { printf '  NOTE %s\n' "$*"; }
sudo=""
[[ $EUID -ne 0 ]] && sudo="sudo"

echo "== Machine"
echo "  $(. /etc/os-release && echo "$PRETTY_NAME") $(uname -m), kernel $(uname -r)"
echo "  session: ${XDG_SESSION_TYPE:-unknown}"
confinement=$(snap debug confinement)
if [[ "$confinement" == strict ]]; then
  pass "snapd supports strict confinement"
elif [[ "$allow_partial" == --allow-partial ]]; then
  note "snapd confinement is '$confinement' (allowed for a dry run; not a strict result)"
else
  fail "snapd confinement is '$confinement', not strict; results would not prove strict confinement"
fi

echo "== Package"
if [[ -f "$snap_file.sha256" ]]; then
  (cd "$(dirname "$snap_file")" && sha256sum --check --quiet "$(basename "$snap_file").sha256") &&
    pass "SHA256 matches" || fail "SHA256 does not match"
else
  note "no $snap_file.sha256 next to the snap; checksum not verified"
fi
arch=$(dpkg --print-architecture)
[[ "$snap_file" == *"_$arch.snap" ]] && pass "snap architecture matches $arch" ||
  fail "snap file name does not end in _$arch.snap"

echo "== Install"
if $sudo snap install --dangerous "$snap_file" >/tmp/wordforge-install.txt 2>&1; then
  pass "$(tail -1 /tmp/wordforge-install.txt)"
else
  fail "install failed: $(tail -3 /tmp/wordforge-install.txt)"
  echo "Stopping: nothing else can be checked."
  exit 1
fi
mode=$(snap info --verbose wordforge 2>/dev/null | awk '/^ *confinement:/ {print $2}')
# This is what the snap requests; the machine check above says whether it is enforced.
[[ "$mode" == strict ]] && pass "snap requests strict confinement" ||
  fail "snap requests '${mode:-unknown}' confinement"

echo "== Interfaces"
while read -r interface plug slot _; do
  [[ "$interface" == Interface ]] && continue
  if [[ "$slot" == - ]]; then
    case "$interface" in
      removable-media) note "removable-media not connected (expected; connect it to use USB drives)" ;;
      cups) fail "$plug is not connected (printing needs CUPS; normal on a desktop, absent in WSL)" ;;
      *) fail "$plug is not connected" ;;
    esac
  else
    pass "$plug -> $slot"
  fi
done < <(snap connections wordforge)

echo "== Bundled resources"
root=/snap/wordforge/current
[[ -f $root/usr/share/hunspell/en_US.dic ]] && pass "en_US spelling dictionary bundled" ||
  fail "spelling dictionary missing"
[[ -f $root/usr/plugins/printsupport/libcupsprintersupport.so ]] && pass "CUPS print plugin bundled" ||
  fail "print plugin missing"
ls $root/usr/share/fonts/truetype/*/*.ttf >/dev/null 2>&1 && pass "fallback fonts bundled" ||
  fail "bundled fonts missing"

echo "== Run"
version=$(timeout 30 snap run wordforge --version 2>/dev/null | tail -1)
[[ "$version" == Wordforge* ]] && pass "$version" || fail "--version printed '${version}'"
if [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
  timeout 15 snap run wordforge --sample >/tmp/wordforge-run.txt 2>&1
  status=$?
  [[ $status -eq 124 ]] && pass "window stayed open for 15 s with the sample document" ||
    fail "app exited with status $status: $(tail -3 /tmp/wordforge-run.txt)"
  denials=$(journalctl -k --since "-2 min" 2>/dev/null | grep -c 'apparmor="DENIED".*snap.wordforge' || true)
  [[ "$denials" == 0 ]] && pass "no AppArmor denials while running" ||
    note "$denials AppArmor denials logged; see: journalctl -k | grep DENIED | grep wordforge"
else
  note "no display; skipped the window test"
fi

echo
echo "== Manual checklist (open with: snap run wordforge)"
cat <<'LIST'
  [ ] Type text; bold/italic/underline; Heading 1; bullets; undo/redo
  [ ] Font list shows your system fonts (not only DejaVu/Noto)
  [ ] Misspell a word, press space: red underline; right-click shows suggestions
  [ ] Copy formatted text from Firefox, paste: formatting kept
  [ ] Insert → Table 3x3; Tab between cells; Format → Table → Insert row below
  [ ] Insert → Picture from ~/Pictures; resize it
  [ ] File → Save as .wfg in ~/Documents, close, reopen; Save as .docx; open in LibreOffice
  [ ] File → Export PDF; File → Print preview; File → Print (to "Print to File"/a printer)
  [ ] Ctrl+K command palette; F11 focus mode; dark theme if the desktop uses it
  [ ] Dead keys / an input method (e.g. Settings → Keyboard → add a language)
  [ ] Type, wait 35 s, then: pkill -9 -f snap/wordforge ; reopen → Recover drafts
  [ ] Repeat the window checks in the other session type (X11 vs Wayland)
LIST
echo
if [[ $failures -eq 0 ]]; then
  echo "Automatic checks passed. Record the manual results in docs/SNAP.md."
else
  echo "$failures automatic check(s) failed."
fi
exit $((failures > 0))
