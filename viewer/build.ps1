param(
    [switch]$Debug
)
$ErrorActionPreference = 'Stop'

$cargo = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
if (-not (Test-Path -LiteralPath $cargo)) {
    throw "cargo not found at $cargo - install Rust via https://rustup.rs"
}

$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$profile = if ($Debug) { @() } else { @('--release') }

& $cargo build @profile --manifest-path (Join-Path $dir 'Cargo.toml')
if ($LASTEXITCODE -ne 0) { throw "cargo failed with $LASTEXITCODE" }

$out = if ($Debug) { 'debug' } else { 'release' }
Get-Item (Join-Path $dir "target\$out\gw2lcd-viewer.exe") |
    Select-Object Name, Length, LastWriteTime | Format-List
