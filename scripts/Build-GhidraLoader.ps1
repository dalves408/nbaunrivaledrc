$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$env:JAVA_HOME = (Get-ChildItem "$projectRoot/tools/jdk" -Directory | Select-Object -First 1).FullName
$env:GHIDRA_INSTALL_DIR = "$projectRoot/tools/ghidra/ghidra_12.2_PUBLIC"
$env:GRADLE_USER_HOME = "$projectRoot/tools/gradle-home"
& "$projectRoot/tools/gradle/gradle-9.3.1/bin/gradle.bat" -p "$projectRoot/tools/XEXLoaderWV/XEXLoaderWV" --no-daemon buildExtension
if ($LASTEXITCODE -ne 0) { throw 'XEX loader build failed.' }
$extension = Get-ChildItem "$projectRoot/tools/XEXLoaderWV/XEXLoaderWV/dist" -Filter '*.zip' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $extension) { throw 'Missing extension archive.' }
$extensionRoot = "$env:GHIDRA_INSTALL_DIR/Ghidra/Extensions"
if (-not (Test-Path "$extensionRoot/XEXLoaderWV")) {
    Expand-Archive -LiteralPath $extension.FullName -DestinationPath $extensionRoot
}
Write-Output 'XEXLoaderWV is installed in the project-local Ghidra distribution.'
