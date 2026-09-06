# Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT.
# Downloads the public NVIDIA DLSS SDK for a local build. Does not vendor it.

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Dest = Join-Path $Root 'third_party\ngx'
$Tag = 'v310.7.0'
$ZipUrl = "https://github.com/NVIDIA/DLSS/archive/refs/tags/$Tag.zip"
$Header = Join-Path $Dest 'include\nvsdk_ngx.h'
$Cache = Join-Path $Root 'build-win\_ngx_fetch'
$Tmp = Join-Path $Cache "$Tag.zip"
$Unpack = Join-Path $Cache 'unpack'
$LegacyTmp = Join-Path $env:TEMP "windoom-dlss-$Tag.zip"
$LegacyUnpack = Join-Path $env:TEMP "windoom-dlss-$Tag"

function Expand-NgxZip {
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

function Get-NgxZip {
    param([string]$Url, [string]$ZipPath)

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        & $curl.Source -fsSL -o $ZipPath $Url
        if ($LASTEXITCODE -ne 0) { throw "curl failed to download $Url" }
        return
    }
    Invoke-WebRequest -Uri $Url -OutFile $ZipPath
}

Write-Host "WinDoom: fetch NVIDIA DLSS SDK ($Tag)"
Write-Host "Destination (gitignored): $Dest"
Write-Host "SDK license is NVIDIA's, not GPLv2. See win32\README-NGX.md"
Write-Host ""

if (Test-Path $Header) {
    Write-Host "SDK already present."
} else {
    if (Test-Path $Dest) {
        Write-Host "Incomplete SDK tree, fetching again."
        Remove-Item $Dest -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $Cache | Out-Null
    if (Test-Path $LegacyUnpack) {
        Remove-Item $LegacyUnpack -Recurse -Force
    }
    if (-not (Test-Path $Tmp) -and (Test-Path $LegacyTmp)) {
        Write-Host "Moving leftover zip off %TEMP%."
        Move-Item $LegacyTmp $Tmp
    } elseif (Test-Path $LegacyTmp) {
        Remove-Item $LegacyTmp -Force
    }
    if (Test-Path $Unpack) { Remove-Item $Unpack -Recurse -Force }

    if (-not (Test-Path $Tmp)) {
        Write-Host "Downloading $ZipUrl"
        Get-NgxZip -Url $ZipUrl -ZipPath $Tmp
    } else {
        Write-Host "Using cached zip $Tmp"
    }

    Expand-NgxZip -ZipPath $Tmp -OutDir $Unpack
    $Inner = Get-ChildItem $Unpack -Directory | Select-Object -First 1
    if (-not $Inner) { throw "Zip had no folder" }
    New-Item -ItemType Directory -Force -Path (Split-Path $Dest) | Out-Null
    Move-Item $Inner.FullName $Dest
    Remove-Item $Unpack -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item $Tmp -Force -ErrorAction SilentlyContinue
    if (-not (Test-Path $Header)) { throw "nvsdk_ngx.h missing after unpack" }
    Write-Host "Unpacked headers and libs."
}

$Dlls = @(
    (Get-ChildItem -Path $Dest -Recurse -Filter 'nvngx_dlss.dll' -ErrorAction SilentlyContinue | Select-Object -First 1),
    (Get-ChildItem -Path $Dest -Recurse -Filter 'nvngx_dlssd.dll' -ErrorAction SilentlyContinue | Select-Object -First 1)
)
$ExeDirs = @(
    (Join-Path $Root 'build-win\Release\windoom-ngx-dlss3.5'),
    (Join-Path $Root 'build-win\Release\windoom-ngx-dlss4'),
    (Join-Path $Root 'build-win\Release\windoom-ngx-dlss4.5'),
    (Join-Path $Root 'build-win\Release\windoom-ngx-dlss5')
)
$copiedAny = $false
foreach ($dll in $Dlls) {
    if (-not $dll) { continue }
    foreach ($dir in $ExeDirs) {
        if (Test-Path $dir) {
            Copy-Item $dll.FullName (Join-Path $dir $dll.Name) -Force
            Write-Host "Copied $($dll.Name) -> $dir"
            $copiedAny = $true
        }
    }
}
if (-not $copiedAny) {
    Write-Host "nvngx_dlss.dll / nvngx_dlssd.dll not copied; stage folders first or copy next to windoom.exe later."
}

Write-Host ""
Write-Host "Next:"
Write-Host "  .\build.cmd --ngx-dlss3.5"
Write-Host "  .\build.cmd --ngx-dlss4"
Write-Host "  .\build.cmd --ngx-dlss4.5"
Write-Host "  .\build.cmd --ngx-dlss5"
Write-Host "  .\build.cmd --all"
Write-Host "CMake will pick up third_party\ngx automatically."
