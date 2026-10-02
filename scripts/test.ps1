# Runs the automated tests.
#
#   powershell -ExecutionPolicy Bypass -File scripts\test.ps1
#
# The clean-DLL test shares Local\GW2LCDShim with any running instance, so close
# GW2 first. The server mock starts and stops lgpipe_server4.exe itself (build
# it with lcdproxy\build-server.ps1 first).
#
# What is NOT covered here and must be checked by hand: the real GW2 handshake,
# and the physical/virtual LCD actually showing the frames.

$ErrorActionPreference = 'Stop'
$root   = Split-Path -Parent $PSScriptRoot
$env:PATH = 'C:\w64\bin;' + $env:PATH
$gcc    = 'C:\w64\bin\gcc.exe'
$cargo  = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
$tmp    = Join-Path $env:TEMP 'gw2lcd-tests'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$script:fail = 0

function Step([string]$name, [scriptblock]$body) {
    Write-Host "`n=== $name ==="
    try {
        & $body
        if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne $null) {
            Write-Host "STEP FAILED (exit $LASTEXITCODE)"
            $script:fail++
        }
    } catch {
        Write-Host "STEP ERROR: $_"
        $script:fail++
    }
}

Step 'Build clean DLL' {
    # Build to a temp path, not over the deployed DLL (which GW2 may hold open).
    & $gcc -shared -O2 -Wall -std=gnu99 -static-libgcc `
        -o (Join-Path $tmp 'LgLcdApi.clean.dll') (Join-Path $root 'clean\cleanapi.c')
}

Step 'Clean DLL integration test' {
    $exe = Join-Path $tmp 'test_cleanapi.exe'
    & $gcc -O2 -Wall -o $exe (Join-Path $root 'clean\test_cleanapi.c')
    & $exe (Join-Path $tmp 'LgLcdApi.clean.dll')
}

Step 'Viewer unit tests' {
    Push-Location (Join-Path $root 'viewer')
    try { & $cargo test } finally { Pop-Location }
}

Step 'Server mock test' {
    $exe = Join-Path $root 'lcdproxy\test_server.exe'
    $srv = Join-Path $root 'lcdproxy\lgpipe_server4.exe'
    if (-not (Test-Path $srv)) { throw "build $srv first (lcdproxy\build-server.ps1)" }
    & $gcc -O2 -Wall -o $exe (Join-Path $root 'lcdproxy\test_server.c')
    $proc = Start-Process -FilePath $srv -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 800
    try { & $exe } finally { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
}

Write-Host ""
if ($script:fail -eq 0) {
    Write-Host 'ALL TESTS PASSED'
} else {
    Write-Host "$script:fail STEP(S) FAILED"
    exit 1
}
