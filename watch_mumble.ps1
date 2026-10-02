Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NS2 {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr OpenFileMappingW(uint a, bool i, string n);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern IntPtr MapViewOfFile(IntPtr h, uint a, uint l, uint hi, UIntPtr b);
    [DllImport("kernel32.dll")] static extern bool UnmapViewOfFile(IntPtr p);
    public static byte[] Read(string name, int len, out string err) {
        err = null;
        IntPtr h = OpenFileMappingW(0x0004, false, name);
        if (h == IntPtr.Zero) { err = "OpenFileMapping err " + Marshal.GetLastWin32Error(); return null; }
        IntPtr v = MapViewOfFile(h, 0x0004, 0, 0, (UIntPtr)(uint)len);
        if (v == IntPtr.Zero) { err = "MapViewOfFile err " + Marshal.GetLastWin32Error(); CloseHandle(h); return null; }
        var buf = new byte[len];
        Marshal.Copy(v, buf, 0, len);
        UnmapViewOfFile(v); CloseHandle(h);
        return buf;
    }
}
'@

function Read-Wide([byte[]]$b, [int]$off, [int]$len) {
    $raw = New-Object byte[] $len
    [Array]::Copy($b, $off, $raw, 0, $len)
    $end = 0
    while ($end -lt $raw.Length -and $raw[$end] -ne 0) { $end++ }
    if ($end -eq 0) { return "" }
    $t = New-Object byte[] $end
    [Array]::Copy($raw, 0, $t, 0, $end)
    [System.Text.Encoding]::Unicode.GetString($t)
}

$TOTAL = 5460
$BASE_CTX = 1108

Write-Output "Watching MumbleLink for 20s. Keep GW2 in the foreground if you can."
Write-Output ""

$prevId = $null
for ($i = 0; $i -lt 10; $i++) {
    $err = $null
    $b = [NS2]::Read('MumbleLink', $TOTAL, [ref]$err)
    if (-not $b) { Write-Output "read failed: $err"; break }

    $tick = [BitConverter]::ToUInt32($b, 4)
    $name = Read-Wide $b 44 512
    $id = Read-Wide $b 592 512
    $uiState = [BitConverter]::ToUInt32($b, $BASE_CTX + 48)
    $focus = ($uiState -band 8) -ne 0
    $inCombat = ($uiState -band 64) -ne 0
    $mapOpen = ($uiState -band 1) -ne 0
    $mapId = [BitConverter]::ToUInt32($b, $BASE_CTX + 28)

    $idChanged = if ($id -ne $prevId) { " <<< IDENTITY CHANGED" } else { "" }
    $prevId = $id

    Write-Output ("tick={0,-8} mapId={1,-4} focus={2,-5} combat={3,-5} mapOpen={4,-5} name='{5}' idLen={6}{7}" -f `
        $tick, $mapId, $focus, $inCombat, $mapOpen, $name, $id.Length, $idChanged)

    if ($id -and $i -eq 0) {
        Write-Output ""
        Write-Output "--- identity JSON ---"
        Write-Output $id
        Write-Output ""
        try {
            $j = $id | ConvertFrom-Json
            $j.PSObject.Properties | ForEach-Object { "  {0,-16} = {1}" -f $_.Name, $_.Value }
        } catch { Write-Output "(JSON parse failed)" }
        Write-Output ""
    }

    Start-Sleep -Seconds 2
}
