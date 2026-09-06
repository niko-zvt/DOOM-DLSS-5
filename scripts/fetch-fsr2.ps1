# Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT.
# Downloads AMD FidelityFX FSR2 2.2.1 sources for a local build. Does not vendor them.

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Dest = Join-Path $Root 'third_party\fsr2'
$Tag = 'v2.2.1'
$ZipUrl = "https://github.com/GPUOpen-Effects/FidelityFX-FSR2/archive/refs/tags/$Tag.zip"
$Header = Join-Path $Dest 'ffx-fsr2-api\ffx_fsr2.h'
$Sc = Join-Path $Dest 'tools\sc\FidelityFX_SC.exe'
$Cache = Join-Path $Root 'build-win\_fsr2_fetch'
$Tmp = Join-Path $Cache "$Tag.zip"
$Unpack = Join-Path $Cache 'unpack'

function Expand-Fsr2Zip {
    param([string]$ZipPath, [string]$OutDir)

    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $tar = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($tar) {
        & $tar.Source -xf $ZipPath -C $OutDir
        if ($LASTEXITCODE -eq 0) { return }
        Write-Host "tar failed, trying ZipFile."
        Remove-Item $OutDir -Recurse -Force
        New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($ZipPath, $OutDir)
}

function Get-Fsr2Zip {
    param([string]$Url, [string]$ZipPath)

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        & $curl.Source -fsSL -o $ZipPath $Url
        if ($LASTEXITCODE -ne 0) { throw "curl failed to download $Url" }
        return
    }
    Invoke-WebRequest -Uri $Url -OutFile $ZipPath
}

Write-Host "WinDoom: fetch AMD FidelityFX FSR2 ($Tag)"
Write-Host "Destination (gitignored): $Dest"
Write-Host "SDK license is MIT (AMD), not GPLv2. See win32\README-FSR2.md"
Write-Host ""

if ((Test-Path $Header) -and (Test-Path $Sc)) {
    Write-Host "FSR2 sources already present."
    exit 0
}

if (Test-Path $Dest) {
    Write-Host "Incomplete FSR2 tree, fetching again."
    Remove-Item $Dest -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $Cache | Out-Null
if (Test-Path $Unpack) { Remove-Item $Unpack -Recurse -Force }

if (-not (Test-Path $Tmp)) {
    Write-Host "Downloading $ZipUrl"
    Get-Fsr2Zip -Url $ZipUrl -ZipPath $Tmp
} else {
    Write-Host "Using cached zip $Tmp"
}

Expand-Fsr2Zip -ZipPath $Tmp -OutDir $Unpack
$Inner = Get-ChildItem $Unpack -Directory | Select-Object -First 1
if (-not $Inner) { throw "Zip had no folder" }

$ApiSrc = Join-Path $Inner.FullName 'src\ffx-fsr2-api'
$ScSrc = Join-Path $Inner.FullName 'tools\sc'
if (-not (Test-Path (Join-Path $ApiSrc 'ffx_fsr2.h'))) {
    throw "ffx_fsr2.h missing in unpacked tree"
}
if (-not (Test-Path (Join-Path $ScSrc 'FidelityFX_SC.exe'))) {
    throw "FidelityFX_SC.exe missing in unpacked tree"
}

New-Item -ItemType Directory -Force -Path $Dest | Out-Null
Move-Item $ApiSrc (Join-Path $Dest 'ffx-fsr2-api')
New-Item -ItemType Directory -Force -Path (Join-Path $Dest 'tools') | Out-Null
Move-Item $ScSrc (Join-Path $Dest 'tools\sc')

Remove-Item $Unpack -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $Tmp -Force -ErrorAction SilentlyContinue

if (-not (Test-Path $Header)) { throw "ffx_fsr2.h missing after unpack" }
if (-not (Test-Path $Sc)) { throw "FidelityFX_SC.exe missing after unpack" }

Write-Host "Unpacked ffx-fsr2-api and FidelityFX_SC."
Write-Host ""
Write-Host "Next:"
Write-Host "  .\build.cmd --fsr2"
Write-Host "CMake will compile the DX12 backend from third_party\fsr2."
