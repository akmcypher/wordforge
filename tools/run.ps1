param(
    [string]$DependencyRoot = 'C:\dev\wordforge-deps\msys64',
    [string]$Document,
    [switch]$Sample,
    [ValidateSet('Release', 'Debug', 'Preview')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$preset = if ($Configuration -eq 'Debug') { 'dev' } else { $Configuration.ToLowerInvariant() }
$program = Join-Path $projectDirectory "build\$preset\wordforge.exe"
if (-not (Test-Path -LiteralPath $program)) { throw 'Build Wordforge using BUILD.md first.' }
$env:PATH = (Join-Path $DependencyRoot 'clangarm64\bin') + ';' + $env:PATH
$arguments = @()
if ($Sample) { $arguments += '--sample' }
if ($Document) { $arguments += $Document }
& $program @arguments
exit $LASTEXITCODE
