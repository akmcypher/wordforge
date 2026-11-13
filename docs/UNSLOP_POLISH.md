# Wordforge native interface polish — 2026-11-13

User approved the direction before edits. This is a light polish of the existing
native Qt interface, preserving features, group order, paper geometry and warm brand.

Direction:
- Product: native document editing for writing and formatting.
- Character: deliberate, legible, warm.
- World: paper and ink; brown remains the accent.
- Moments: document content carries detail; chrome stays quiet.
- Signature: restrained brown active-tab underline.

Audit: uneven 9/3/5px group/control rhythm; 11px captions; mixed paragraph text
symbols and line icons; heavy brown checked fills; inconsistent large icon sizes.
No hero or generated imagery is appropriate for this document editor.

Nine passes:
1. Spacing: group padding12/4px,4px gaps,28px controls, consistent tab padding and
   64x60px preset/page-break tiles. Ribbon accommodates native font metrics.
2. Color: keep brown/taupe; reduce saturation in dark chrome, soften checked
   controls to a neutral surface step with a brown outline; quiet hairlines.
3. Icons: paragraph lists/indent use matching18px line drawings,1.5px strokes;
   alignment matches them; paste/picture use24px monochrome drawings.
4. Copy: retain command labels and accessible names; no invented claims/features.
5. Type: captions12px with explicit secondary text; retain native Segoe UI; no
   document-font changes. Bold/italic/underline samples retain their semantics.
6. Layout: existing ribbon/groups and document proportions retained.
7. Details: checked/pressed/focus states defined; QDoubleSpinBox receives the same
   input treatment as other fields. Existing keyboard commands remain intact.
8. Visuals: actual native editable sample document; no decorative assets/prompts.
9. Motion: no animation added; native feedback remains immediate.

Verification: native Windows snapshot inspected after build; no clipped labels
observed at the captured size. All five existing suites pass on Windows ARM64
and Ubuntu ARM64,85 cases (Windows one Ubuntu-only font skip). Logs:
2026-11-13-windows-polish-tests.txt and 2026-11-13-ubuntu-polish-tests.txt.
No new feature tests for this reversible appearance pass. Full accessibility and
all display/theme combinations remain unverified. Table UI remains unfinished.

Local native screenshot: wordforge/out/unslop-after.png (ignored build artifact).
Windows verified output: build/polish-preview; Linux: /root/wordforge-build.
No user's existing editor was closed or edited.

Workflow attribution: unslop by Damir Sokolovsky (@heydamir),
https://heydamir.design/unslop — CC BY4.0. Applied to the native desktop stack;
no skill source was copied into product code.
