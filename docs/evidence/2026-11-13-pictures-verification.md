# Inline picture verification — 2026-11-13

Source checkpoint: 0.6 picture increment on codex/wordforge-native.

- Windows ARM64: Clang23/Qt6.12/Skia143 Release, build/images-final.
  All five CTest executables pass; 75 cases pass and one Ubuntu-only font case skips.
  Qt separate -o txt log records 25 pass including setup/cleanup (23 actual cases).
- Ubuntu24.04 ARM64 in WordforgeUbuntu WSL2: GCC13/Qt6.4.2/Skia143 Release.
  All five executables and 76 actual cases pass; /root/wordforge-build.
- Native Windows computer-use: Insert ribbon → Picture chooser → user's local
  reference PNG → actual picture on page. Picture size changed width360 to300pt;
  locked height followed74.8 to62.3pt and page rendered the resized picture.
  This does not establish screen-reader accessibility or broad desktop usability.
- Independent python-docx1.1.0 opened a native-exported DOCX: one inline shape,
  144x72pt, exact fixture PNG bytes in word/media. Structural evidence only;
  Microsoft Word/LibreOffice visual round trip remains unexecuted.
- Review corrected deleted-image bytes retained in exports and RTL image caret
  direction. Reproductions failed before fixes. Independent final review reported
  no material P1/P2; eight targeted image regressions passed.

Model/format timing in ubuntu-pictures-export.txt uses one tiny4x2 fixture and a
cold first command. It excludes Qt, layout, rendering and disk; it is a single
observation, not a performance target, realistic-photo benchmark or P95 claim.
Generate export by inserting ImageResource{4,2,fixturePNG} at144x72 in Editor,
then serializeNative/serializeDocx. Check using python-docx Document.inline_shapes
and zipfile media bytes. Fixture: tests/fixtures/red-blue.png.

Limits and unsupported features are in CURRENT_STATUS.md and WFG_SCHEMA.md.
No Snap Store release, installed-Snap result or full Word parity is asserted.
