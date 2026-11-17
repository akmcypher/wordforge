# Cross-format and multilingual compatibility check against Microsoft Word.
#
# Word writes one formatted document with text in ~20 languages and scripts
# (world-samples.json) in every format it can save, plus each language alone as plain text in
# its traditional code pages (Shift_JIS, GB18030, Big5, windows-1251, KOI8-R...). Each file is
# opened with wordforge_roundtrip exactly as Wordforge opens it and saved as DOCX; Word then
# opens that DOCX and the script checks which sample lines are still there.
#
#   powershell -ExecutionPolicy Bypass -File tools\fidelity\format-compat.ps1 `
#       -Tool build\<dir>\wordforge_roundtrip.exe
#
# Writes out\formats\<timestamp>\results.csv and report.md. Needs Microsoft Word; starts its
# own hidden Word instance and quits only it. Keep this file ASCII: Windows PowerShell 5.1
# reads scripts without a byte-order mark in the ANSI code page.
param(
    [Parameter(Mandatory = $true)][string]$Tool,
    [string]$Out = ""
)
$ErrorActionPreference = "Stop"
$Tool = (Resolve-Path $Tool).Path
$languages = (Get-Content (Join-Path $PSScriptRoot "world-samples.json") -Raw -Encoding UTF8 |
    ConvertFrom-Json).languages
if (-not $Out) { $Out = Join-Path "out\formats" (Get-Date -Format "yyyy-MM-dd_HHmmss") }
New-Item -ItemType Directory -Force (Join-Path $Out "files"), (Join-Path $Out "wordforge") | Out-Null
$Out = (Resolve-Path $Out).Path
$deps = "C:\dev\wordforge-deps\msys64\clangarm64\bin"
if (Test-Path $deps) { $env:PATH = "$deps;$env:PATH" }
$env:QT_QPA_PLATFORM = "offscreen"

$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0
$word.Options.CheckSpellingAsYouType = $false
$word.Options.CheckGrammarAsYouType = $false
$wordVersion = "$($word.Version) (build $($word.Build))"
# On any failure, quit this Word instance (never the user's) before stopping.
trap { try { $word.Quit(0) } catch {}; break }

function Add($document, [string]$text, $style = $null) {
    $document.Content.InsertParagraphAfter()
    $range = $document.Paragraphs.Last.Range
    $range.Text = $text
    $range = $document.Paragraphs.Last.Range
    if ($style) { $range.Style = $style }
    return $range
}
function Save-Word($document, [string]$path, [int]$format, [int]$encoding = 0) {
    if ($encoding) {
        # SaveAs2(FileName, FileFormat, LockComments, Password, AddToRecentFiles, WritePassword,
        #         ReadOnlyRecommended, EmbedTrueTypeFonts, SaveNativePictureFormat,
        #         SaveFormsData, SaveAsAOCELetter, Encoding)
        $document.SaveAs2([string]$path, $format, $false, "", $false, "", $false, $false, $false,
            $false, $false, $encoding)
    } else {
        $document.SaveAs2([string]$path, $format)
    }
}
# Text without any whitespace or Word's control marks, so line wrapping, cell marks and
# paragraph marks never decide a match.
function Squash([string]$text) { return ($text -replace "[\s\x00-\x1f\u00a0\u200b]", "") }

$cases = @()
$files = Join-Path $Out "files"

# 1. One formatted document with every language, saved in each Word format.
$all = @()
foreach ($language in $languages) { $all += $language.lines }
$d = $word.Documents.Add()
$d.Paragraphs.Last.Range.Text = "World sample"
$d.Paragraphs.Last.Range.Style = $d.Styles.Item(-63).NameLocal # Title
foreach ($language in $languages) {
    [void](Add $d $language.name $d.Styles.Item(-3).NameLocal) # Heading 2
    $first = $true
    foreach ($line in $language.lines) {
        $r = Add $d $line
        if ($first) {
            $r.Words.Item(1).Bold = $true
            $r.ParagraphFormat.Alignment = 3 # justified
            $r.ParagraphFormat.LeftIndent = 36
            $r.ParagraphFormat.FirstLineIndent = -18
            $first = $false
        }
        if ($language.name -in @("Arabic", "Hebrew", "Persian")) { $r.ParagraphFormat.ReadingOrder = 0 }
    }
}
[void](Add $d "Languages in a list")
$list = Add $d $languages[0].lines[0]
$list.ListFormat.ApplyBulletDefault()
$table = $d.Tables.Add((Add $d "table"), 2, 2)
$table.Borders.Enable = 1
$table.Cell(1, 1).Range.Text = "Language"
$table.Cell(1, 2).Range.Text = "Sample"
$table.Cell(2, 1).Range.Text = "Japanese"
$table.Cell(2, 2).Range.Text = "OK"
$note = Add $d "A footnote follows."
[void]$d.Footnotes.Add($d.Range($note.End - 1, $note.End - 1), [Type]::Missing, "Footnote text.")
$worldFormats = @(
    @("Word document (.docx)", "world.docx", 16, 0, $true),
    @("Word macro-enabled document (.docm)", "world.docm", 13, 0, $true),
    @("Word template (.dotx)", "world.dotx", 14, 0, $true),
    @("Word macro-enabled template (.dotm)", "world.dotm", 15, 0, $true),
    @("Strict Open XML document (.docx)", "world-strict.docx", 24, 0, $true),
    @("Word 97-2003 document (.doc)", "world.doc", 0, 0, $false),
    @("Rich Text Format (.rtf)", "world.rtf", 6, 0, $false),
    @("OpenDocument text (.odt)", "world.odt", 23, 0, $false),
    @("Word XML document (.xml)", "world.xml", 19, 0, $false),
    @("Web page, filtered (.htm)", "world.htm", 10, 0, $false),
    @("PDF (.pdf)", "world.pdf", 17, 0, $false),
    @("Plain text, UTF-8 (.txt)", "world-utf8.txt", 7, 65001, $true),
    @("Plain text, UTF-16 LE (.txt)", "world-utf16le.txt", 7, 1200, $true),
    @("Plain text, UTF-16 BE (.txt)", "world-utf16be.txt", 7, 1201, $true)
)
foreach ($format in $worldFormats) {
    $path = Join-Path $files $format[1]
    Save-Word $d $path $format[2] $format[3]
    $cases += [pscustomobject]@{ Group = "All languages, formatted"; Format = $format[0]; Path = $path;
        Expected = $all; Formatted = $format[4] -and $format[2] -ne 7 }
}
$d.Close(0)

# 2. Each language alone as plain text in its traditional code pages.
$names = @{ 874 = "Thai (874)"; 932 = "Shift_JIS (932)"; 949 = "Korean (949)"; 950 = "Big5 (950)";
    1250 = "Central European (1250)"; 1251 = "Cyrillic (1251)"; 1252 = "Western (1252)";
    1253 = "Greek (1253)"; 1254 = "Turkish (1254)"; 1255 = "Hebrew (1255)"; 1256 = "Arabic (1256)";
    20866 = "KOI8-R"; 51932 = "EUC-JP"; 54936 = "GB18030" }
foreach ($language in $languages) {
    foreach ($codepage in $language.codepages) {
        $d = $word.Documents.Add()
        $d.Content.Text = ($language.lines -join "`r")
        $path = Join-Path $files ("{0}-cp{1}.txt" -f ($language.name -replace "[^A-Za-z]", ""), $codepage)
        Save-Word $d $path 7 $codepage
        $d.Close(0)
        $cases += [pscustomobject]@{ Group = $language.name; Format = "Plain text, " + $names[[int]$codepage];
            Path = $path; Expected = $language.lines; Formatted = $false }
    }
}

# Open each file with Wordforge, save it as DOCX, then read that DOCX back in Word.
$results = @()
foreach ($case in $cases) {
    $name = [IO.Path]::GetFileName($case.Path)
    $saved = Join-Path $Out ("wordforge\" + $name + ".docx")
    $json = Join-Path $Out ("wordforge\" + $name + ".json")
    & $Tool $case.Path $saved $json | Out-Null
    $report = Get-Content $json -Raw -Encoding UTF8 | ConvertFrom-Json
    $row = [ordered]@{ Group = $case.Group; Format = $case.Format; File = $name; Opens = "no";
        TextKept = ""; Missing = ""; Encoding = ""; Warnings = ""; Note = "" }
    if (-not $report.ok) {
        $row.Note = $report.error
    } else {
        $row.Opens = "yes"
        $row.Encoding = $report.encoding
        $row.Warnings = ($report.warnings -join " | ")
        $opened = $word.Documents.Open([string]$saved, $false, $true, $false)
        $text = $opened.Content.Text
        foreach ($n in $opened.Footnotes) { $text += $n.Range.Text }
        $opened.Close(0)
        $squashed = Squash $text
        $missing = @($case.Expected | Where-Object { -not $squashed.Contains((Squash $_)) })
        $row.TextKept = "{0}/{1}" -f ($case.Expected.Count - $missing.Count), $case.Expected.Count
        $row.Missing = ($missing | ForEach-Object { $_.Substring(0, [Math]::Min(24, $_.Length)) }) -join " | "
    }
    $results += [pscustomobject]$row
    Write-Host ("{0,-44} {1,-4} {2,-6} {3}" -f $row.Format, $row.Opens, $row.TextKept, $row.Encoding)
}
$word.Quit()

$results | Export-Csv (Join-Path $Out "results.csv") -NoTypeInformation -Encoding UTF8
$lines = @("# Format and language compatibility", "",
    "Microsoft Word $wordVersion wrote every file; Wordforge opened it and saved DOCX; Word read",
    "that DOCX back. Text kept counts sample lines found again (whitespace ignored).", "",
    "| Content | Format | Wordforge opens | Text kept | Detected encoding | Notes |",
    "|---|---|---|---|---|---|")
foreach ($r in $results) {
    $note = if ($r.Opens -eq "no") { $r.Note } elseif ($r.Missing) { "missing: " + $r.Missing } else { "" }
    $lines += "| {0} | {1} | {2} | {3} | {4} | {5} |" -f $r.Group, $r.Format, $r.Opens, $r.TextKept,
        $r.Encoding, ($note -replace "\|", "/")
}
$lines | Set-Content (Join-Path $Out "report.md") -Encoding UTF8
Write-Host "Report: $(Join-Path $Out 'report.md')"
