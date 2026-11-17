# Builds a reproducible DOCX corpus with Microsoft Word itself (COM automation), so each
# file is exactly what Word writes. Each document exercises one or more features, from
# what Wordforge supports to what it does not yet (sections, columns, fields, endnotes).
#
#   powershell -ExecutionPolicy Bypass -File tools\fidelity\make-word-corpus.ps1 [-Out out\fidelity\corpus] [-Seed 7]
#
# Needs Microsoft Word on Windows. Starts its own hidden Word instance and quits only it.
param(
    [string]$Out = "out\fidelity\corpus",
    [int]$Seed = 7,
    [int]$Limit = 0, # build only the first N documents (0: all)
    [string]$Log = "" # append each step to this file as it happens
)
function Step([string]$message) {
    if ($Log) { Add-Content -Path $Log -Value ("{0:HH:mm:ss.fff} {1}" -f (Get-Date), $message) }
}
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$random = New-Object System.Random $Seed

# Pictures to insert: a PNG and a JPEG drawn here.
Add-Type -AssemblyName System.Drawing
$pictures = @()
foreach ($kind in @("png", "jpg")) {
    $path = Join-Path $Out "picture.$kind"
    $bitmap = New-Object System.Drawing.Bitmap 240, 160
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::FromArgb(242, 230, 208))
    $graphics.FillEllipse([System.Drawing.Brushes]::SteelBlue, 30, 20, 120, 120)
    $graphics.FillRectangle([System.Drawing.Brushes]::IndianRed, 130, 60, 90, 80)
    $graphics.Dispose()
    $format = if ($kind -eq "png") { [System.Drawing.Imaging.ImageFormat]::Png } else { [System.Drawing.Imaging.ImageFormat]::Jpeg }
    $bitmap.Save($path, $format)
    $bitmap.Dispose()
    $pictures += $path
}

$vocabulary = ("the of and to in a is that for it as was with be by on not he this are or his from at which " +
    "but have an they you were her she there been one all we their has would when if so no will more can " +
    "document layout table paragraph review chapter section report figure result method analysis budget " +
    "project quarterly summary customer contract schedule meeting proposal design system").Split(" ")
function Sentence([int]$min = 6, [int]$max = 18) {
    $count = $random.Next($min, $max)
    $words = @()
    for ($i = 0; $i -lt $count; $i++) { $words += $vocabulary[$random.Next($vocabulary.Length)] }
    $text = ($words -join " ")
    return $text.Substring(0, 1).ToUpper() + $text.Substring(1) + "."
}
function ParagraphText([int]$sentences = 3) {
    return ((1..$sentences | ForEach-Object { Sentence }) -join " ")
}

Step "starting Word"
$word = New-Object -ComObject Word.Application
Step "Word started"
$word.Visible = $false
$word.DisplayAlerts = 0
$word.Options.CheckSpellingAsYouType = $false
$word.Options.CheckGrammarAsYouType = $false

function Style($document, [int]$builtin) { return $document.Styles.Item($builtin).NameLocal }
# Appends a paragraph at the end with `text` and returns its range.
function Add($document, [string]$text, $style = $null) {
    $end = $document.Content
    $end.InsertParagraphAfter()
    $range = $document.Paragraphs.Last.Range
    $range.Text = $text
    $range = $document.Paragraphs.Last.Range
    if ($style) { $range.Style = $style }
    return $range
}

# Features: each adds content to the document.
$features = [ordered]@{
    "text"        = { param($d) 1..3 | ForEach-Object { [void](Add $d (ParagraphText 4)) } }
    "headings"    = { param($d)
        [void](Add $d "Report title" (Style $d -63))
        foreach ($n in 1..3) {
            [void](Add $d "Chapter $n" (Style $d -2))
            [void](Add $d (ParagraphText 3))
            [void](Add $d "Section $n.1" (Style $d -3))
            [void](Add $d (ParagraphText 2))
        } }
    "character"   = { param($d)
        $range = Add $d (ParagraphText 4)
        $words = $range.Words
        for ($i = 1; $i -le [Math]::Min($words.Count, 30); $i += 3) {
            $w = $words.Item($i)
            switch ($i % 6) {
                0 { $w.Bold = $true } 1 { $w.Italic = $true } 2 { $w.Underline = 1 }
                3 { $w.Font.Color = 0x2040C0 } 4 { $w.HighlightColorIndex = 7 } 5 { $w.Font.Size = 16 }
            }
        }
        $range = Add $d (ParagraphText 2)
        $range.Font.Name = "Times New Roman"
        $range = Add $d (ParagraphText 1)
        $range.Font.StrikeThrough = $true }
    "lists"       = { param($d)
        foreach ($i in 1..4) { [void](Add $d (Sentence 3 8) (Style $d -49)) }
        foreach ($i in 1..4) { [void](Add $d (Sentence 3 8) (Style $d -50)) }
        [void](Add $d (ParagraphText 1)) }
    "multilevel"  = { param($d)
        $first = $null
        foreach ($i in 1..6) {
            $r = Add $d (Sentence 3 8)
            if (-not $first) { $first = $r.Start }
        }
        $range = $d.Range($first, $d.Content.End)
        $range.ListFormat.ApplyListTemplate($word.ListGalleries.Item(3).ListTemplates.Item(1))
        $d.Paragraphs.Item($d.Paragraphs.Count - 3).Range.ListFormat.ListIndent()
        $d.Paragraphs.Item($d.Paragraphs.Count - 2).Range.ListFormat.ListIndent()
        [void](Add $d (ParagraphText 1) (Style $d -1)) }
    "alignment"   = { param($d)
        foreach ($a in 0, 1, 2, 3) { $r = Add $d (ParagraphText 2); $r.ParagraphFormat.Alignment = $a } }
    "indents"     = { param($d)
        $r = Add $d (ParagraphText 3); $r.ParagraphFormat.LeftIndent = 36
        $r = Add $d (ParagraphText 3); $r.ParagraphFormat.FirstLineIndent = 24
        $r = Add $d (ParagraphText 3); $r.ParagraphFormat.LeftIndent = 36; $r.ParagraphFormat.FirstLineIndent = -18
        $r = Add $d (ParagraphText 3); $r.ParagraphFormat.RightIndent = 48 }
    "spacing"     = { param($d)
        $r = Add $d (ParagraphText 3); $r.ParagraphFormat.SpaceBefore = 18; $r.ParagraphFormat.SpaceAfter = 12
        $r = Add $d (ParagraphText 3); $r.ParagraphFormat.LineSpacingRule = 1
        $r = Add $d (ParagraphText 3); $r.ParagraphFormat.LineSpacingRule = 2 }
    "pagebreak"   = { param($d)
        [void](Add $d (ParagraphText 2))
        $d.Paragraphs.Last.Range.InsertBreak(7)
        [void](Add $d (ParagraphText 2)) }
    "table"       = { param($d)
        [void](Add $d "A table follows.")
        $t = $d.Tables.Add($d.Paragraphs.Last.Range, 3 + $random.Next(3), 2 + $random.Next(3))
        $t.Borders.Enable = 1
        for ($r = 1; $r -le $t.Rows.Count; $r++) { for ($c = 1; $c -le $t.Columns.Count; $c++) {
            $t.Cell($r, $c).Range.Text = (Sentence 1 4) } }
        [void](Add $d (ParagraphText 1)) }
    "tablemerge"  = { param($d)
        [void](Add $d "Merged cells follow.")
        $t = $d.Tables.Add($d.Paragraphs.Last.Range, 4, 4)
        $t.Borders.Enable = 1
        for ($r = 1; $r -le 4; $r++) { for ($c = 1; $c -le 4; $c++) { $t.Cell($r, $c).Range.Text = "r$r c$c" } }
        $t.Cell(1, 1).Merge($t.Cell(1, 2))
        $t.Cell(2, 3).Merge($t.Cell(3, 3))
        [void](Add $d (ParagraphText 1)) }
    "tablestyle"  = { param($d)
        [void](Add $d "Shaded table.")
        $t = $d.Tables.Add($d.Paragraphs.Last.Range, 3, 3)
        $t.Borders.Enable = 1
        $t.Borders.OutsideLineWidth = 12
        $t.Borders.OutsideColor = 0x993366
        $t.Cell(1, 1).Shading.BackgroundPatternColor = 0xCCEEFF
        $t.Cell(2, 2).Shading.BackgroundPatternColor = 0xD0E0F2
        $t.Columns.Item(1).Width = 60
        for ($r = 1; $r -le 3; $r++) { for ($c = 1; $c -le 3; $c++) { $t.Cell($r, $c).Range.Text = (Sentence 1 3) } }
        [void](Add $d "Table without borders.")
        $t = $d.Tables.Add($d.Paragraphs.Last.Range, 2, 2)
        $t.Borders.Enable = 0
        $t.Cell(1, 1).Range.Text = "plain"
        [void](Add $d (ParagraphText 1)) }
    "image"       = { param($d)
        $r = Add $d "Picture: "
        $r.Collapse(0); $r.Move(1, -1) | Out-Null
        [void]$d.InlineShapes.AddPicture([string]$pictures[$random.Next(2)], $false, $true, $r)
        [void](Add $d (ParagraphText 1)) }
    "footnotes"   = { param($d)
        foreach ($i in 1..3) {
            $r = Add $d (ParagraphText 2)
            $at = $d.Range($r.End - 1, $r.End - 1)
            [void]$d.Footnotes.Add($at, [Type]::Missing, (Sentence 4 10))
        } }
    "endnotes"    = { param($d)
        $r = Add $d (ParagraphText 2)
        $at = $d.Range($r.End - 1, $r.End - 1)
        [void]$d.Endnotes.Add($at, [Type]::Missing, (Sentence 4 10)) }
    "comments"    = { param($d)
        foreach ($i in 1..2) {
            $r = Add $d (ParagraphText 2)
            [void]$d.Comments.Add($r.Words.Item(2), (Sentence 3 8))
        } }
    "tracked"     = { param($d)
        $r = Add $d (ParagraphText 3)
        $d.TrackRevisions = $true
        $r.Words.Item(3).Delete() | Out-Null
        $r = $d.Paragraphs.Last.Range
        $r.InsertAfter(" Added while tracking.")
        $d.TrackRevisions = $false }
    "header"      = { param($d)
        $section = $d.Sections.Item(1)
        $section.Headers.Item(1).Range.Text = "Company confidential"
        [void]$section.Footers.Item(1).PageNumbers.Add(1, $true)
        [void](Add $d (ParagraphText 3)) }
    "sections"    = { param($d)
        [void](Add $d (ParagraphText 2))
        $d.Paragraphs.Last.Range.InsertBreak(2)
        [void](Add $d (ParagraphText 2))
        $d.Sections.Last.PageSetup.Orientation = 1 }
    "columns"     = { param($d)
        [void](Add $d (ParagraphText 6))
        $d.PageSetup.TextColumns.SetCount(2) }
    "toc"         = { param($d)
        $r = $d.Range(0, 0)
        [void]$d.TablesOfContents.Add($r)
        [void](Add $d "Introduction" (Style $d -2))
        [void](Add $d (ParagraphText 2))
        [void](Add $d "Results" (Style $d -2))
        [void](Add $d (ParagraphText 2))
        $d.TablesOfContents.Item(1).Update() }
    "hyperlink"   = { param($d)
        $r = Add $d "Visit the project page for details."
        [void]$d.Hyperlinks.Add($r.Words.Item(4), "https://example.org/project") }
    "long"        = { param($d)
        foreach ($chapter in 1..12) {
            [void](Add $d "Chapter $chapter" (Style $d -2))
            foreach ($p in 1..14) { [void](Add $d (ParagraphText 5)) }
        } }
}
$names = @($features.Keys)
$supported = @("text", "headings", "character", "lists", "alignment", "spacing", "pagebreak", "table",
    "tablemerge", "tablestyle", "image", "footnotes", "comments", "tracked", "header")

# Plan: each feature alone in three variants, then mixed documents.
$plan = @()
foreach ($name in $names) { foreach ($variant in 1..3) { $plan += , @("text", $name) } }
for ($i = 0; $i -lt 30; $i++) {
    $pick = @("headings") + (1..(3 + $random.Next(4)) | ForEach-Object { $supported[$random.Next($supported.Count)] })
    $plan += , ($pick | Select-Object -Unique)
}
for ($i = 0; $i -lt 15; $i++) {
    $pick = @("headings") + (1..(3 + $random.Next(4)) | ForEach-Object { $names[$random.Next($names.Count)] })
    $plan += , ($pick | Select-Object -Unique)
}

$index = 0
$failures = 0
if ($Limit -gt 0) { $plan = $plan | Select-Object -First $Limit }
foreach ($set in $plan) {
    $index++
    $file = Join-Path $Out ("{0:D3}-{1}.docx" -f $index, (($set | Where-Object { $_ -ne "text" }) -join "+"))
    $document = $word.Documents.Add()
    try {
        $document.Content.Text = (ParagraphText 2)
        foreach ($name in $set) {
            Step "$file : $name"
            $started = Get-Date
            & $features[$name] $document
            $seconds = ((Get-Date) - $started).TotalSeconds
            if ($seconds -gt 20) { Write-Warning ("{0}: {1} took {2:N0} s" -f $file, $name, $seconds) }
        }
        Step "$file : saving"
        $document.SaveAs2([string]$file, 16)
        Step "$file : saved"
        Write-Output ("{0:D3} {1}" -f $index, (Split-Path $file -Leaf))
    } catch {
        $failures++
        Write-Warning "$file : $($_.Exception.Message)"
    } finally {
        $document.Close(0)
    }
}
$word.Quit()
[void][Runtime.InteropServices.Marshal]::ReleaseComObject($word)
Write-Output "Built $($index - $failures) documents in $Out ($failures failed)"

