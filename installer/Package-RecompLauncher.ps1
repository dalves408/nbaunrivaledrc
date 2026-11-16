[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+([-.][A-Za-z0-9.-]+)?$')]
    [string]$Version,
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [string]$OutputDir,
    [string]$CrtDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = (Resolve-Path -LiteralPath $BuildDir).Path
if (!$OutputDir) { $OutputDir = Join-Path $root 'out/recomplauncher' }
if (!$CrtDir) {
    if (!$env:VCToolsRedistDir) { throw 'Open the developer shell or supply -CrtDir.' }
    $CrtDir = Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC143.CRT'
}

# Keep the launch payload explicit so local user files never enter the archive.
$files = [ordered]@{
    'nbaunrivaled.exe' = Join-Path $build 'nbaunrivaled.exe'
    'nba_import.exe' = Join-Path $build 'nba_import.exe'
    'config.exe' = Join-Path $build 'config.exe'
    'rexruntime.dll' = Join-Path $build 'rexruntime.dll'
    'rexgpu-xenos.dll' = Join-Path $build 'rexgpu-xenos.dll'
    'recomplauncher.json' = Join-Path $root 'recomplauncher.json'
    'launcher-support/Apply-TitleMenuPatches.ps1' = Join-Path $root 'scripts/Apply-TitleMenuPatches.ps1'
    'launcher-support/title-menu-patches.json' = Join-Path $root 'scripts/title-menu-patches.json'
    'gamecontrollerdb.txt' = Join-Path $root 'tools/controller-mappings/gamecontrollerdb.txt'
    'licenses/SDL_GameControllerDB.txt' = Join-Path $root 'tools/controller-mappings/LICENSE'
    'licenses/ReXGlue.txt' = Join-Path $PSScriptRoot 'licenses/ReXGlue.txt'
    'licenses/SDL3.txt' = Join-Path $root 'tools/rexglue-sdk/win-amd64/licenses/SDL3/LICENSE.txt'
}
foreach ($dll in 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    $files[$dll] = Join-Path $CrtDir $dll
}
foreach ($source in $files.Values) {
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing release input: $source" }
}

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$output = (New-Item -ItemType Directory -Path $OutputDir -Force).FullName
$zip = Join-Path $output "NBAUnrivaled-Recompiled-v$Version-Windows-x64.zip"
$temp = "$zip.$([Guid]::NewGuid().ToString('N')).tmp"
try {
    $archive = [IO.Compression.ZipFile]::Open($temp, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($entry in $files.GetEnumerator()) {
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive, $entry.Value, $entry.Key, [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally {
        $archive.Dispose()
    }
    Move-Item -LiteralPath $temp -Destination $zip -Force
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force }
}

$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $(Split-Path $zip -Leaf)" | Set-Content -LiteralPath "$zip.sha256" -Encoding ASCII
Get-Item -LiteralPath $zip
