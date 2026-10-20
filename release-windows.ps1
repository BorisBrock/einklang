# =============================================================================
# release-windows.ps1 — Einklang release packager (Windows)
#
# Produces two release artefacts in the "release/" directory:
#   • EinklangServer_Portable_Windows_<version>.zip
#   • EinklangClient_Portable_Windows_<version>.zip
#
# Usage:  .\release-windows.ps1 <version>     (e.g. 1.0.0 — normally the git tag without 'v')
# =============================================================================
#Requires -Version 5.1
param(
    [Parameter(Mandatory = $true)]
    [string] $Version
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
$RootDir    = $PSScriptRoot
$BuildDir   = Join-Path $RootDir 'build\Release'
$ReleaseDir = Join-Path $RootDir 'release'

# ---------------------------------------------------------------------------
# Step 0 — Version (compiled into the binaries)
# ---------------------------------------------------------------------------
$Version = $Version -replace '^v', ''
Write-Host "==> Version: $Version"

# ---------------------------------------------------------------------------
# Step 1 — Prepare the release output folder
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "==> Preparing release directory"

if (-not (Test-Path $ReleaseDir)) {
    New-Item -ItemType Directory -Path $ReleaseDir | Out-Null
}

# Remove any leftover artefacts from a previous run so we don't accidentally
# ship stale files.
Get-ChildItem -Path $ReleaseDir -Filter '*.zip' | Remove-Item -Force

# ---------------------------------------------------------------------------
# Step 2 — Configure and build (Release, both targets)
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "==> Configuring CMake (Release)"

cmake -S $RootDir -B $BuildDir `
    -DCMAKE_BUILD_TYPE=Release `
    -DEINKLANG_BUILD_CLIENT=ON `
    "-DEINKLANG_VERSION=$Version"

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "==> Building EinklangServerApp"
cmake --build $BuildDir --target EinklangServerApp --config Release --parallel

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "==> Building EinklangClientApp"
cmake --build $BuildDir --target EinklangClientApp --config Release --parallel

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# With multi-config generators (MSVC/Ninja Multi-Config) binaries land in
# a per-configuration subdirectory, e.g. build\Release\Release\.
$BinDir = Join-Path $BuildDir 'Release'

# ---------------------------------------------------------------------------
# Helper: create a zip from a set of files without embedding full paths
# ---------------------------------------------------------------------------
function New-FlatZip {
    param(
        [string]   $ZipPath,
        [string[]] $Files
    )

    $stagingDir = Join-Path $env:TEMP "einklang_release_staging_$([System.IO.Path]::GetRandomFileName())"
    New-Item -ItemType Directory -Path $stagingDir | Out-Null

    try {
        foreach ($f in $Files) {
            Copy-Item -Path $f -Destination $stagingDir
        }
        Compress-Archive -Path (Join-Path $stagingDir '*') -DestinationPath $ZipPath -Force
    }
    finally {
        Remove-Item -Recurse -Force $stagingDir
    }
}

# ---------------------------------------------------------------------------
# Step 3 — Package the server
# ---------------------------------------------------------------------------
$ServerZip = Join-Path $ReleaseDir "EinklangServer_Portable_Windows_${Version}.zip"

Write-Host ""
Write-Host "==> Packaging server → $ServerZip"

New-FlatZip -ZipPath $ServerZip -Files @(
    (Join-Path $BinDir 'EinklangServerApp.exe'),
    (Join-Path $BuildDir 'server-config.yml')
)

Write-Host "    Created: $ServerZip"

# ---------------------------------------------------------------------------
# Step 4 — Package the client
# ---------------------------------------------------------------------------
$ClientZip = Join-Path $ReleaseDir "EinklangClient_Portable_Windows_${Version}.zip"

Write-Host ""
Write-Host "==> Packaging client → $ClientZip"

New-FlatZip -ZipPath $ClientZip -Files @(
    (Join-Path $BinDir 'EinklangClientApp.exe'),
    (Join-Path $BuildDir 'client-config.yml')
)

Write-Host "    Created: $ClientZip"

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "==> Release $Version complete. Artefacts in: $ReleaseDir"
Get-ChildItem -Path $ReleaseDir | Format-Table Name, Length, LastWriteTime
