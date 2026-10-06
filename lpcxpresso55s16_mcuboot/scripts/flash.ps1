<#
.SYNOPSIS
    Flash the MCUboot bootloader to the LPCXpresso55S16 board with NXP LinkServer.

.DESCRIPTION
    Programs the built image (default: ..\Debug\lpcxpresso55s16_mcuboot.axf) into the
    LPC55S16 internal flash through the on-board debug probe (LPC-Link2 / MCU-Link), then resets
    the board so the firmware starts running.

    Build the project in MCUXpresso IDE first - the Debug folder is not stored in git.

.PARAMETER Image
    Firmware file to flash (.axf/.elf/.hex/.srec). Relative paths are resolved from the project root.

.PARAMETER Config
    Build configuration folder used when -Image is not given (Debug or Release).

.PARAMETER Device
    LinkServer device name.

.PARAMETER Probe
    Probe serial number (or substring), or '#<index>'. Only needed when several probes are connected.

.PARAMETER EraseAll
    Mass-erase the whole flash before programming.

.PARAMETER NoReset
    Do not start the firmware after programming.

.PARAMETER LinkServer
    Full path to LinkServer.exe. By default the newest C:\nxp\LinkServer_* install is used.

.EXAMPLE
    .\scripts\flash.ps1
    Flash Debug\lpcxpresso55s16_mcuboot.axf.

.EXAMPLE
    .\scripts\flash.ps1 -Config Release -EraseAll
#>
[CmdletBinding()]
param(
    [string]$Image,
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',
    [string]$Device = 'LPC55S16',
    [string]$Probe,
    [switch]$EraseAll,
    [switch]$NoReset,
    [string]$LinkServer
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProjectName = Split-Path -Leaf $ProjectRoot

# --- Locate LinkServer -------------------------------------------------------
if (-not $LinkServer) {
    $cmd = Get-Command LinkServer.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        $LinkServer = $cmd.Source
    } else {
        $install = Get-ChildItem -Path 'C:\nxp' -Directory -Filter 'LinkServer_*' -ErrorAction SilentlyContinue |
            Sort-Object { [version]($_.Name -replace '^LinkServer_', '') } -Descending |
            Select-Object -First 1
        if ($install) { $LinkServer = Join-Path $install.FullName 'LinkServer.exe' }
    }
}
if (-not $LinkServer -or -not (Test-Path $LinkServer)) {
    throw "LinkServer.exe not found. Install NXP LinkServer or pass -LinkServer <path>."
}

# --- Locate the firmware image -----------------------------------------------
if (-not $Image) {
    $Image = Join-Path $ProjectRoot "$Config\$ProjectName.axf"
} elseif (-not [System.IO.Path]::IsPathRooted($Image)) {
    $Image = Join-Path $ProjectRoot $Image
}
if (-not (Test-Path $Image)) {
    throw "Firmware image not found: $Image`nBuild the project in MCUXpresso IDE ($Config configuration) first."
}
$Image = (Resolve-Path $Image).Path

# --- Flash -------------------------------------------------------------------
$linkArgs = @('flash')
if ($Probe) { $linkArgs += @('--probe', $Probe) }
$linkArgs += @($Device, 'load')
if ($EraseAll) { $linkArgs += '--erase-all' }
if ($NoReset)  { $linkArgs += '--no-reset' }
$linkArgs += $Image

$built = (Get-Item $Image).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
Write-Host "LinkServer : $LinkServer"
Write-Host "Device     : $Device"
Write-Host "Image      : $Image (built $built)"
Write-Host ''

# LinkServer logs progress on stderr; Windows PowerShell 5.1 would turn that into a
# terminating error under 'Stop', so rely on the exit code instead.
$ErrorActionPreference = 'Continue'
& $LinkServer @linkArgs 2>&1 | ForEach-Object { Write-Host "$_" }
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) {
    Write-Host ''
    Write-Host "Flashing FAILED (LinkServer exit code $LASTEXITCODE)." -ForegroundColor Red
    Write-Host 'Check the USB cable on the debug port (J1), and close any running debug session in the IDE.'
    exit $LASTEXITCODE
}

Write-Host ''
Write-Host 'Flashing done.' -ForegroundColor Green
if (-not $NoReset) {
    Write-Host 'Open a serial terminal at 115200 8N1 and press RESET to see the MCUboot log ("hello sbl.").'
}
