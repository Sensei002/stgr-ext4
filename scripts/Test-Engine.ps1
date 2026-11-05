#Requires -Version 5.1
<#
.SYNOPSIS
    Compiles and runs the portable EXT4 engine's host unit tests.

.DESCRIPTION
    The engine is freestanding C: no WDK headers, no libc, no kernel calls. It
    is compiled here as a normal host executable with MSVC, at /W4 /WX, and the
    test binary is run. This is test layer L2 in docs/TESTING.md.

    It needs no driver package, no test signing, and no VM. That is the whole
    point: parsing bugs are caught here rather than in a kernel debugger.

.PARAMETER Configuration
    Informational only. The host tests build one way; the parameter exists so
    the script matches the other build scripts.

.PARAMETER CorpusDir
    Directory produced by scripts/New-Ext4Corpus.sh. When supplied, every image
    hash in manifest.sha256 is verified before the tests run, and the binary is
    given --corpus so it parses those real mke2fs images.

.EXAMPLE
    .\scripts\Test-Engine.ps1
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$CorpusDir = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$engineDir = Join-Path $repoRoot 'driver\ext4'
$testDir = Join-Path $engineDir 'tests'
$outputDir = Join-Path $repoRoot "artifacts\engine\$Configuration"
$binary = Join-Path $outputDir 'stgr-ext4-tests.exe'

if (-not (Test-Path -LiteralPath $engineDir)) {
    throw "Engine directory not found: $engineDir"
}

if (-not (Test-Path -LiteralPath $testDir)) {
    throw "Engine test directory not found: $testDir"
}

#
# Find MSVC without requiring a pre-configured developer prompt.
#
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe not found at $vswhere. Install the Visual Studio C++ build tools."
}

$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $vsPath) {
    throw 'No Visual Studio installation with the C++ x64 toolset was found.'
}

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "vcvars64.bat not found at $vcvars"
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

#
# Verify the corpus before compiling: the point of the manifest is that the
# Windows tests parse the exact bytes Linux produced, not a different download.
#
$corpusPath = ''
$corpusCount = 0
$corpusArgument = ''
if ($CorpusDir) {
    if (-not (Test-Path -LiteralPath $CorpusDir)) {
        throw "Corpus directory not found: $CorpusDir"
    }

    $corpusPath = (Resolve-Path -LiteralPath $CorpusDir).Path
    $manifest = Join-Path $corpusPath 'manifest.sha256'
    if (-not (Test-Path -LiteralPath $manifest)) {
        throw "Corpus manifest not found: $manifest"
    }

    foreach ($line in Get-Content -LiteralPath $manifest) {
        if (-not $line.Trim()) {
            continue
        }

        $parts = $line.Trim() -split '\s+', 2
        if ($parts.Count -ne 2) {
            throw "Malformed manifest line: $line"
        }

        $expectedHash = $parts[0].ToLowerInvariant()
        $imagePath = Join-Path $corpusPath $parts[1].TrimStart('*')
        if (-not (Test-Path -LiteralPath $imagePath)) {
            throw "Corpus image missing: $imagePath"
        }

        $actualHash = (Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "Corpus image hash mismatch: $imagePath"
        }

        $corpusCount++
    }

    if ($corpusCount -eq 0) {
        throw 'Corpus manifest listed no images.'
    }

    $corpusArgument = ' --corpus "' + $corpusPath + '"'
}

$sources = @()
$sources += Get-ChildItem -LiteralPath $engineDir -Filter '*.c' -File |
    Sort-Object Name |
    ForEach-Object { $_.FullName }
$sources += Get-ChildItem -LiteralPath $testDir -Filter '*.c' -File |
    Sort-Object Name |
    ForEach-Object { $_.FullName }

if ($sources.Count -eq 0) {
    throw 'No engine sources found to compile.'
}

#
# Build a batch file instead of trying to quote a compiler command line through
# PowerShell: cl.exe argument quoting here would otherwise be guesswork.
#
$batchLines = @(
    '@echo off'
    ('call "' + $vcvars + '" >nul')
    'if errorlevel 1 exit /b 1'
    ('cd /d "' + $outputDir + '"')
    ('cl /nologo /std:c11 /W4 /WX /I"' + $engineDir + '" ' +
        (($sources | ForEach-Object { '"' + $_ + '"' }) -join ' ') +
        ' /Fe:"' + $binary + '"')
    'if errorlevel 1 exit /b 2'
    (('"' + $binary + '"') + $corpusArgument)
)

$batchPath = Join-Path $outputDir 'run-engine-tests.cmd'
Set-Content -LiteralPath $batchPath -Value $batchLines -Encoding ascii

Write-Host "Engine host tests"
Write-Host "  configuration: $Configuration"
Write-Host "  sources:       $($sources.Count)"
Write-Host "  output:        $outputDir"
if ($corpusPath) {
    Write-Host "  corpus:        $corpusPath ($corpusCount images, hashes verified)"
}
else {
    Write-Host '  corpus:        none; synthetic superblocks only'
}
Write-Host ''

& cmd.exe /c $batchPath
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0) {
    throw "Engine host tests failed with exit code $exitCode."
}

Write-Host ''
Write-Host 'Engine host tests passed.'
exit 0
