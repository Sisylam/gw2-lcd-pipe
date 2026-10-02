param(
    [string]$Out = 'LgLcdApi.clean.dll',
    [switch]$Debug
)

$ErrorActionPreference = 'Stop'
$env:PATH = 'C:\w64\bin;' + $env:PATH
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $dir
$defs = @()
if ($Debug) { $defs += '-DCLEAN_DEBUG' }
& 'C:\w64\bin\gcc.exe' -shared -O2 -Wall -std=gnu99 -static-libgcc @defs -o $Out 'cleanapi.c'
if ($LASTEXITCODE -ne 0) { throw "gcc failed with $LASTEXITCODE" }
Get-Item -LiteralPath (Join-Path $dir $Out) |
    Select-Object Name, Length, LastWriteTime | Format-List
