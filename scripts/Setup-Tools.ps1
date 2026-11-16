# Restore the pinned project-local tools. Requires network access on a fresh checkout.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$lock = Get-Content "$projectRoot/config/tools.lock.json" -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force "$projectRoot/tools" | Out-Null
foreach ($artifact in $lock.artifacts) {
    $download = Join-Path "$projectRoot/tools" $artifact.file
    if (-not (Test-Path $download)) { Invoke-WebRequest $artifact.url -OutFile $download }
    if ((Get-FileHash $download -Algorithm SHA256).Hash.ToLower() -ne $artifact.sha256) {
        throw "Checksum mismatch: $($artifact.name)"
    }
    $destination = Join-Path "$projectRoot/tools" $artifact.destination
    if ($artifact.kind -eq 'file') {
        New-Item -ItemType Directory -Force (Split-Path $destination -Parent) | Out-Null
        Copy-Item -LiteralPath $download -Destination $destination -Force
    } elseif ($artifact.name -eq 'LLVM') {
        if (-not (Test-Path "$destination/bin/clang++.exe")) {
            $install = Start-Process -FilePath $download -ArgumentList '/S',"/D=$destination" -WindowStyle Hidden -Wait -PassThru
            if ($install.ExitCode -ne 0) { throw 'LLVM installation failed.' }
        }
    } elseif (-not (Test-Path $destination)) {
        Expand-Archive -LiteralPath $download -DestinationPath $destination
    }
}
foreach ($source in $lock.sources) {
    $checkout = Join-Path "$projectRoot/tools" $source.name
    if (-not (Test-Path $checkout)) {
        & git clone $source.url $checkout
        if ($LASTEXITCODE -ne 0) { throw "Clone failed: $($source.name)" }
        & git -C $checkout checkout --detach $source.commit
        if ($LASTEXITCODE -ne 0) { throw "Checkout failed: $($source.name)" }
    }
    $actualCommit = & git -C $checkout rev-parse HEAD
    if ($actualCommit -ne $source.commit) { throw "Source revision mismatch: $($source.name)" }
}
Write-Output 'Pinned tools are ready. Run scripts/Build-GhidraLoader.ps1 to build the Ghidra extension.'
