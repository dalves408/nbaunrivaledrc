$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ Build Tools are required.' }
& (Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1') -SkipAutomaticLocation -Arch amd64 -HostArch amd64
if (-not (Test-Path "$projectRoot/tools/llvm/bin/clang++.exe")) { throw 'Missing local LLVM compiler.' }
$env:PATH = "$projectRoot\tools\llvm\bin;$projectRoot\tools\rexglue-sdk\win-amd64\bin;$env:PATH;$projectRoot\tools\lldb-python"
$env:CMAKE_PREFIX_PATH = "$projectRoot/tools/rexglue-sdk/win-amd64"
Set-Location $projectRoot
