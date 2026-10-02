Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class Peb {
    [DllImport("ntdll.dll")]
    static extern int NtQueryInformationProcess(
        IntPtr h, int cls, ref PROCESS_BASIC_INFORMATION pbi, int len, out int ret);

    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_BASIC_INFORMATION {
        public IntPtr Reserved1;
        public IntPtr PebBaseAddress;
        public IntPtr Reserved2_0, Reserved2_1;
        public IntPtr UniqueProcessId;
        public IntPtr Reserved3;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadProcessMemory(
        IntPtr h, IntPtr addr, byte[] buf, int size, out int read);

    static IntPtr ReadPtr(IntPtr h, IntPtr addr) {
        var b = new byte[IntPtr.Size];
        int r;
        if (!ReadProcessMemory(h, addr, b, b.Length, out r)) return IntPtr.Zero;
        return new IntPtr(BitConverter.ToInt64(b, 0));
    }

    static string ReadUnicode(IntPtr h, IntPtr addr) {
        var bytes = new byte[1024];
        int r;
        if (!ReadProcessMemory(h, addr, bytes, bytes.Length, out r)) return null;
        int end = 0;
        for (int i = 0; i + 1 < bytes.Length; i += 2) {
            if (bytes[i] == 0 && bytes[i+1] == 0) { end = i; break; }
            end = i + 2;
        }
        return Encoding.Unicode.GetString(bytes, 0, end);
    }

    public static string GetCwd(int pid) {
        var h = System.Diagnostics.Process.GetProcessById(pid).Handle;
        var pbi = new PROCESS_BASIC_INFORMATION();
        int ret;
        if (NtQueryInformationProcess(h, 0, ref pbi, Marshal.SizeOf(pbi), out ret) != 0)
            return "<NtQuery failed>";
        IntPtr peb = pbi.PebBaseAddress;
        if (peb == IntPtr.Zero) return "<no PEB>";
        // x64: ProcessParameters at PEB+0x20
        IntPtr paramsPtr = ReadPtr(h, peb + 0x20);
        if (paramsPtr == IntPtr.Zero) return "<no ProcessParameters>";
        // RTL_USER_PROCESS_PARAMETERS.CurrentDirectory: CurDir at +0x38 (CURDIR{DosPath UNICODE_STRING, Handle})
        // UNICODE_STRING: ushort Length, ushort MaxLen, IntPtr Buffer
        IntPtr curDir = paramsPtr + 0x38;
        var us = new byte[16];
        int r;
        if (!ReadProcessMemory(h, curDir, us, 16, out r)) return "<read curdir failed>";
        ushort len = BitConverter.ToUInt16(us, 0);
        IntPtr buf = new IntPtr(BitConverter.ToInt64(us, 8));
        if (buf == IntPtr.Zero) return "<null curdir buffer>";
        return ReadUnicode(h, buf);
    }
}
'@

$proc = Get-Process Gw2-64, Gw2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Output "GW2 not running"; exit 1 }

Write-Output "PID       : $($proc.Id)"
Write-Output "Exe       : $($proc.Path)"
try {
    $cwd = [Peb]::GetCwd($proc.Id)
    Write-Output "CWD       : $cwd"
    if ($cwd -and $cwd -notlike '<*') {
        $candidate = Join-Path $cwd 'MumbleLink'
        Write-Output ""
        Write-Output "Candidate : $candidate"
        if (Test-Path -LiteralPath $candidate) {
            Write-Output "FOUND     : $((Get-Item -LiteralPath $candidate).Length) bytes"
        } else {
            Write-Output "Status    : not present"
        }
    }
} catch {
    Write-Output "CWD read failed: $($_.Exception.Message)"
}

Write-Output ""
Write-Output "--- Mumble's own view (log) ---"
$mLog = Join-Path $env:APPDATA 'Mumble\mumble.log'
if (Test-Path -LiteralPath $mLog) {
    Write-Output "log: $mLog  (modified $((Get-Item -LiteralPath $mLog).LastWriteTime))"
    Select-String -LiteralPath $mLog -Pattern 'MumbleLink|mumble link|GW2|Guild' -ErrorAction SilentlyContinue |
        Select-Object -Last 12 | ForEach-Object { $_.Line }
} else {
    Write-Output "no Mumble log at $mLog"
    Get-ChildItem (Join-Path $env:APPDATA 'Mumble') -ErrorAction SilentlyContinue | Select-Object Name, LastWriteTime
}
