$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$env:JAVA_HOME = (Get-ChildItem "$projectRoot/tools/jdk" -Directory | Select-Object -First 1).FullName
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
$settingsPath = Join-Path $projectRoot 'private\ghidra-home'
$cachePath = Join-Path $projectRoot 'private\ghidra-cache'
$tempPath = Join-Path $projectRoot 'private\ghidra-temp'
$env:APPDATA = $settingsPath
$env:LOCALAPPDATA = $cachePath
$env:JAVA_TOOL_OPTIONS = "-Duser.home=$settingsPath -Dapplication.settingsdir=$settingsPath -Dapplication.cachedir=$cachePath -Dapplication.tempdir=$tempPath"
New-Item -ItemType Directory -Force $settingsPath,$cachePath,$tempPath | Out-Null
& "$projectRoot/tools/ghidra/ghidra_12.2_PUBLIC/ghidraRun.bat"
