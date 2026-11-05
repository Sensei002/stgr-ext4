#Requires -Version 5.1
<#
.SYNOPSIS
    Repository hygiene checks that do not require a compiler.

.DESCRIPTION
    Verifies:
      * required files exist and are non-empty;
      * C/H/RC/Rust sources carry an SPDX MIT identifier;
      * the driver and management code reference no WinFsp/Dokany/FUSE;
      * no private key material or checked-in driver binaries exist;
      * driver, INF, RC and Cargo versions agree.

    Run by ci.yml, security.yml, and locally.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$errors = New-Object System.Collections.Generic.List[string]

function Add-Problem {
    param([string]$Message)
    $errors.Add($Message)
}

function Get-Text {
    param([string]$Path)
    return [System.IO.File]::ReadAllText($Path)
}

function Test-TextFile {
    param([System.IO.FileInfo]$File)
    $extension = $File.Extension.ToLowerInvariant()
    return $extension -in @('.c', '.h', '.rc', '.rs', '.toml', '.yml', '.yaml', '.md', '.ps1', '.inf', '.vcxproj', '.props', '.config', '.txt')
}

$requiredFiles = @(
    'README.md'
    'LICENSE'
    'CONTRIBUTING.md'
    'SECURITY.md'
    'CHANGELOG.md'
    'ROADMAP.md'
    'THIRD_PARTY_NOTICES.md'
    'docs/FEASIBILITY.md'
    'docs/ARCHITECTURE.md'
    'docs/WINDOWS-IFS.md'
    'docs/EXT4-FORMAT.md'
    'docs/JOURNAL-IMPLEMENTATION.md'
    'docs/DRIVER-INSTALLATION.md'
    'docs/DRIVER-SIGNING.md'
    'docs/GITHUB-ACTIONS.md'
    'docs/CONFIGURATION.md'
    'docs/FILESYSTEM-COMPATIBILITY.md'
    'docs/DATA-INTEGRITY.md'
    'docs/GAMING-COMPATIBILITY.md'
    'docs/TESTING.md'
    'docs/TROUBLESHOOTING.md'
    'driver/STGRExt4.vcxproj'
    'driver/STGRExt4.inf'
    'driver/include/STGRExt4.h'
    'driver/src/driver.c'
    'driver/src/dispatch.c'
    'management/Cargo.lock'
    'management/Cargo.toml'
    'management/stgr-ext4-cli/Cargo.toml'
    'management/stgr-ext4-cli/src/main.rs'
    'management/stgr-ext4-cli/tests/cli.rs'
    '.github/workflows/ci.yml'
    '.github/workflows/driver-build.yml'
    '.github/workflows/management-build.yml'
    '.github/workflows/security.yml'
    '.github/workflows/release.yml'
)

foreach ($relative in $requiredFiles) {
    $path = Join-Path $repoRoot $relative
    if (-not (Test-Path -LiteralPath $path)) {
        Add-Problem "Missing required file: $relative"
        continue
    }
    if ((Get-Item -LiteralPath $path).Length -eq 0) {
        Add-Problem "Required file is empty: $relative"
    }
}

# ---------------------------------------------------------------------------
# SPDX headers in source files
# ---------------------------------------------------------------------------
$sourceRoots = @(
    (Join-Path $repoRoot 'driver')
    (Join-Path $repoRoot 'management')
)

$sourceExtensions = @('.c', '.h', '.rc', '.rs')

foreach ($root in $sourceRoots) {
    if (-not (Test-Path -LiteralPath $root)) {
        continue
    }
    Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
        $_.Extension.ToLowerInvariant() -in $sourceExtensions -and
        $_.FullName -notmatch '\\target\\|\\obj\\'
    } | ForEach-Object {
        $head = (Get-Content -LiteralPath $_.FullName -TotalCount 25) -join "`n"
        if ($head -notmatch 'SPDX-License-Identifier:\s*MIT') {
            Add-Problem "Missing SPDX-License-Identifier: MIT in $($_.FullName.Substring($repoRoot.Length + 1))"
        }
    }
}

# ---------------------------------------------------------------------------
# Forbidden user-mode filesystem dependencies in code (documentation may
# discuss them; code must not depend on them).
# ---------------------------------------------------------------------------
$codeExtensions = @('.c', '.h', '.rc', '.rs', '.toml', '.inf', '.vcxproj', '.props')
$forbidden = '(?i)\b(winfsp|dokany|dokan|fuse)\b'

foreach ($root in $sourceRoots) {
    if (-not (Test-Path -LiteralPath $root)) {
        continue
    }
    Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
        $_.Extension.ToLowerInvariant() -in $codeExtensions -and
        $_.FullName -notmatch '\\target\\|\\obj\\'
    } | ForEach-Object {
        $text = Get-Text $_.FullName
        if ($text -match $forbidden) {
            Add-Problem "Forbidden user-mode filesystem dependency referenced in $($_.FullName.Substring($repoRoot.Length + 1))"
        }
    }
}

# ---------------------------------------------------------------------------
# Private key material and checked-in binaries
# ---------------------------------------------------------------------------
$scanRoots = @($repoRoot)
$skipDirectories = @('packages', 'artifacts', 'target', '.git', '.vs')

$keyPattern = '-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----'
$secretExtensions = @('.pfx', '.pvk', '.snk')
$binaryExtensions = @('.sys', '.cat', '.pdb', '.img', '.vhd', '.vhdx', '.dmp')

Get-ChildItem -LiteralPath $repoRoot -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
    $relative = $_.FullName.Substring($repoRoot.Length + 1)
    $skip = $false
    foreach ($directory in $skipDirectories) {
        if ($relative -like "$directory\*" -or $relative -like "*/$directory/*") {
            $skip = $true
            break
        }
    }
    -not $skip
} | ForEach-Object {
    $relative = $_.FullName.Substring($repoRoot.Length + 1)
    $extension = $_.Extension.ToLowerInvariant()

    if ($extension -in $secretExtensions) {
        Add-Problem "Private key container must not be committed: $relative"
    }

    if ($extension -in $binaryExtensions) {
        Add-Problem "Build/test binary must not be committed: $relative"
    }

    if ($_.Length -lt 1048576 -and (Test-TextFile $_)) {
        $text = Get-Text $_.FullName
        if ($text -match $keyPattern) {
            Add-Problem "Private key material found in $relative"
        }
    }
}

# ---------------------------------------------------------------------------
# Version consistency
# ---------------------------------------------------------------------------
$headerPath = Join-Path $repoRoot 'driver\include\STGRExt4.h'
$infPath = Join-Path $repoRoot 'driver\STGRExt4.inf'
$rcPath = Join-Path $repoRoot 'driver\version.rc'
$cargoPath = Join-Path $repoRoot 'management\Cargo.toml'

if ((Test-Path -LiteralPath $headerPath) -and (Test-Path -LiteralPath $infPath) -and
    (Test-Path -LiteralPath $rcPath) -and (Test-Path -LiteralPath $cargoPath)) {

    $header = Get-Text $headerPath
    $major = [int]([regex]::Match($header, '#define\s+STGR_DRIVER_VERSION_MAJOR\s+(\d+)').Groups[1].Value)
    $minor = [int]([regex]::Match($header, '#define\s+STGR_DRIVER_VERSION_MINOR\s+(\d+)').Groups[1].Value)
    $patch = [int]([regex]::Match($header, '#define\s+STGR_DRIVER_VERSION_PATCH\s+(\d+)').Groups[1].Value)

    $infText = Get-Text $infPath
    $infMatch = [regex]::Match($infText, 'DriverVer\s*=\s*[^,\r\n]+,\s*(\d+)\.(\d+)\.(\d+)\.(\d+)')

    $rcText = Get-Text $rcPath
    $rcMatch = [regex]::Match($rcText, 'FILEVERSION\s+(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)')

    $cargoText = Get-Text $cargoPath
    $cargoMatch = [regex]::Match($cargoText, '(?m)^version\s*=\s*"(\d+)\.(\d+)\.(\d+)"')

    if (-not $infMatch.Success) {
        Add-Problem 'Could not parse DriverVer from driver/STGRExt4.inf'
    }
    if (-not $rcMatch.Success) {
        Add-Problem 'Could not parse FILEVERSION from driver/version.rc'
    }
    if (-not $cargoMatch.Success) {
        Add-Problem 'Could not parse workspace version from management/Cargo.toml'
    }

    if ($infMatch.Success) {
        if ([int]$infMatch.Groups[1].Value -ne $major -or
            [int]$infMatch.Groups[2].Value -ne $minor -or
            [int]$infMatch.Groups[3].Value -ne $patch) {
            Add-Problem "INF version $($infMatch.Groups[1].Value).$($infMatch.Groups[2].Value).$($infMatch.Groups[3].Value) does not match driver header $major.$minor.$patch"
        }
    }

    if ($rcMatch.Success) {
        if ([int]$rcMatch.Groups[1].Value -ne $major -or
            [int]$rcMatch.Groups[2].Value -ne $minor -or
            [int]$rcMatch.Groups[3].Value -ne $patch) {
            Add-Problem "RC version does not match driver header $major.$minor.$patch"
        }
    }

    if ($cargoMatch.Success) {
        if ([int]$cargoMatch.Groups[1].Value -ne $major -or
            [int]$cargoMatch.Groups[2].Value -ne $minor -or
            [int]$cargoMatch.Groups[3].Value -ne $patch) {
            Add-Problem "Cargo version $($cargoMatch.Groups[1].Value).$($cargoMatch.Groups[2].Value).$($cargoMatch.Groups[3].Value) does not match driver header $major.$minor.$patch"
        }
    }
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
Write-Host 'STGR EXT4 repository hygiene'
Write-Host "  repository: $repoRoot"
Write-Host "  required files checked: $($requiredFiles.Count)"
Write-Host '  SPDX headers: checked'
Write-Host '  forbidden user-mode filesystem dependencies: checked'
Write-Host '  private key material and binaries: checked'
Write-Host '  version consistency: checked'

if ($errors.Count -gt 0) {
    Write-Host ''
    Write-Host "FAILED with $($errors.Count) problem(s):"
    foreach ($problem in $errors) {
        Write-Host "  - $problem"
    }
    exit 1
}

Write-Host ''
Write-Host 'All repository hygiene checks passed.'
exit 0
