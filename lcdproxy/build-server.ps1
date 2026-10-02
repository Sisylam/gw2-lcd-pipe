param(
    [string]$Out = 'lgpipe_server.exe'
)

$ErrorActionPreference = 'Stop'
$env:PATH = 'C:\w64\bin;' + $env:PATH
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $dir
& 'C:\w64\bin\gcc.exe' -O2 -Wall -std=gnu99 -static-libgcc -o $Out 'lgpipe_server.c'
if ($LASTEXITCODE -ne 0) { throw "gcc failed with $LASTEXITCODE" }
Get-Item -LiteralPath (Join-Path $dir $Out) |
    Select-Object Name, Length, LastWriteTime | Format-List
