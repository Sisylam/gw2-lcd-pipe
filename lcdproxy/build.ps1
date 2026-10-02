param(
    [string]$Out = 'LgLcdApiProxy.dll'
)

$ErrorActionPreference = 'Stop'
$env:PATH = 'C:\w64\bin;' + $env:PATH
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $dir
& 'C:\w64\bin\gcc.exe' -shared -O2 -Wall -std=gnu99 -static-libgcc -o $Out 'lcdproxy.c' -ladvapi32
if ($LASTEXITCODE -ne 0) { throw "gcc failed with $LASTEXITCODE (is the target DLL loaded by a running process?)" }
Get-Item -LiteralPath (Join-Path $dir $Out) |
    Select-Object Name, Length, LastWriteTime | Format-List
