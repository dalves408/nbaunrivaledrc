param(
    [ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo',
    [switch]$AllowUnresolved,
    [switch]$Fresh
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Enter-DevShell.ps1"
$codegenArgs = @('--log-file', 'private/codegen.log')
if ($AllowUnresolved) { $codegenArgs += '--force' }
$codegenArgs += @('codegen', 'nbaunrivaled_manifest.toml', '--ignore-stamp')
& rexglue @codegenArgs
if ($LASTEXITCODE -ne 0) { throw 'Codegen failed. See private/codegen.log; resolve analysis errors before building.' }
$preset = 'win-amd64-' + $Configuration.ToLower()
$configureArgs = @('--preset', $preset)
if ($Fresh) { $configureArgs += '--fresh' }
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& cmake --build --preset $preset --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
