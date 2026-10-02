$ErrorActionPreference = 'Stop'
$env:PATH = "C:\w64\bin;" + $env:PATH

$gcc = 'C:\w64\bin\gcc.exe'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path

$inc = @(
  '-I', 'C:\w64\x86_64-w64-mingw32\include',
  '-I', 'C:\w64\include',
  '-I', 'C:\w64\lib64\gcc\x86_64-w64-mingw32\4.8.2\include'
)
$lib = @('-L', 'C:\w64\x86_64-w64-mingw32\lib', '-L', 'C:\w64\lib64')

$args = @('-shared', '-O2', '-Wall', '-static-libgcc') + $inc + $lib + @(
  '-o', "$dir\LogitechLcd.dll",
  "$dir\LogitechLcdShim.c", "$dir\exports.c"
)

& $gcc @args
if ($LASTEXITCODE -ne 0) { throw "gcc failed with exit $LASTEXITCODE" }
Write-Output "built $dir\LogitechLcd.dll"
Get-Item "$dir\LogitechLcd.dll" | Select-Object Name, Length, LastWriteTime | Format-List
