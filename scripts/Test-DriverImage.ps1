#Requires -Version 5.1
<#
.SYNOPSIS
    Verifies that a built image is a native x64 kernel driver.

.DESCRIPTION
    Uses dumpbin from the Visual Studio installation selected by vswhere.
    A successful build is not proof of a working filesystem, but this check
    catches the common mistake of linking a user-mode DLL or an x86 image.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DriverPath
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $DriverPath)) {
    throw "Driver image not found: $DriverPath"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe not found at $vswhere"
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($vsPath)) {
    throw "No Visual Studio installation with the x64 C++ toolset was found."
}

$msvcRoot = Join-Path $vsPath 'VC\Tools\MSVC'
if (-not (Test-Path -LiteralPath $msvcRoot)) {
    throw "MSVC tools directory not found: $msvcRoot"
}

$msvc = Get-ChildItem -LiteralPath $msvcRoot -Directory |
    Sort-Object Name -Descending |
    Select-Object -First 1

$dumpbin = Join-Path $msvc.FullName 'bin\Hostx64\x64\dumpbin.exe'
if (-not (Test-Path -LiteralPath $dumpbin)) {
    throw "dumpbin.exe not found: $dumpbin"
}

$headers = & $dumpbin /nologo /headers $DriverPath 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed with exit code $LASTEXITCODE"
}

$text = $headers -join "`n"

$machineOk = $text -match 'machine \(x64\)'
$subsystemOk = $text -match 'subsystem \((Windows )?native\)'

$headers | Select-String -Pattern 'machine \(|subsystem \(' | ForEach-Object { Write-Host $_.Line.Trim() }

if (-not $machineOk) {
    throw "The image is not an x64 PE. dumpbin did not report 'machine (x64)'."
}

if (-not $subsystemOk) {
    throw "The image is not a native kernel image. dumpbin did not report a native subsystem."
}

Write-Host "Driver image check passed: x64 native image."
