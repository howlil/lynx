# Compare two opt-in LYNX native renderer CPU traces. Both must have
# been produced on the same hardware, DPI, visible state and duration.
param(
  [Parameter(Mandatory=$true)][string]$Baseline,
  [Parameter(Mandatory=$true)][string]$Candidate
)
function Read-Metrics([string]$path) {
  $rows=@(Import-Csv -Path $path)
  if($rows.Count -ne 1) { throw "Expected exactly one renderer summary row in $path" }
  $row=$rows[0]
  if([double]$row.total_draw_calls -lt 10) {
    throw "Only $($row.total_draw_calls) draws in $path; cannot compare sample reliably."
  }
  return $row
}
function Delta([double]$base,[double]$current) {
  if($base -le 0) { return "n/a" }
  return ('{0:+0.0;-0.0;0.0}%' -f (100*($current-$base)/$base))
}
$a=Read-Metrics $Baseline
$b=Read-Metrics $Candidate
Write-Host 'LYNX renderer CPU-side comparison (lower draw latency is better)'
Write-Host ('Baseline : {0}, draws={1}' -f $a.renderer,$a.total_draw_calls)
Write-Host ('Candidate: {0}, draws={1}' -f $b.renderer,$b.total_draw_calls)
foreach($metric in @('mean_cpu_ms','p95_recent_cpu_ms','max_cpu_ms')) {
  $one=[double]$a.$metric
  $two=[double]$b.$metric
  Write-Host ('{0}: {1:N3} ms → {2:N3} ms ({3})' -f $metric,$one,$two,(Delta $one $two))
}
Write-Warning 'Only CPU-side render cost. Does NOT measure GPU or display latency/frame pacing.'
Write-Warning 'If hardware, DPI, workloads or draw counts differ, comparison is not causal.'
