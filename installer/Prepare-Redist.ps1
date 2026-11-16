# Copy Microsoft VC++ Redistributable (x64) into installer\redist for Inno Setup.
# Prefers a local VS Build Tools / VS install; falls back to aka.ms download.
[CmdletBinding()]
param(
    [string]$DestDir = '',
    [string]$DestName = 'vc_redist.x64.exe'
)

$ErrorActionPreference = 'Stop'
$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $DestDir) { $DestDir = Join-Path $here 'redist' }

New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
$dest = Join-Path $DestDir $DestName

$candidates = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\v143\vc_redist.x64.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\v143\vc_redist.x64.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC\v143\vc_redist.x64.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC\v143\vc_redist.x64.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\v143\vc_redist.x64.exe"
)

foreach ($src in $candidates) {
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src -Destination $dest -Force
        Write-Host "Copied VC redist from:`n  $src`n-> $dest"
        Get-Item -LiteralPath $dest | Select-Object FullName, Length
        exit 0
    }
}

Write-Host "No local VS vc_redist.x64.exe found; downloading from aka.ms..."
$uri = 'https://aka.ms/vs/17/release/vc_redist.x64.exe'
Invoke-WebRequest -Uri $uri -OutFile $dest -UseBasicParsing
Write-Host "Downloaded -> $dest"
Get-Item -LiteralPath $dest | Select-Object FullName, Length
