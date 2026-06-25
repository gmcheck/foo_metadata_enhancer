<#
.SYNOPSIS
    foo_ai_metadata plugin packaging script

.DESCRIPTION
    Collects plugin files from build output and source, packages into a zip.
    Version is read from foo_ai_metadata.rc file.

.EXAMPLE
    .\pack.ps1
    .\pack.ps1 -Version 1.0.1
    .\pack.ps1 -BuildFirst
#>

param(
    [string]$Version = "",
    [switch]$BuildFirst
)

$ErrorActionPreference = "Stop"

# =============================================================================
# Paths
# =============================================================================
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "out\build\Release"
$WorkerSourceDir = Join-Path $ProjectRoot "worker"
$RcFile = Join-Path $ProjectRoot "foo_ai_metadata.rc"
$ZipsDir = Join-Path $ProjectRoot "zips"

# =============================================================================
# Read version from .rc file
# =============================================================================
if ([string]::IsNullOrEmpty($Version)) {
    if (Test-Path $RcFile) {
        $rcContent = Get-Content $RcFile -Raw
        if ($rcContent -match 'VER_PRODUCTVERSION_STR\s+"(\d+)\.(\d+)\.(\d+)') {
            $Version = "$($matches[1]).$($matches[2]).$($matches[3])"
        }
    }
    if ([string]::IsNullOrEmpty($Version)) {
        Write-Error "Cannot read version from foo_ai_metadata.rc. Use -Version parameter."
        exit 1
    }
}

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  foo_ai_metadata Packager" -ForegroundColor Cyan
Write-Host "  Version: $Version" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

# =============================================================================
# Optional: Build first
# =============================================================================
if ($BuildFirst) {
    Write-Host "[1/5] Building project..." -ForegroundColor Yellow
    Push-Location $ProjectRoot
    try {
        & cmake --build out/build --config Release -- /m
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Build failed"
            exit 1
        }
    } finally {
        Pop-Location
    }
    Write-Host "      Build complete" -ForegroundColor Green
} else {
    Write-Host "[1/5] Skipping build (use -BuildFirst to enable)" -ForegroundColor DarkGray
}

# =============================================================================
# Check files
# =============================================================================
Write-Host "[2/5] Checking files..." -ForegroundColor Yellow

$DllPath = Join-Path $BuildDir "foo_ai_metadata.dll"
if (-not (Test-Path $DllPath)) {
    Write-Error "DLL not found: $DllPath`nBuild first or use -BuildFirst"
    exit 1
}
Write-Host "      DLL: $DllPath" -ForegroundColor DarkGray

if (-not (Test-Path $WorkerSourceDir)) {
    Write-Error "Worker directory not found: $WorkerSourceDir"
    exit 1
}
Write-Host "      Worker: $WorkerSourceDir" -ForegroundColor DarkGray

# =============================================================================
# Prepare temp directory
# =============================================================================
Write-Host "[3/5] Preparing files..." -ForegroundColor Yellow

$TempDir = Join-Path $env:TEMP "foo_ai_metadata_pack_$Version"
if (Test-Path $TempDir) {
    Remove-Item $TempDir -Recurse -Force
}
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

# Copy DLL
Copy-Item $DllPath $TempDir

# Create foo_ai_metadata folder structure
$PluginDir = Join-Path $TempDir "foo_ai_metadata"
New-Item -ItemType Directory -Path $PluginDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $PluginDir "cache") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $PluginDir "logs") -Force | Out-Null

# Copy worker directory (excluding __pycache__ and config.yaml)
$WorkerTargetDir = Join-Path $PluginDir "worker"
Copy-Item $WorkerSourceDir $WorkerTargetDir -Recurse

# Remove __pycache__ directories
Get-ChildItem -Path $WorkerTargetDir -Recurse -Directory -Filter "__pycache__" | Remove-Item -Recurse -Force

# Remove .pyc files
Get-ChildItem -Path $WorkerTargetDir -Recurse -Filter "*.pyc" | Remove-Item -Force

# Remove local config.yaml (contains API keys), use template instead
$LocalConfig = Join-Path $WorkerTargetDir "config.yaml"
if (Test-Path $LocalConfig) {
    Remove-Item $LocalConfig -Force
}

# Copy config.yaml.template as config.yaml
$ConfigTemplate = Join-Path $WorkerSourceDir "config.yaml.template"
if (Test-Path $ConfigTemplate) {
    Copy-Item $ConfigTemplate (Join-Path $WorkerTargetDir "config.yaml")
}

Write-Host "      Temp: $TempDir" -ForegroundColor DarkGray

# =============================================================================
# Create zip
# =============================================================================
Write-Host "[4/5] Creating zip..." -ForegroundColor Yellow

if (-not (Test-Path $ZipsDir)) {
    New-Item -ItemType Directory -Path $ZipsDir -Force | Out-Null
}

$ZipName = "foo_ai_metadata-$Version.zip"
$ZipPath = Join-Path $ZipsDir $ZipName

if (Test-Path $ZipPath) {
    Remove-Item $ZipPath -Force
    Write-Host "      Removed old: $ZipName" -ForegroundColor DarkGray
}

Compress-Archive -Path (Join-Path $TempDir "*") -DestinationPath $ZipPath -CompressionLevel Optimal

Write-Host "      Created: $ZipPath" -ForegroundColor Green

# =============================================================================
# Cleanup
# =============================================================================
Write-Host "[5/5] Cleaning up..." -ForegroundColor Yellow
Remove-Item $TempDir -Recurse -Force
Write-Host "      Done" -ForegroundColor Green

# =============================================================================
# Result
# =============================================================================
Write-Host ""
Write-Host "==========================================" -ForegroundColor Green
Write-Host "  Package created!" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Version: $Version" -ForegroundColor White
Write-Host "  File:    $ZipPath" -ForegroundColor White
Write-Host ""
Write-Host "  Zip structure:" -ForegroundColor DarkGray
Write-Host "    foo_ai_metadata.dll" -ForegroundColor DarkGray
Write-Host "    foo_ai_metadata/" -ForegroundColor DarkGray
Write-Host "      cache/   (empty)" -ForegroundColor DarkGray
Write-Host "      logs/    (empty)" -ForegroundColor DarkGray
Write-Host "      worker/  (Python scripts)" -ForegroundColor DarkGray
Write-Host ""
Write-Host "  Install: Extract to foobar2000 components/ directory" -ForegroundColor Yellow
Write-Host ""
