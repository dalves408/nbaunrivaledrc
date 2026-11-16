[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GameRoot
)

$ErrorActionPreference = 'Stop'

function Get-BytesSha256([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha.ComputeHash($Bytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

$frontEnd = Join-Path $GameRoot 'FRONT_END'
$manifestPath = Join-Path $PSScriptRoot 'title-menu-patches.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$prepared = foreach ($name in @('FE_MAINMENU.XUR', 'FE_MAINMENU_4X3.XUR')) {
    $path = Join-Path $frontEnd $name
    $bytes = [IO.File]::ReadAllBytes($path)
    $patch = $manifest.PSObject.Properties[$name].Value
    $hash = Get-BytesSha256 $bytes
    if ($hash -eq $patch.result) { continue }
    $source = $patch.sources.PSObject.Properties[$hash]
    if ($null -eq $source) {
        throw "Unsupported title menu file: $name. Restore it from your original game files and retry."
    }

    foreach ($edit in $source.Value.replace) {
        $offset = [int]$edit[0]
        $hex = [string]$edit[1]
        for ($i = 0; $i -lt $hex.Length; $i += 2) {
            $bytes[$offset++] = [Convert]::ToByte($hex.Substring($i, 2), 16)
        }
    }
    $insert = [int]$source.Value.insert[0]
    $patched = [byte[]]::new($bytes.Length + 1)
    [Array]::Copy($bytes, 0, $patched, 0, $insert)
    $patched[$insert] = [byte]$source.Value.insert[1]
    [Array]::Copy($bytes, $insert, $patched, $insert + 1, $bytes.Length - $insert)
    if ((Get-BytesSha256 $patched) -ne $patch.result) {
        throw "Title menu patch verification failed: $name"
    }
    [pscustomobject]@{ Path = $path; Bytes = $patched }
}

foreach ($file in $prepared) {
    $temp = $file.Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::WriteAllBytes($temp, $file.Bytes)
        [IO.File]::Replace($temp, $file.Path, [NullString]::Value)
    } finally {
        if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp }
    }
}

# Locale XUS (non-EN) returnLib.Text -> Quit Game
function Get-BeUInt16([byte[]]$Buffer, [int]$Offset) {
    return [uint16](([int]$Buffer[$Offset] -shl 8) -bor [int]$Buffer[$Offset + 1])
}
function Write-BeUInt16([System.Collections.Generic.List[byte]]$Out, [uint16]$Value) {
    $Out.Add([byte](($Value -shr 8) -band 0xFF))
    $Out.Add([byte]($Value -band 0xFF))
}
function Write-BeUtf16([System.Collections.Generic.List[byte]]$Out, [string]$Text) {
    Write-BeUInt16 $Out ([uint16]$Text.Length)
    foreach ($ch in $Text.ToCharArray()) {
        $code = [int][char]$ch
        $Out.Add([byte](($code -shr 8) -band 0xFF))
        $Out.Add([byte]($code -band 0xFF))
    }
}
function Read-XusPairs([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 12 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'XUIS') {
        throw "Not an XUIS file: $Path"
    }
    $count = Get-BeUInt16 $bytes 10
    $i = 12
    $pairs = New-Object System.Collections.Generic.List[object]
    for ($n = 0; $n -lt $count; $n++) {
        $l1 = Get-BeUInt16 $bytes $i; $i += 2
        $chars1 = New-Object char[] $l1
        for ($c = 0; $c -lt $l1; $c++) {
            $chars1[$c] = [char](([int]$bytes[$i] -shl 8) -bor [int]$bytes[$i + 1]); $i += 2
        }
        $value = -join $chars1
        $l2 = Get-BeUInt16 $bytes $i; $i += 2
        $chars2 = New-Object char[] $l2
        for ($c = 0; $c -lt $l2; $c++) {
            $chars2[$c] = [char](([int]$bytes[$i] -shl 8) -bor [int]$bytes[$i + 1]); $i += 2
        }
        $key = -join $chars2
        $pairs.Add([pscustomobject]@{ Key = $key; Value = $value })
    }
    return , $pairs
}
function Write-XusPairs([string]$Path, $Pairs) {
    $out = New-Object System.Collections.Generic.List[byte]
    foreach ($b in [Text.Encoding]::ASCII.GetBytes('XUIS')) { $out.Add($b) }
    $out.Add(1); $out.Add(0); $out.Add(0); $out.Add(0)
    Write-BeUInt16 $out 0
    Write-BeUInt16 $out ([uint16]$Pairs.Count)
    foreach ($pair in $Pairs) {
        Write-BeUtf16 $out ([string]$pair.Value)
        Write-BeUtf16 $out ([string]$pair.Key)
    }
    $size = [uint16]$out.Count
    $out[8] = [byte](($size -shr 8) -band 0xFF)
    $out[9] = [byte]($size -band 0xFF)
    [System.IO.File]::WriteAllBytes($Path, $out.ToArray())
}

$xusHits = 0
Get-ChildItem -LiteralPath $frontEnd -Directory -ErrorAction SilentlyContinue | ForEach-Object {
    foreach ($name in @('FE_MAINMENU.XUS', 'FE_MAINMENU_4X3.XUS')) {
        $path = Join-Path $_.FullName $name
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $pairs = @(Read-XusPairs $path)
        $changed = $false
        $rewritten = foreach ($pair in $pairs) {
            if ($pair.Key -match 'tecmo_returnLib\.Text$' -and [string]$pair.Value -cne 'Quit Game') {
                $changed = $true
                [pscustomobject]@{ Key = $pair.Key; Value = 'Quit Game' }
            } else {
                $pair
            }
        }
        if ($changed) {
            Write-XusPairs $path @($rewritten)
            $xusHits++
        }
    }
}

Write-Host ("Title-menu patches applied (xur_updated={0}, locale_xus={1})" -f @($prepared).Count, $xusHits)
if ((Split-Path $PSScriptRoot -Leaf) -eq 'launcher-support') {
    Set-Content -LiteralPath (Join-Path $GameRoot '.nba-launcher-ready') -Value '1' -Encoding ASCII
}
