# Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT.
# Downloads the public NVIDIA DLSS SDK for a local build. Does not vendor it.

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Dest = Join-Path $Root 'third_party\ngx'
$Tag = 'v310.7.0'
$ZipUrl = "https://github.com/NVIDIA/DLSS/archive/refs/tags/$Tag.zip"
$Header = Join-Path $Dest 'include\nvsdk_ngx.h'

Write-Host "WinDoom: fetch NVIDIA DLSS SDK ($Tag)"
Write-Host "Destination (gitignored): $Dest"
Write-Host "SDK license is NVIDIA's, not GPLv2. See win32\README-NGX.md"
Write-Host ""

if (Test-Path $Header) {
    Write-Host "SDK already present."
} else {
    $Tmp = Join-Path $env:TEMP "windoom-dlss-$Tag.zip"
    $Unpack = Join-Path $env:TEMP "windoom-dlss-$Tag"
    Write-Host "Downloading $ZipUrl"
    Invoke-WebRequest -Uri $ZipUrl -OutFile $Tmp
    if (Test-Path $Unpack) { Remove-Item $Unpack -Recurse -Force }
    Expand-Archive -Path $Tmp -DestinationPath $Unpack
    $Inner = Get-ChildItem $Unpack -Directory | Select-Object -First 1
    if (-not $Inner) { throw "Zip had no folder" }
    New-Item -ItemType Directory -Force -Path (Split-Path $Dest) | Out-Null
    if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
    Move-Item $Inner.FullName $Dest
    Remove-Item $Tmp -Force
    Remove-Item $Unpack -Recurse -Force -ErrorAction SilentlyContinue
    if (-not (Test-Path $Header)) { throw "nvsdk_ngx.h missing after unpack" }
    Write-Host "Unpacked headers and libs."
}

$Dll = Get-ChildItem -Path $Dest -Recurse -Filter 'nvngx_dlss.dll' -ErrorAction SilentlyContinue |
    Select-Object -First 1
$ExeDirs = @(
    (Join-Path $Root 'build-win\Release'),
    (Join-Path $Root 'build-win')
)
if ($Dll) {
    foreach ($dir in $ExeDirs) {
        if (Test-Path $dir) {
            Copy-Item $Dll.FullName (Join-Path $dir $Dll.Name) -Force
            Write-Host "Copied $($Dll.Name) -> $dir"
        }
    }
} else {
    Write-Host "nvngx_dlss.dll not in this SDK tree; copy it next to windoom.exe later."
}

Write-Host ""
Write-Host "Next:"
Write-Host "  cmake -S . -B build-win -A x64"
Write-Host "  cmake --build build-win --config Release"
Write-Host "CMake will pick up third_party\ngx automatically."
