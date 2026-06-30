$ErrorActionPreference = "SilentlyContinue"

$inputText = [Console]::In.ReadToEnd()
if ([string]::IsNullOrWhiteSpace($inputText)) {
  exit 0
}

try {
  $payload = $inputText | ConvertFrom-Json
} catch {
  exit 0
}

function To-Int($value, $fallback = 0) {
  if ($null -eq $value) { return $fallback }
  try { return [int][math]::Round([double]$value) } catch { return $fallback }
}

function Format-Reset($epoch) {
  if ($null -eq $epoch) { return $null }
  try {
    return ([DateTimeOffset]::FromUnixTimeSeconds([int64]$epoch)).ToLocalTime().ToString("HH:mm")
  } catch {
    return $null
  }
}

$five = $payload.rate_limits.five_hour
$week = $payload.rate_limits.seven_day
$context = $payload.context_window

$percent = $null
$display = $null
$label = $null
$resetAt = $null
$status = "context"

if ($null -ne $five -and $null -ne $five.used_percentage) {
  $percent = To-Int $five.used_percentage
  $resetLocal = Format-Reset $five.resets_at
  $display = "$percent%"
  $label = if ($resetLocal) { "5h reset $resetLocal" } else { "5h" }
  $resetAt = $five.resets_at
  $status = "5h"
} elseif ($null -ne $week -and $null -ne $week.used_percentage) {
  $percent = To-Int $week.used_percentage
  $resetLocal = Format-Reset $week.resets_at
  $display = "$percent%"
  $label = if ($resetLocal) { "7d reset $resetLocal" } else { "7d" }
  $resetAt = $week.resets_at
  $status = "7d"
} else {
  $percent = To-Int $context.used_percentage
  $display = "$percent%"
  $label = "contexto"
}

$tokens = 0
if ($null -ne $context) {
  $tokens = (To-Int $context.total_input_tokens) + (To-Int $context.total_output_tokens)
}

$out = [ordered]@{
  generated_at = ([DateTimeOffset]::UtcNow.ToString("o"))
  status = $status
  display_value = $display
  percent_used = $percent
  limit_label = $label
  reset_at = $resetAt
  tokens_used = $tokens
  model = $payload.model.display_name
  source = "claude_statusline"
}

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$outPath = Join-Path $projectRoot "server\claude_status.json"
$json = $out | ConvertTo-Json -Depth 8
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($outPath, $json, $utf8NoBom)

if ($label) {
  Write-Output "Claude $display $label"
} else {
  Write-Output "Claude $display"
}
