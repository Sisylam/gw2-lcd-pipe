param(
    [ValidateSet('Register', 'Restore', 'Status')]
    [string]$Action = 'Status'
)

$ErrorActionPreference = 'Stop'

$clsid  = '{d0e790a5-01a7-49ae-ae0b-e986bdd0c21b}'
$shim   = Join-Path $PSScriptRoot 'LogitechLcd.dll'
$backup = Join-Path $PSScriptRoot 'registry-backup.json'

# The x64 view is the one GW2.exe (a 64-bit process) will read.
$key    = "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
$subkey = Join-Path $key 'ServerBinary'

function Get-Current {
    if (-not (Test-Path -LiteralPath $subkey)) { return $null }
    (Get-Item -LiteralPath $subkey).GetValue('')
}

switch ($Action) {

    'Status' {
        Write-Output "shim : $shim"
        Write-Output "key  : $subkey"
        Write-Output "value: $(Get-Current)"
        if ((Get-Current) -eq $shim) {
            Write-Output 'STATE: shim is active'
        } else {
            Write-Output 'STATE: stock Logitech SDK is active'
        }
    }

    'Register' {
        if (-not (Test-Path -LiteralPath $shim)) { throw "shim not built: $shim" }

        $id = [Security.Principal.WindowsIdentity]::GetCurrent()
        $pr = New-Object Security.Principal.WindowsPrincipal($id)
        if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'HKLM write needs an elevated PowerShell. Re-run as Administrator.'
        }

        $current = Get-Current
        if ($current -eq $shim) { Write-Output 'already registered'; break }

        # back up whatever is there now so Restore can put it back exactly
        @{ serverBinary = $current; savedAt = (Get-Date).ToString('o') } |
            ConvertTo-Json | Set-Content -LiteralPath $backup -Encoding UTF8
        Write-Output "backed up -> $backup ($current)"

        if (-not (Test-Path -LiteralPath $subkey)) {
            New-Item -Path $subkey -Force | Out-Null
        }
        Set-Item -LiteralPath $subkey -Value $shim
        Write-Output "registered -> $shim"
        Write-Output 'Restart GW2.exe so it re-resolves the CLSID.'
    }

    'Restore' {
        $id = [Security.Principal.WindowsIdentity]::GetCurrent()
        $pr = New-Object Security.Principal.WindowsPrincipal($id)
        if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'HKLM write needs an elevated PowerShell. Re-run as Administrator.'
        }

        if (-not (Test-Path -LiteralPath $backup)) {
            throw "no backup at $backup"
        }
        $saved = (Get-Content -LiteralPath $backup -Raw | ConvertFrom-Json).serverBinary

        if ([string]::IsNullOrEmpty($saved)) {
            if (Test-Path -LiteralPath $subkey) { Remove-Item -LiteralPath $subkey -Force }
            Write-Output 'removed ServerBinary (there was none originally)'
        } else {
            if (-not (Test-Path -LiteralPath $subkey)) { New-Item -Path $subkey -Force | Out-Null }
            Set-Item -LiteralPath $subkey -Value $saved
            Write-Output "restored -> $saved"
        }
        Write-Output 'Restart GW2.exe so it re-resolves the CLSID.'
    }
}
