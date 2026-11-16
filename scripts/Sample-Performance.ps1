[CmdletBinding()]
param(
    [Parameter(Mandatory)][int]$GameProcessId,
    [ValidateRange(3, 300)][int]$Samples = 20,
    [Parameter(Mandatory)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$logicalProcessors = [Environment]::ProcessorCount
$game = Get-Process -Id $GameProcessId
$previousCpu = $game.TotalProcessorTime.TotalSeconds
$timer = [Diagnostics.Stopwatch]::StartNew()
$previousTime = $timer.Elapsed.TotalSeconds
$threadStart = @{}
foreach ($thread in $game.Threads) { $threadStart[$thread.Id] = $thread.TotalProcessorTime.TotalSeconds }
$records = @()
for ($sample = 0; $sample -lt $Samples; $sample++) {
    Start-Sleep -Milliseconds 1000
    $game.Refresh()
    if ($game.HasExited) { break }
    $now = $timer.Elapsed.TotalSeconds
    $cpu = $game.TotalProcessorTime.TotalSeconds
    $cores = ($cpu - $previousCpu) / ($now - $previousTime)
    $engines = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine |
        Where-Object Name -Like "pid_${GameProcessId}_*")
    $gpuMemory = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory |
        Where-Object Name -Like "pid_${GameProcessId}_*")
    $records += [pscustomobject]@{
        elapsed_seconds = [math]::Round($now, 3)
        cpu_cores = [math]::Round($cores, 3)
        cpu_machine_percent = [math]::Round(100 * $cores / $logicalProcessors, 2)
        gpu_engines = @($engines | Select-Object Name, UtilizationPercentage)
        gpu_memory = @($gpuMemory | Select-Object Name, DedicatedUsage, SharedUsage)
    }
    $previousCpu = $cpu
    $previousTime = $now
}
$threadCosts = @()
$game.Refresh()
if (-not $game.HasExited) {
    foreach ($thread in $game.Threads) {
        if ($threadStart.ContainsKey($thread.Id)) {
            $threadCosts += [pscustomobject]@{
                thread_id = $thread.Id
                cpu_seconds = [math]::Round($thread.TotalProcessorTime.TotalSeconds - $threadStart[$thread.Id], 4)
            }
        }
    }
}
$report = [ordered]@{
    process_id = $GameProcessId
    logical_processors = $logicalProcessors
    duration_seconds = [math]::Round($timer.Elapsed.TotalSeconds, 3)
    note = 'CPU cores uses 1.0 for one fully occupied logical CPU. GPU engine percentages must not be summed across overlapping engines. Label the scene separately.'
    samples = $records
    threads = @($threadCosts | Sort-Object cpu_seconds -Descending)
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Output "Saved $($records.Count) samples to $OutputPath"
Write-Output "Mean occupied CPU cores: $(($records | Measure-Object cpu_cores -Average).Average)"
$threadCosts | Sort-Object cpu_seconds -Descending | Select-Object -First 8 | Format-Table
