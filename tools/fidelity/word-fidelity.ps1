# Microsoft Word fidelity check. For each DOCX in the corpus: measure it in Word, open and
# save it with Wordforge (wordforge_roundtrip), open Wordforge's file in Word and measure it
# again. Reports whether Word opened the file, how much text survived, which structures
# (tables, pictures, notes, comments, revisions, sections, fields) kept their counts and how
# many paragraphs kept their style, alignment and leading character formatting.
#
#   powershell -ExecutionPolicy Bypass -File tools\fidelity\word-fidelity.ps1 `
#       -Tool build\word-fidelity\wordforge_roundtrip.exe [-Corpus out\fidelity\corpus]
#
# Writes out\fidelity\<timestamp>\results.csv and report.md. Needs Microsoft Word; starts its
# own hidden Word instance and quits only it.
param(
    [Parameter(Mandatory = $true)][string]$Tool,
    [string]$Corpus = "out\fidelity\corpus",
    [string]$Out = "",
    [int]$MaxParagraphs = 200
)
$ErrorActionPreference = "Stop"
$Tool = (Resolve-Path $Tool).Path
$Corpus = (Resolve-Path $Corpus).Path
if (-not $Out) { $Out = Join-Path "out\fidelity" (Get-Date -Format "yyyy-MM-dd_HHmmss") }
New-Item -ItemType Directory -Force (Join-Path $Out "wordforge") | Out-Null
$Out = (Resolve-Path $Out).Path
# The tool needs the toolchain's DLLs next to it or on PATH (as for the tests).
$deps = "C:\dev\wordforge-deps\msys64\clangarm64\bin"
if (Test-Path $deps) { $env:PATH = "$deps;$env:PATH" }
$env:QT_QPA_PLATFORM = "offscreen"

$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0
$wordVersion = "$($word.Version) (build $($word.Build))"
$word.Options.CheckSpellingAsYouType = $false
$word.Options.CheckGrammarAsYouType = $false

function Clean([string]$text) {
    # Cell and row marks, field and object placeholders, line/paragraph breaks.
    return (($text -replace "[\x07\x01\x02\x13\x14\x15\x0b\x0c\r\n\t]", " ") -replace "\s+", " ").Trim()
}
function Get-WordFacts([string]$path) {
    $d = $word.Documents.Open([string]$path, $false, $true, $false)
    try {
        $text = Clean $d.Content.Text
        $notes = @()
        foreach ($s in $d.Sections) {
            foreach ($band in @($s.Headers.Item(1), $s.Footers.Item(1))) {
                if ($band.Exists) { $notes += Clean $band.Range.Text }
            }
        }
        foreach ($n in $d.Footnotes) { $notes += Clean $n.Range.Text }
        foreach ($n in $d.Endnotes) { $notes += Clean $n.Range.Text }
        foreach ($c in $d.Comments) { $notes += Clean $c.Range.Text }
        $signature = @(); $characters = @()
        $count = [Math]::Min($d.Paragraphs.Count, $MaxParagraphs)
        for ($i = 1; $i -le $count; $i++) {
            $p = $d.Paragraphs.Item($i)
            if ($p.Range.Information(12)) { continue } # inside a table: compared by count only
            # Paragraph: style, alignment, indents, spacing and line spacing (points, rounded).
            $signature += "{0}|{1}|{2:N0}|{3:N0}|{4:N0}|{5:N0}|{6:N0}|{7}|{8:N1}" -f $p.Style.NameLocal,
                $p.Alignment, $p.LeftIndent, $p.FirstLineIndent, $p.RightIndent, $p.SpaceBefore,
                $p.SpaceAfter, $p.LineSpacingRule, $p.LineSpacing
            # Characters: up to six words spread over the paragraph.
            $words = $p.Range.Words
            $step = [Math]::Max(1, [int][Math]::Floor($words.Count / 6))
            for ($k = 1; $k -le $words.Count -and $k -le 6 * $step; $k += $step) {
                $f = $words.Item($k).Font
                $characters += "{0}|{1}|{2}|{3}|{4}|{5}|{6:N1}|{7}" -f $f.Name, [int]$f.Bold, [int]$f.Italic,
                    [int]$f.Underline, [int]$f.StrikeThrough, $f.Color, $f.Size,
                    $words.Item($k).HighlightColorIndex
            }
        }
        return [ordered]@{
            words = $d.ComputeStatistics(0); pages = $d.ComputeStatistics(2)
            tables = $d.Tables.Count; images = $d.InlineShapes.Count
            footnotes = $d.Footnotes.Count; endnotes = $d.Endnotes.Count
            comments = $d.Comments.Count; revisions = $d.Revisions.Count
            sections = $d.Sections.Count; fields = $d.Fields.Count; hyperlinks = $d.Hyperlinks.Count
            text = $text; notes = ($notes -join " "); signature = $signature; characters = $characters
        }
    } finally {
        $d.Close(0)
    }
}
function Retained([string]$before, [string]$after) {
    $a = @{}
    foreach ($w in ($before -split " ")) { if ($w) { $a[$w] = 1 + [int]$a[$w] } }
    $total = 0; foreach ($v in $a.Values) { $total += $v }
    if ($total -eq 0) { return 1.0 }
    $b = @{}
    foreach ($w in ($after -split " ")) { if ($w) { $b[$w] = 1 + [int]$b[$w] } }
    $kept = 0
    foreach ($k in $a.Keys) { $kept += [Math]::Min($a[$k], [int]$b[$k]) }
    return $kept / $total
}

$structures = @("tables", "images", "footnotes", "endnotes", "comments", "revisions", "sections", "fields", "hyperlinks")
$rows = @()
$warningsSeen = @{}
foreach ($file in Get-ChildItem $Corpus -Filter *.docx | Sort-Object Name) {
    $row = [ordered]@{ file = $file.Name; opened = $false; text = 0.0; notes = 0.0; format = 0.0; characters = 0.0; pages = $false;
        structure = 0.0; pagesWord = 0; pagesAfter = 0; pagesWordforge = 0; lost = ""; error = "" }
    try {
        $before = Get-WordFacts $file.FullName
        $saved = Join-Path $Out "wordforge\$($file.Name)"
        $json = Join-Path $Out "wordforge\$($file.BaseName).json"
        & $Tool $file.FullName $saved $json | Out-Null
        $report = Get-Content $json -Raw | ConvertFrom-Json
        if (-not $report.ok) { throw "Wordforge could not open it: $($report.error)" }
        foreach ($w in $report.warnings) { $warningsSeen[$w] = 1 + [int]$warningsSeen[$w] }
        $row.pagesWordforge = $report.pages
        $after = Get-WordFacts $saved
        $row.opened = $true
        $row.text = [Math]::Round((Retained $before.text $after.text), 4)
        $row.notes = [Math]::Round((Retained $before.notes $after.notes), 4)
        $same = 0; $lost = @()
        foreach ($s in $structures) {
            if ($before[$s] -eq $after[$s]) { $same++ } else { $lost += "$s $($before[$s])->$($after[$s])" }
        }
        $row.structure = [Math]::Round($same / $structures.Count, 4)
        $row.lost = $lost -join "; "
        $n = [Math]::Max($before.signature.Count, 1)
        $match = 0
        for ($i = 0; $i -lt [Math]::Min($before.signature.Count, $after.signature.Count); $i++) {
            if ($before.signature[$i] -eq $after.signature[$i]) { $match++ }
        }
        $row.format = [Math]::Round($match / $n, 4)
        $n = [Math]::Max($before.characters.Count, 1)
        $match = 0
        for ($i = 0; $i -lt [Math]::Min($before.characters.Count, $after.characters.Count); $i++) {
            if ($before.characters[$i] -eq $after.characters[$i]) { $match++ }
        }
        $row.characters = [Math]::Round($match / $n, 4)
        $row.pages = $before.pages -eq $after.pages
        $row.pagesWord = $before.pages
        $row.pagesAfter = $after.pages
    } catch {
        $row.error = ("line {0}: {1}" -f $_.InvocationInfo.ScriptLineNumber, $_.Exception.Message) -replace "[\r\n]+", " "
    }
    $rows += [pscustomobject]$row
    Write-Output ("{0,-40} opened={1} text={2} format={3} structure={4} {5}" -f $file.Name, $row.opened,
        $row.text, $row.format, $row.structure, $row.error)
}
$word.Quit()
[void][Runtime.InteropServices.Marshal]::ReleaseComObject($word)

$rows | Export-Csv (Join-Path $Out "results.csv") -NoTypeInformation -Encoding UTF8
$total = $rows.Count
$opened = @($rows | Where-Object opened).Count
function Mean($values) { if (@($values).Count -eq 0) { return 0 } return ($values | Measure-Object -Average).Average }
$openRows = @($rows | Where-Object opened)
$textMean = Mean ($openRows | ForEach-Object text)
$notesMean = Mean ($openRows | ForEach-Object notes)
$formatMean = Mean ($openRows | ForEach-Object format)
$charactersMean = Mean ($openRows | ForEach-Object characters)
$pagesMean = if ($openRows.Count) { @($openRows | Where-Object pages).Count / $openRows.Count } else { 0 }
$structureMean = Mean ($openRows | ForEach-Object structure)
$score = if ($total) { 100 * ($opened / $total) * ($textMean + $notesMean + $formatMean + $charactersMean + $structureMean + $pagesMean) / 6 } else { 0 }
$lines = @("# Word fidelity report", "",
    "Generated $(Get-Date -Format 'yyyy-MM-dd HH:mm') with Microsoft Word $wordVersion on $total documents from ``$Corpus``.", "",
    "| Measure | Result |", "|---|---|",
    "| Opened in Word after Wordforge saved them | $opened / $total |",
    ("| Body text kept (mean) | {0:P1} |" -f $textMean),
    ("| Header, footer, note and comment text kept (mean) | {0:P1} |" -f $notesMean),
    ("| Paragraphs with the same style, alignment, indents, spacing and line spacing (mean) | {0:P1} |" -f $formatMean),
    ("| Sampled words with the same font, size, emphasis, colour and highlight (mean) | {0:P1} |" -f $charactersMean),
    ("| Same page count in Word | {0:P1} |" -f $pagesMean),
    ("| Structure counts unchanged (mean of 9) | {0:P1} |" -f $structureMean),
    ("| Overall score | {0:N0} / 100 |" -f $score), "",
    "## What changed most often", "")
$losses = @{}
foreach ($r in $rows) { foreach ($l in ($r.lost -split "; ")) { if ($l) { $key = ($l -split " ")[0]; $losses[$key] = 1 + [int]$losses[$key] } } }
foreach ($k in ($losses.Keys | Sort-Object { -$losses[$_] })) { $lines += "* $k changed in $($losses[$k]) documents" }
$lines += @("", "## Wordforge import warnings", "")
foreach ($k in ($warningsSeen.Keys | Sort-Object { -$warningsSeen[$_] })) { $lines += "* $($warningsSeen[$k]) x $k" }
$lines += @("", "## Lowest-scoring documents", "", "| File | Text | Paragraphs | Characters | Structure | Pages | Changes |", "|---|---|---|---|---|---|---|")
foreach ($r in ($rows | Sort-Object { if ($_.opened) { $_.text + $_.format + $_.characters + $_.structure } else { -1 } } | Select-Object -First 20)) {
    $lines += "| $($r.file) | $($r.text) | $($r.format) | $($r.characters) | $($r.structure) | $($r.pagesWord)->$($r.pagesAfter) | $($r.lost)$($r.error) |"
}
$lines | Set-Content (Join-Path $Out "report.md") -Encoding UTF8
Write-Output ("Score {0:N0}/100; opened {1}/{2}. Report: {3}" -f $score, $opened, $total, (Join-Path $Out "report.md"))

