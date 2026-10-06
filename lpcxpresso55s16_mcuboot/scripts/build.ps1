<#
.SYNOPSIS
    Build the MCUboot bootloader from the command line with MCUXpresso IDE in headless mode.

.DESCRIPTION
    Runs the MCUXpresso IDE command-line builder (mcuxpressoidec.exe, Eclipse CDT headless build)
    on this project, so no IDE window is needed. The output is written to the same place as an IDE
    build: <Config>\lpcxpresso55s16_mcuboot.axf.

    The headless build uses its own private workspace (under %LOCALAPPDATA%\mcux-headless), so it
    also works while MCUXpresso IDE is open on your normal workspace.

.PARAMETER Config
    Build configuration: Debug or Release.

.PARAMETER Clean
    Clean before building (full rebuild).

.PARAMETER Flash
    Flash the board with scripts\flash.ps1 after a successful build.

.PARAMETER McuxIde
    Full path to mcuxpressoidec.exe. By default the newest C:\nxp\MCUXpressoIDE_* install is used.

.EXAMPLE
    .\scripts\build.ps1
    Incremental Debug build.

.EXAMPLE
    .\scripts\build.ps1 -Clean -Flash
    Full rebuild, then flash the board.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',
    [switch]$Clean,
    [switch]$Flash,
    [string]$McuxIde
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProjectName = Split-Path -Leaf $ProjectRoot

# --- Locate the MCUXpresso IDE command-line launcher -------------------------
if (-not $McuxIde) {
    $install = Get-ChildItem -Path 'C:\nxp' -Directory -Filter 'MCUXpressoIDE_*' -ErrorAction SilentlyContinue |
        Sort-Object { [version](($_.Name -replace '^MCUXpressoIDE_', '') -replace '[^0-9.]', '') } -Descending |
        Select-Object -First 1
    if ($install) { $McuxIde = Join-Path $install.FullName 'ide\mcuxpressoidec.exe' }
}
if (-not $McuxIde -or -not (Test-Path $McuxIde)) {
    throw "mcuxpressoidec.exe not found. Install MCUXpresso IDE or pass -McuxIde <path>."
}

# --- Private headless workspace (one per project location) -------------------
$md5 = [System.Security.Cryptography.MD5]::Create()
$hash = -join ($md5.ComputeHash([Text.Encoding]::UTF8.GetBytes($ProjectRoot.ToLower())) |
    Select-Object -First 4 | ForEach-Object { $_.ToString('x2') })
$Workspace = Join-Path $env:LOCALAPPDATA "mcux-headless\$ProjectName-$hash"
New-Item -ItemType Directory -Force -Path $Workspace | Out-Null
$imported = Test-Path (Join-Path $Workspace ".metadata\.plugins\org.eclipse.core.resources\.projects\$ProjectName")

# --- Build -------------------------------------------------------------------
$ideArgs = @('-nosplash', '--launcher.suppressErrors',
    '-application', 'org.eclipse.cdt.managedbuilder.core.headlessbuild',
    '-data', $Workspace)
if (-not $imported) { $ideArgs += @('-import', $ProjectRoot) }
$ideArgs += @($(if ($Clean) { '-cleanBuild' } else { '-build' }), "$ProjectName/$Config")

$Image = Join-Path $ProjectRoot "$Config\$ProjectName.axf"
$before = if (Test-Path $Image) { (Get-Item $Image).LastWriteTime } else { [datetime]::MinValue }

Write-Host "MCUXpresso : $McuxIde"
Write-Host "Project    : $ProjectRoot"
Write-Host "Config     : $Config$(if ($Clean) { ' (clean build)' })"
Write-Host "Workspace  : $Workspace"
Write-Host ''

# The headless builder re-serializes the project files (default values, env hashes)
# without any real change; keep them byte-identical so git stays clean.
$keepFiles = @('.cproject', '.settings\language.settings.xml') |
    ForEach-Object { Join-Path $ProjectRoot $_ } | Where-Object { Test-Path $_ }
$saved = @{}
foreach ($f in $keepFiles) { $saved[$f] = [IO.File]::ReadAllBytes($f) }

$timer = [Diagnostics.Stopwatch]::StartNew()
# The builder logs on stderr too; Windows PowerShell 5.1 would turn that into a
# terminating error under 'Stop', so rely on the exit code instead.
$ErrorActionPreference = 'Continue'
try {
    & $McuxIde @ideArgs 2>&1 | ForEach-Object { Write-Host "$_" }
    $exitCode = $LASTEXITCODE
} finally {
    foreach ($f in $saved.Keys) { [IO.File]::WriteAllBytes($f, $saved[$f]) }
}
$ErrorActionPreference = 'Stop'
$timer.Stop()

Write-Host ''
$after = if (Test-Path $Image) { (Get-Item $Image).LastWriteTime } else { [datetime]::MinValue }
if ($exitCode -ne 0 -or $after -eq [datetime]::MinValue) {
    Write-Host "Build FAILED (exit code $exitCode) after $([int]$timer.Elapsed.TotalSeconds) s." -ForegroundColor Red
    Write-Host 'Scroll up for the compiler errors.'
    if ($exitCode -eq 0) { $exitCode = 1 }
    exit $exitCode
}

$state = if ($after -gt $before) { 'built' } else { 'up to date' }
Write-Host ''
Write-Host "Build OK ($state) in $([int]$timer.Elapsed.TotalSeconds) s: $Image" -ForegroundColor Green

if ($Flash) {
    Write-Host ''
    & (Join-Path $PSScriptRoot 'flash.ps1') -Config $Config
    exit $LASTEXITCODE
}
