# Checks a Windows package the way a user's machine would see it. A portable ZIP is unzipped,
# and a setup .exe is installed silently for the current user, into a folder whose name needs
# Unicode file APIs. Every development folder is removed from PATH and every Qt override from
# the environment, then Wordforge.exe --self-test runs. For the installer it also checks the
# .wfg association, then uninstalls and checks that the files and registry keys are gone.
# Usage: tools\test-windows-package.ps1 -Package out\Wordforge-0.6.1-windows-x64.zip
#        tools\test-windows-package.ps1 -Package out\Wordforge-0.6.1-windows-x64-setup.exe
param([Parameter(Mandatory)][string]$Package)
$ErrorActionPreference = 'Stop'
$file = (Resolve-Path -LiteralPath $Package).Path
$installer = $file.EndsWith('.exe', [StringComparison]::OrdinalIgnoreCase)
$expected = (Get-Content -LiteralPath "$file.sha256" -ErrorAction SilentlyContinue) -split '\s+' | Select-Object -First 1
if ($expected) {
    $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw "checksum mismatch: $actual, expected $expected" }
    Write-Output "checksum ok"
}

$root = Join-Path ([IO.Path]::GetTempPath()) ("Wordforge test " + [char]0x0442 + [char]0x0435 + [char]0x0441 + [char]0x0442 + " " + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $root | Out-Null
$classes = 'HKCU:\Software\Classes'
try {
    if ($installer) {
        $appDir = Join-Path $root 'Wordforge'
        $log = Join-Path $root 'setup.log'
        $setup = Start-Process -FilePath $file -PassThru -Wait -ArgumentList @(
            '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', "/DIR=`"$appDir`"", "/LOG=`"$log`"")
        if ($setup.ExitCode -ne 0) { Get-Content -LiteralPath $log -ErrorAction SilentlyContinue; throw "setup failed with exit code $($setup.ExitCode)" }
        $command = (Get-ItemProperty -LiteralPath "$classes\Wordforge.Document\shell\open\command").'(default)'
        if ($command -ne "`"$appDir\Wordforge.exe`" `"%1`"") { throw "unexpected open command: $command" }
        if ((Get-ItemProperty -LiteralPath "$classes\.wfg").'(default)' -ne 'Wordforge.Document') { throw ".wfg is not associated" }
        Write-Output "installed to $appDir; .wfg association ok"
    } else {
        Expand-Archive -LiteralPath $file -DestinationPath $root
        $appDir = (Get-ChildItem -LiteralPath $root -Directory | Select-Object -First 1).FullName
    }
    $exe = Join-Path $appDir 'Wordforge.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw "Wordforge.exe missing from $file" }
    foreach ($required in 'platforms\qwindows.dll', 'imageformats\qjpeg.dll', 'styles', 'dictionaries\en_US.dic',
                          'dictionaries\en_US.aff', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'BUILD-INFO.txt') {
        if (-not (Test-Path -LiteralPath (Join-Path $appDir $required))) { throw "missing $required" }
    }

    $savedPath = $env:PATH
    $systemRoot = $env:SystemRoot
    $env:PATH = "$systemRoot\System32;$systemRoot;$systemRoot\System32\Wbem"
    Get-ChildItem Env: | Where-Object { $_.Name -like 'QT_*' -or $_.Name -like 'WORDFORGE_*' } |
        ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }

    $report = Join-Path $root 'self-test.txt'
    $process = Start-Process -FilePath $exe -ArgumentList @('--self-test', "`"$report`"") -PassThru -Wait
    if (Test-Path -LiteralPath $report) { Get-Content -LiteralPath $report -Encoding UTF8 }
    else { Write-Output "no report written (exit code $($process.ExitCode)); a DLL or plugin is likely missing" }
    if ($process.ExitCode -ne 0) { throw "self-test failed with exit code $($process.ExitCode)" }
    $env:PATH = $savedPath

    if ($installer) {
        $uninstaller = Join-Path $appDir 'unins000.exe'
        $remove = Start-Process -FilePath $uninstaller -PassThru -Wait -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
        if ($remove.ExitCode -ne 0) { throw "uninstall failed with exit code $($remove.ExitCode)" }
        # The uninstaller deletes itself from a temporary copy that may still be running.
        for ($i = 0; $i -lt 30 -and (Test-Path -LiteralPath $exe); $i++) { Start-Sleep -Seconds 1 }
        if (Test-Path -LiteralPath $exe) { throw "Wordforge.exe still present after uninstall" }
        if (Test-Path -LiteralPath "$classes\Wordforge.Document") { throw "Wordforge.Document left in the registry" }
        if ((Get-ItemProperty -LiteralPath "$classes\.wfg" -ErrorAction SilentlyContinue).'(default)') { throw ".wfg association left behind" }
        Write-Output "uninstall ok"
    }
    Write-Output "package test passed: $file"
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
