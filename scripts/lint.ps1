# Runs the linters over the C and Rust code.
#
#   powershell -ExecutionPolicy Bypass -File scripts\lint.ps1
#
# gcc is taken from C:\w64\bin if present, else from PATH (CI). cppcheck and the
# Rust tools are skipped with a notice if not installed.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
# gcc's cc1 needs the MinGW DLLs, which live next to gcc.exe, on PATH.
if (Test-Path 'C:\w64\bin') { $env:PATH = 'C:\w64\bin;' + $env:PATH }

function Find-Tool([string]$local, [string]$name) {
    if ($local -and (Test-Path $local)) { return $local }
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    return $null
}

$gcc = Find-Tool 'C:\w64\bin\gcc.exe' 'gcc'
$cppcheck = Find-Tool 'C:\Program Files\Cppcheck\cppcheck.exe' 'cppcheck'
$cargo = Find-Tool (Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe') 'cargo'

$script:fail = 0
function Step([string]$name, [scriptblock]$body) {
    Write-Host "`n=== $name ==="
    try { & $body }
    catch { Write-Host "STEP FAILED: $_"; $script:fail++ }
}

$cFiles = @(
    'lcdproxy\lgpipe_server.c', 'lcdproxy\lcdproxy.c', 'clean\cleanapi.c',
    'clean\test_cleanapi.c', 'lcdproxy\test_server.c',
    'shim\LogitechLcdShim.c', 'shim\exports.c'
) | ForEach-Object { Join-Path $root $_ } | Where-Object { Test-Path $_ }

Step 'C: gcc -Wall -Wextra' {
    if (-not $gcc) { throw 'gcc not found' }
    foreach ($f in $cFiles) {
        Write-Host "-- $(Split-Path -Leaf $f)"
        & $gcc -fsyntax-only -std=gnu99 -Wall -Wextra $f
        if ($LASTEXITCODE -ne 0) { throw "gcc reported problems in $(Split-Path -Leaf $f)" }
    }
}

Step 'C: cppcheck' {
    if (-not $cppcheck) { Write-Host 'cppcheck not installed - skipped'; return }
    & $cppcheck --enable=warning,performance,portability --std=c99 `
        --suppress=missingIncludeSystem --inline-suppr --error-exitcode=1 `
        --template='{file}:{line}: {severity}: {message} [{id}]' $cFiles
    if ($LASTEXITCODE -ne 0) { throw "cppcheck findings (exit $LASTEXITCODE)" }
}

Step 'Rust: clippy' {
    if (-not $cargo) { Write-Host 'cargo not found - skipped'; return }
    Push-Location (Join-Path $root 'viewer')
    try { & $cargo clippy --all-targets -- -D warnings } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw 'clippy findings' }
}

Step 'Rust: fmt --check' {
    if (-not $cargo) { Write-Host 'cargo not found - skipped'; return }
    Push-Location (Join-Path $root 'viewer')
    try { & $cargo fmt -- --check } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw 'rustfmt would reformat files' }
}

Write-Host ""
if ($script:fail -eq 0) {
    Write-Host 'LINT OK'
} else {
    Write-Host "$script:fail STEP(S) FAILED"
    exit 1
}
