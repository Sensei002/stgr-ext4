#Requires -Version 5.1
<#
.SYNOPSIS
    Generates the driver catalog with the pinned WDK's Inf2Cat.

.DESCRIPTION
    Inf2Cat validates the INF against the target operating system and writes
    STGRExt4.cat. The catalog is UNSIGNED. This script proves the INF is
    structurally valid; it does not prove the package is installable.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDir
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path -LiteralPath $PackageDir)) {
    throw "Package directory not found: $PackageDir"
}

$candidates = @(Get-ChildItem -Path (Join-Path $repoRoot 'packages') -Recurse -Filter 'Inf2Cat.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -like '*\x86\Inf2Cat.exe' })

if ($candidates.Count -eq 0) {
    throw @"
Inf2Cat.exe was not found under packages\.

Run:
    nuget restore .\packages.config -PackagesDirectory .\packages\
"@
}

$inf2cat = $candidates[0].FullName
Write-Host "Inf2Cat: $inf2cat"
Write-Host "Driver:  $PackageDir"

& $inf2cat "/driver:$PackageDir" /os:10_X64
if ($LASTEXITCODE -ne 0) {
    throw "Inf2Cat failed with exit code $LASTEXITCODE"
}

$catalog = Join-Path $PackageDir 'STGRExt4.cat'
if (-not (Test-Path -LiteralPath $catalog)) {
    throw "Inf2Cat reported success but did not create $catalog"
}

Write-Host "Catalog created: $catalog (unsigned)"
