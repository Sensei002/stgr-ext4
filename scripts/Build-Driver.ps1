#Requires -Version 5.1
<#
.SYNOPSIS
    Builds the STGR EXT4 kernel driver for one configuration.

.DESCRIPTION
    This script is used by GitHub Actions and can be run locally after the
    pinned NuGet packages have been restored:

        nuget restore .\packages.config -PackagesDirectory .\packages\

    It does not download packages, does not sign the driver, and does not
    install anything. The output is an unsigned development build.

.PARAMETER Configuration
    Debug or Release.

.PARAMETER Platform
    x64. Only x64 is supported in Milestone 1.

.PARAMETER RunCodeAnalysis
    Also run MSVC code analysis (/analyze). Advisory in Milestone 1.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [switch]$RunCodeAnalysis
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repoRoot 'driver\STGRExt4.vcxproj'
$wdkPackage = Join-Path $repoRoot 'packages\Microsoft.Windows.WDK.x64.10.0.26100.6584'

if (-not (Test-Path -LiteralPath $project)) {
    throw "Driver project not found: $project"
}

if (-not (Test-Path -LiteralPath $wdkPackage)) {
    throw @"
The pinned WDK NuGet package is missing.

Run:
    nuget restore .\packages.config -PackagesDirectory .\packages\

Expected:
    $wdkPackage
"@
}

if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) {
    throw "msbuild.exe is not on PATH. In CI, microsoft/setup-msbuild provides it."
}

$buildArgs = @(
    $project
    '/m'
    '/nologo'
    "/p:Configuration=$Configuration"
    "/p:Platform=$Platform"
    "/p:SolutionDir=$repoRoot\"
)

if ($RunCodeAnalysis) {
    $buildArgs += '/p:RunCodeAnalysis=true'
    Write-Host 'Code analysis: enabled (advisory)'
}

Write-Host "Building $Configuration|$Platform"
Write-Host "  project: $project"
Write-Host "  WDK:     $wdkPackage"

& msbuild @buildArgs
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE"
}

& (Join-Path $PSScriptRoot 'Stage-DriverPackage.ps1') -Configuration $Configuration -Platform $Platform

if ($RunCodeAnalysis) {
    Write-Host 'Code analysis finished. Treat the output as advisory until the baseline is clean.'
}
