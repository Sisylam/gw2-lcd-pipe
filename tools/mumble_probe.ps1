Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class NamedSection {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr OpenFileMappingW(uint access, bool inherit, string name);

    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool CloseHandle(IntPtr h);

    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr MapViewOfFile(IntPtr h, uint access, uint offLow, uint offHigh, UIntPtr bytes);

    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool UnmapViewOfFile(IntPtr p);

    // FILE_MAP_READ = 0x0004
    public static byte[] Read(string name, int length, out string err) {
        err = null;
        IntPtr h = OpenFileMappingW(0x0004, false, name);
        if (h == IntPtr.Zero) {
            err = "OpenFileMapping failed, error " + Marshal.GetLastWin32Error()
                + " (2 = file not found, 5 = access denied)";
            return null;
        }
        IntPtr v = MapViewOfFile(h, 0x0004, 0, 0, (UIntPtr)(uint)length);
        if (v == IntPtr.Zero) {
            err = "MapViewOfFile failed, error " + Marshal.GetLastWin32Error();
            CloseHandle(h);
            return null;
        }
        var buf = new byte[length];
        Marshal.Copy(v, buf, 0, length);
        UnmapViewOfFile(v);
        CloseHandle(h);
        return buf;
    }
}
'@

function Read-Wide([byte[]]$b, [int]$off, [int]$len) {
    $raw = New-Object byte[] $len
    [Array]::Copy($b, $off, $raw, 0, $len)
    # Trim on a UTF-16 code-unit boundary (2-byte aligned). A 0x00 as the
    # high byte of a character is data, not a terminator.
    $end = 0
    while (($end + 1) -lt $raw.Length) {
        if ($raw[$end] -eq 0 -and $raw[$end + 1] -eq 0) { break }
        $end += 2
    }
    if ($end -eq 0) { return "" }
    $t = New-Object byte[] $end
    [Array]::Copy($raw, 0, $t, 0, $end)
    [System.Text.Encoding]::Unicode.GetString($t)
}

$OFF_TICK = 4
$OFF_AVATAR = 8
$OFF_NAME = 44
$OFF_IDENTITY = 592
$OFF_CTX_LEN = 1104
$BASE_CTX = 1108
$OFF_DESC = 1364
$TOTAL = 5460

$CTX = [ordered]@{
    MapId = 'UInt32'; MapType = 'UInt32'; ShardId = 'UInt32'; Instance = 'UInt32'
    BuildId = 'UInt32'; UiState = 'UInt32'
    CompassWidth = 'UInt16'; CompassHeight = 'UInt16'
    CompassRotation = 'Single'
    PlayerX = 'Single'; PlayerY = 'Single'
    MapCenterX = 'Single'; MapCenterY = 'Single'
    MapScale = 'Single'
    ProcessId = 'UInt32'; MountIndex = 'Byte'
}
$CTX_OFF = @{
    MapId = 28; MapType = 32; ShardId = 36; Instance = 40; BuildId = 44; UiState = 48
    CompassWidth = 52; CompassHeight = 54; CompassRotation = 56
    PlayerX = 60; PlayerY = 64; MapCenterX = 68; MapCenterY = 72; MapScale = 76
    ProcessId = 80; MountIndex = 84
}

Write-Output "Opening named section \BaseNamedObjects\MumbleLink ..."
$err = $null
$buf = [NamedSection]::Read('MumbleLink', $TOTAL, [ref]$err)

if (-not $buf) {
    Write-Output ""
    Write-Output "FAILED: $err"
    Write-Output ""
    Write-Output "MumbleLink is a Win32 named section, not a file on disk."
    Write-Output "It exists only while GW2 has a character loaded."
    Write-Output ""
    $p = Get-Process Gw2-64, Gw2 -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($p) { Write-Output "GW2 client : RUNNING (PID $($p.Id), started $($p.StartTime))" }
    else { Write-Output "GW2 client : NOT RUNNING" }
    exit 1
}

Write-Output "OK. Read $TOTAL bytes."
Write-Output ""
$ver = [BitConverter]::ToUInt32($buf, 0)
Write-Output ("uiVersion   : {0}{1}" -f $ver, $(if ($ver -ne 2) { "  (current GW2 uses 2)" } else { "" }))
Write-Output ("uiTick      : {0}" -f [BitConverter]::ToUInt32($buf, $OFF_TICK))
Write-Output ("context_len : {0}" -f [BitConverter]::ToUInt32($buf, $OFF_CTX_LEN))
Write-Output ("name        : {0}" -f (Read-Wide $buf $OFF_NAME 512))

$identity = Read-Wide $buf $OFF_IDENTITY 512
Write-Output ""
Write-Output "=== identity (JSON) ==="
Write-Output $identity
try {
    $j = $identity | ConvertFrom-Json
    Write-Output ""
    Write-Output "parsed fields:"
    $j.PSObject.Properties | ForEach-Object { "  {0,-16} = {1}" -f $_.Name, $_.Value }
} catch {
    Write-Output "(not parseable as JSON - GW2 may not have written it yet)"
}

Write-Output ""
Write-Output "=== description ==="
Write-Output (Read-Wide $buf $OFF_DESC 4096)

Write-Output ""
Write-Output "=== context ==="
foreach ($k in $CTX.Keys) {
    $o = $BASE_CTX + $CTX_OFF[$k]
    switch ($CTX[$k]) {
        'UInt16' { $v = [BitConverter]::ToUInt16($buf, $o) }
        'Byte'   { $v = $buf[$o] }
        'Single' { $v = [math]::Round([BitConverter]::ToSingle($buf, $o), 4) }
        default  { $v = [BitConverter]::ToUInt32($buf, $o) }
    }
    "  {0,-15} = {1}" -f $k, $v
}

$uiState = [BitConverter]::ToUInt32($buf, $BASE_CTX + $CTX_OFF.UiState)
$bits = @{
    1 = 'Map is open'; 2 = 'Compass at top-left'; 3 = 'Compass rotation enabled'
    4 = 'GW2 window has focus'; 5 = 'Competitive mode'; 6 = 'Textbox has focus'
    7 = 'In combat (per wiki)'
}
Write-Output ""
Write-Output ("uiState = {0} (0x{1:X})" -f $uiState, $uiState)
foreach ($b in 1..7) {
    if ($uiState -band (1 -shl ($b - 1))) { "  bit $b : SET  - $($bits[$b])" }
}

Write-Output ""
Write-Output ("avatarPosition : x={0} y={1} z={2}" -f `
    [math]::Round([BitConverter]::ToSingle($buf, 8), 1),
    [math]::Round([BitConverter]::ToSingle($buf, 12), 1),
    [math]::Round([BitConverter]::ToSingle($buf, 16), 1))

Write-Output ""
Write-Output "=== live tick check (3 samples) ==="
$ticks = @()
for ($i = 0; $i -lt 3; $i++) {
    Start-Sleep -Milliseconds 500
    $e2 = $null
    $b2 = [NamedSection]::Read('MumbleLink', $TOTAL, [ref]$e2)
    if ($b2) {
        $t = [BitConverter]::ToUInt32($b2, $OFF_TICK)
        $ticks += $t
        $ax = [math]::Round([BitConverter]::ToSingle($b2, 8), 1)
        $az = [math]::Round([BitConverter]::ToSingle($b2, 16), 1)
        "  tick={0,-10} pos=({1}, {2})" -f $t, $ax, $az
    } else { "  read failed: $e2" }
}

Write-Output ""
if ($ticks.Count -ge 2) {
    $delta = $ticks[-1] - $ticks[0]
    if ($delta -gt 0) {
        Write-Output "STATUS: IN WORLD  (tick advancing, +$delta over 1s)"
        Write-Output "  character identity should be available above."
    } else {
        Write-Output "STATUS: NOT IN WORLD  (tick FROZEN at $($ticks[0]))"
        Write-Output "  GW2 publishes no character data outside the world."
        Write-Output "  Select a character and enter the game, then re-run."
    }
}
