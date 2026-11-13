# Checks a portable Windows package the way a user's machine would see it: unzips it into a
# folder whose name needs Unicode file APIs, removes every development folder from PATH and
# every Qt override from the environment, then runs Wordforge.exe --self-test.
# Usage: tools\test-windows-package.ps1 -Package out\Wordforge-0.6.1-windows-x64.zip
param([Parameter(Mandatory)][string]$Package)
$ErrorActionPreference = 'Stop'
$zip = (Resolve-Path -LiteralPath $Package).Path
$expected = (Get-Content -LiteralPath "$zip.sha256" -ErrorAction SilentlyContinue) -split '\s+' | Select-Object -First 1
if ($expected) {
    $actual = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw "checksum mismatch: $actual, expected $expected" }
    Write-Output "checksum ok"
}

$root = Join-Path ([IO.Path]::GetTempPath()) ("Wordforge test " + [char]0x0442 + [char]0x0435 + [char]0x0441 + [char]0x0442 + " " + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    Expand-Archive -LiteralPath $zip -DestinationPath $root
    $app = Get-ChildItem -LiteralPath $root -Directory | Select-Object -First 1
    $exe = Join-Path $app.FullName 'Wordforge.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw "Wordforge.exe missing from $zip" }
    foreach ($required in 'platforms\qwindows.dll', 'imageformats\qjpeg.dll', 'styles', 'dictionaries\en_US.dic',
                          'dictionaries\en_US.aff', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'BUILD-INFO.txt') {
        if (-not (Test-Path -LiteralPath (Join-Path $app.FullName $required))) { throw "missing $required" }
    }

    $systemRoot = $env:SystemRoot
    $env:PATH = "$systemRoot\System32;$systemRoot;$systemRoot\System32\Wbem"
    Get-ChildItem Env: | Where-Object { $_.Name -like 'QT_*' -or $_.Name -like 'WORDFORGE_*' } |
        ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }

    $report = Join-Path $root 'self-test.txt'
    $process = Start-Process -FilePath $exe -ArgumentList @('--self-test', "`"$report`"") -PassThru -Wait
    if (Test-Path -LiteralPath $report) { Get-Content -LiteralPath $report -Encoding UTF8 }
    else { Write-Output "no report written (exit code $($process.ExitCode)); a DLL or plugin is likely missing" }
    if ($process.ExitCode -ne 0) { throw "self-test failed with exit code $($process.ExitCode)" }
    Write-Output "package test passed: $zip"
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
