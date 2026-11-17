# Formats and languages

Which files Wordforge opens, measured with `tools/fidelity/format-compat.ps1`. Microsoft
Word writes one formatted document holding text in 23 languages and scripts
(`tools/fidelity/world-samples.json`: Latin, Greek, Cyrillic, Arabic, Hebrew, Persian,
Devanagari, Bengali, Tamil, Thai, Vietnamese, Chinese, Japanese, Korean, emoji) in every
format it can save. It also saves each language alone as plain text in its traditional code
pages. Wordforge opens each file exactly as the application does and saves DOCX; Word reads
that DOCX back and the script counts the sample lines that survived.

```powershell
powershell -ExecutionPolicy Bypass -File tools\fidelity\format-compat.ps1 -Tool build\<dir>\wordforge_roundtrip.exe
```

## Results

2026-11-17, Microsoft Word 16.0 (build 16.0.20527):

| Format | Opens | Text kept |
|---|---|---|
| Word document (.docx) | yes | 67/67 lines |
| Word Strict Open XML document (.docx) | yes | 67/67 |
| Word macro-enabled document (.docm), macros removed | yes | 67/67 |
| Word template (.dotx) and macro-enabled template (.dotm) | yes | 67/67 |
| Plain text, UTF-8, UTF-16 LE and UTF-16 BE | yes | 67/67 |
| Plain text in 18 legacy code pages (below) | yes | all lines |
| Word 97–2003 (.doc), RTF, OpenDocument (.odt), Word XML (.xml), web page, PDF | no | — |

Legacy code pages detected and decoded correctly: windows-1250 (Polish, Czech),
windows-1251 and KOI8-R (Russian, Ukrainian), windows-1252 (English, French, German,
Spanish), Turkish (detected as ISO-8859-9), Greek (ISO-8859-7), Hebrew (ISO-8859-8),
windows-1256 (Arabic), windows-874 (Thai), GB18030, Big5, Shift_JIS, EUC-JP and EUC-KR.

Wordforge refuses unsupported formats with an error and never opens them partially. The
formats it does not open are the next interoperability work, most used first: legacy
.doc, OpenDocument .odt (LibreOffice's default), and RTF.

## Limits of this check

- It checks that the text survives. It does not check formatting; for formatting in DOCX
  see [WORD_FIDELITY.md](WORD_FIDELITY.md).
- Legacy encodings are guessed from the bytes (ICU's charset detector, plus a Thai rule ICU
  lacks). Very short files can still be guessed wrong; a manual encoding choice is not yet
  offered.
- Detecting ISO-8859-7/9 for Greek and Turkish Windows files kept every sample line here,
  but a few punctuation characters differ between those code pages.
- LibreOffice is not tested yet. It is not installed on the test machine, and files it
  writes may differ from Word's.
