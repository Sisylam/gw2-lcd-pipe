Add-Type -AssemblyName System.Windows.Forms

$p = Get-Process Gw2-64 -ErrorAction SilentlyContinue
if (-not $p) { Write-Output "GW2 not running"; exit 1 }

Write-Output "PID: $($p.Id)"

# Working directory of the process
$h = $p.Handle
$len = New-Object System.Text.StringBuilder 1024
$size = 1024
$r = [System.Runtime.InteropServices.Marshal]::ReadInt32([System.Runtime.InteropServices.Marshal]::ReadInt32($h, 0) + 0x38)
Write-Output "PEB read attempt: $r (nonzero means path may be at that address)"

Write-Output ""
Write-Output "--- searching common roots for MumbleLink ---"
$roots = @('S:\Programme\Guild Wars 2', 'C:\Program Files (x86)\Guild Wars 2', 'C:\Program Files\Guild Wars 2', $env:TEMP, $env:USERPROFILE)
foreach ($r in $roots) {
    if (Test-Path -LiteralPath $r) {
        Get-ChildItem -LiteralPath $r -Filter "MumbleLink" -Recurse -Force -ErrorAction SilentlyContinue |
            Select-Object FullName, Length, LastWriteTime | Format-Table -AutoSize
    }
}

Write-Output "--- debug.log: mumble mentions ---"
$log = 'S:\Programme\Guild Wars 2\debug.log'
if (Test-Path -LiteralPath $log) {
    Select-String -LiteralPath $log -Pattern 'mumble' -SimpleMatch -ErrorAction SilentlyContinue |
        Select-Object -Last 15 | ForEach-Object { $_.Line }
}
Write-Output "--- debug.log tail ---"
if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 12 }
