Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NS3 {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr OpenFileMappingW(uint a, bool i, string n);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern IntPtr MapViewOfFile(IntPtr h, uint a, uint l, uint hi, UIntPtr b);
    [DllImport("kernel32.dll")] static extern bool UnmapViewOfFile(IntPtr p);
    public static byte[] Read(string name, int len, out string err) {
        err = null;
        IntPtr h = OpenFileMappingW(0x0004, false, name);
        if (h == IntPtr.Zero) { err = "err " + Marshal.GetLastWin32Error(); return null; }
        IntPtr v = MapViewOfFile(h, 0x0004, 0, 0, (UIntPtr)(uint)len);
        if (v == IntPtr.Zero) { err = "err " + Marshal.GetLastWin32Error(); CloseHandle(h); return null; }
        var buf = new byte[len];
        Marshal.Copy(v, buf, 0, len);
        UnmapViewOfFile(v); CloseHandle(h);
        return buf;
    }
}
'@

$TOTAL = 5460
$err = $null
$b = [NS3]::Read('MumbleLink', $TOTAL, [ref]$err)
if (-not $b) { Write-Output "read failed: $err"; exit 1 }

Write-Output ("uiVersion = {0}   (wiki documents v1 layout)" -f [BitConverter]::ToUInt32($b, 0))
Write-Output ""
Write-Output "=== non-zero regions of the buffer (16-byte rows, offset 0..5440) ==="
for ($row = 0; $row -lt 340; $row++) {
    $off = $row * 16
    $slice = $b[$off..($off + 15)]
    $nonZero = ($slice | Where-Object { $_ -ne 0 }).Count
    if ($nonZero -gt 0) {
        $hex = ($slice | ForEach-Object { "{0:X2}" -f $_ }) -join ' '
        $asc = -join ($slice | ForEach-Object { if ($_ -ge 32 -and $_ -lt 127) { [char]$_ } else { '.' } })
        "  {0,5}  {1}  |{2}|" -f $off, $hex, $asc
    }
}

Write-Output ""
Write-Output "=== scanning for UTF-16LE strings (>=4 printable chars) ==="
$found = 0
$i = 0
while ($i -lt $TOTAL - 8) {
    # try to read a UTF-16LE run starting at i
    $chars = New-Object System.Collections.Generic.List[string]
    $j = $i
    while ($j -lt $TOTAL - 1) {
        $lo = $b[$j]; $hi = $b[$j + 1]
        if ($lo -eq 0 -and $hi -eq 0) { break }
        $code = $lo -bor ($hi -shl 8)
        if ($code -lt 32 -or $code -gt 126) { break }
        $chars.Add([char]$code)
        $j += 2
    }
    if ($chars.Count -ge 4) {
        $s = -join $chars
        if ($s -notmatch '^[?]+$' -and $s -notmatch '^\?{2}$') {
            "  offset {0,5}: {1}" -f $i, $s
            $found++
        }
        $i = $j + 2
    } else {
        $i++
    }
}
if ($found -eq 0) { Write-Output "  (no ASCII-range UTF-16 strings found anywhere)" }

Write-Output ""
Write-Output "=== searching for JSON-like ASCII anywhere ==="
$ascii = -join ($b | ForEach-Object { if ($_ -ge 32 -and $_ -lt 127) { [char]$_ } else { "`n" } })
$chunks = $ascii -split "`n+" | Where-Object { $_.Length -ge 8 }
if ($chunks.Count -eq 0) {
    Write-Output "  (none)"
} else {
    $chunks | Select-Object -First 20 | ForEach-Object { "  $_" }
}
