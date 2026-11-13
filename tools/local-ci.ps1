<#
.SYNOPSIS
  Local CI for Wordforge: Windows build and tests, then Ubuntu (WSL) build and
  tests, optionally an Ubuntu snap. Uses no hosted CI minutes.
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\local-ci.ps1
  powershell -ExecutionPolicy Bypass -File tools\local-ci.ps1 -Snap
  powershell -ExecutionPolicy Bypass -File tools\local-ci.ps1 -SkipWindows
.NOTES
  Logs go to out\local-ci\<timestamp>\ (ignored by git). Exit code 0 means every
  selected stage passed. A running wordforge.exe from the build folder is never
  closed; pass -BuildDir to build elsewhere.
#>
param(
    [switch]$SkipWindows,
    [switch]$SkipUbuntu,
    [switch]$Snap,
    [string]$BuildDir = "build\local-ci",
    [string]$MsysBin = $(if ($env:WORDFORGE_MSYS2_BIN) { $env:WORDFORGE_MSYS2_BIN } else { "C:\dev\wordforge-deps\msys64\clangarm64\bin" }),
    [string]$Distro = $(if ($env:WORDFORGE_WSL_DISTRO) { $env:WORDFORGE_WSL_DISTRO } else { "WordforgeUbuntu" }),
    [string]$WslUser = "root"
)
# Native tools write warnings to stderr; do not turn those into PowerShell errors.
$ErrorActionPreference = "Continue"
$source = Split-Path -Parent $PSScriptRoot
$logs = Join-Path $source ("out\local-ci\" + (Get-Date -Format "yyyy-MM-dd_HHmmss"))
New-Item -ItemType Directory -Force $logs | Out-Null
$results = [ordered]@{}

function ToWsl([string]$path) {
    $full = [System.IO.Path]::GetFullPath($path)
    "/mnt/" + $full.Substring(0, 1).ToLower() + $full.Substring(2).Replace("\", "/")
}

if (-not $SkipWindows) {
    Write-Host "== Windows: configure and build ($BuildDir)"
    $build = Join-Path $source $BuildDir
    $locked = Get-Process wordforge -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($build, [StringComparison]::OrdinalIgnoreCase) }
    if ($locked) {
        Write-Host "wordforge.exe is running from $build; close it or pass -BuildDir." -ForegroundColor Yellow
        $results["Windows"] = "SKIPPED (executable in use)"
    } else {
        $env:PATH = "$MsysBin;$env:PATH"
        $buildLog = Join-Path $logs "windows-build.txt"
        & cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=$MsysBin\c++.exe" *> $buildLog
        if ($LASTEXITCODE -eq 0) { & cmake --build $build --parallel *>> $buildLog }
        if ($LASTEXITCODE -ne 0) {
            Select-String -Path $buildLog -Pattern "error|FAILED" | Select-Object -First 20 | ForEach-Object { $_.Line }
            $results["Windows"] = "BUILD FAILED"
        } else {
            Write-Host "== Windows: tests"
            $testLog = Join-Path $logs "windows-tests.txt"
            & ctest --test-dir $build -V *> $testLog
            $ok = $LASTEXITCODE -eq 0
            Select-String -Path $testLog -Pattern "tests passed|FAIL!" | Select-Object -Last 5 | ForEach-Object { $_.Line }
            $results["Windows"] = $(if ($ok) { "PASSED" } else { "TESTS FAILED" })
        }
    }
}

if (-not $SkipUbuntu) {
    Write-Host "== Ubuntu ($Distro)"
    $script = ToWsl (Join-Path $source "tools\local-ci-ubuntu.sh")
    $arguments = @("-d", $Distro, "-u", $WslUser, "--", "bash", $script, (ToWsl $logs))
    if ($Snap) { $arguments += "--snap" }
    & wsl.exe @arguments
    $results["Ubuntu"] = $(if ($LASTEXITCODE -eq 0) { "PASSED" } else { "FAILED" })
}

Write-Host ""
Write-Host "== Summary (logs: $logs)"
$failed = $false
foreach ($stage in $results.Keys) {
    $state = $results[$stage]
    $color = if ($state -eq "PASSED") { "Green" } elseif ($state -like "SKIPPED*") { "Yellow" } else { "Red" }
    Write-Host ("{0,-8} {1}" -f $stage, $state) -ForegroundColor $color
    if ($state -ne "PASSED") { $failed = $true }
}
if ($failed) { exit 1 } else { exit 0 }
