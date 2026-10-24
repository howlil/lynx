# LYNX native runtime sampling. No administrator rights required.
# Example:
#   powershell -ExecutionPolicy Bypass -File scripts/measure-runtime.ps1 -Seconds 120
# Run separately at stable ambient idle and while an animation/activity is live.
param(
    [ValidateRange(10,3600)][int]$Seconds = 120,
    [ValidateRange(1,60)][int]$IntervalSeconds = 5,
    [string]$ProcessName = "lynx_m0",
    [string]$OutputCsv = "lynx-runtime-samples.csv"
)

$processes = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
if ($processes.Count -ne 1) {
    throw "Expected exactly one '$ProcessName' process, found $($processes.Count). Launch LYNX or close duplicates."
}
$processId = $processes[0].Id
$cpuCount = [Environment]::ProcessorCount
$rows = [System.Collections.Generic.List[object]]::new()
$timer = [System.Diagnostics.Stopwatch]::StartNew()
$previousSeconds = 0.0
$previousCpu = $processes[0].TotalProcessorTime.TotalSeconds

Write-Host "LYNX sampling pid=$processId, processors=$cpuCount, seconds=$Seconds"
Write-Host "Do not move the mouse onto Island or change focus during the idle run."

while ($timer.Elapsed.TotalSeconds -lt $Seconds) {
    $remaining = $Seconds - $timer.Elapsed.TotalSeconds
    Start-Sleep -Milliseconds ([int]([Math]::Min($IntervalSeconds, $remaining) * 1000))
    $current = Get-Process -Id $processId -ErrorAction SilentlyContinue
    if ($null -eq $current -or $current.HasExited) {
        throw "LYNX process exited or restarted during sampling. Discard incomplete results."
    }
    $nowSeconds = $timer.Elapsed.TotalSeconds
    $elapsed = $nowSeconds - $previousSeconds
    $cpu = $current.TotalProcessorTime.TotalSeconds
    $cpuDelta = [Math]::Max(0.0, $cpu - $previousCpu)
    $rows.Add([pscustomobject]@{
        Timestamp = (Get-Date).ToString("o")
        Seconds = [Math]::Round($nowSeconds, 2)
        CpuOneCorePercent = [Math]::Round(100 * $cpuDelta / $elapsed, 3)
        CpuWholeMachinePercent = [Math]::Round(100 * $cpuDelta / $elapsed / $cpuCount, 3)
        WorkingSetMB = [Math]::Round($current.WorkingSet64 / 1MB, 2)
        PrivateMemoryMB = [Math]::Round($current.PrivateMemorySize64 / 1MB, 2)
        Handles = $current.HandleCount
    })
    $previousSeconds = $nowSeconds
    $previousCpu = $cpu
}
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $OutputCsv
$meanCpu = ($rows | Measure-Object -Property CpuWholeMachinePercent -Average).Average
$maxWorkingSet = ($rows | Measure-Object -Property WorkingSetMB -Maximum).Maximum
Write-Host ("Mean CPU (whole machine): {0:N3}% | Max working set: {1:N2} MB" -f $meanCpu,$maxWorkingSet)
Write-Host "Saved raw samples: $OutputCsv"
Write-Host "CPU/WS alone do not prove GPU, frame pacing, native UIA or click-through. Use WPR/WPA for those."
