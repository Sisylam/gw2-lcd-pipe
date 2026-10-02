param(
    [ValidateSet('Register', 'Restore')]
    [string]$Action = 'Register'
)

# Self-elevates so the HKLM write succeeds. All output is teed to
# elevation.log because the elevated child runs in its own console whose
# window we cannot read from the parent.
$log = Join-Path $PSScriptRoot 'elevation.log'
$PSScriptRoot | Out-Null

function Write-Log($m) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Host $line
    Add-Content -LiteralPath $log -Value $line
}

$id = [Security.Principal.WindowsIdentity]::GetCurrent()
$pr = New-Object Security.Principal.WindowsPrincipal($id)
$me = [Security.Principal.WindowsBuiltInRole]::Administrator

Write-Log "--- $Action requested (elevated=$($pr.IsInRole($me))) ---"

if (-not $pr.IsInRole($me)) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass',
                 '-File', ('"{0}"' -f $PSCommandPath), '-Action', $Action)
    Write-Log "relaunching elevated: $Action"
    try {
        $p = Start-Process powershell -Verb RunAs -ArgumentList $argList -PassThru -Wait
        Write-Log "child exited with code $($p.ExitCode)"
    } catch {
        Write-Log "elevation failed: $($_.Exception.Message)"
    }
    return
}

try {
    & (Join-Path $PSScriptRoot 'registry.ps1') -Action $Action 2>&1 |
        ForEach-Object { Write-Log "  $_" }
    Write-Log "done: $Action"
} catch {
    Write-Log "ERROR: $($_.Exception.Message)"
    exit 1
}
