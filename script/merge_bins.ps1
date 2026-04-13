param(
  [Parameter(Mandatory = $true)][string]$BootBin,
  [Parameter(Mandatory = $true)][string]$AppBin,
  [Parameter(Mandatory = $true)][string]$OutBin,
  [UInt32]$AppOffset = 0x10000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$boot = [System.IO.File]::ReadAllBytes($BootBin)
$app = [System.IO.File]::ReadAllBytes($AppBin)

if ($boot.Length -gt $AppOffset) {
  throw "Bootloader image is larger than the app offset."
}

$merged = New-Object byte[] ($AppOffset + $app.Length)
for ($i = 0; $i -lt $merged.Length; $i++) {
  $merged[$i] = 0xFF
}

[Array]::Copy($boot, 0, $merged, 0, $boot.Length)
[Array]::Copy($app, 0, $merged, $AppOffset, $app.Length)

$outDir = Split-Path -Parent $OutBin
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

[System.IO.File]::WriteAllBytes($OutBin, $merged)
Write-Host "Merged to $OutBin"