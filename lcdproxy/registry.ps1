param(
    [ValidateSet('Register', 'Restore', 'Status')]
    [string]$Action = 'Status'
)

$ErrorActionPreference = 'Stop'

$proxy  = Join-Path $PSScriptRoot 'LgLcdApiProxy.dll'
$backup = Join-Path $PSScriptRoot 'registry-backup.json'

# GW2 resolves this class, loads the DLL, then calls GetInterface.
$liveKey = 'HKLM:\SOFTWARE\Classes\CLSID\{FE750200-B72E-11d9-829B-0050DA1A72D3}\ServerBinary'

# The obsolete Logitech SDK 8.57 class. GW2 never reads it, but other games do,
# so it gets put back to the stock DLL.
$legacyKey = 'HKLM:\SOFTWARE\Classes\CLSID\{d0e790a5-01a7-49ae-ae0b-e986bdd0c21b}\ServerBinary'
$stockLcd = 'C:\Program Files\Logitech Gaming Software\SDK\LCD\x64\LogitechLcd.dll'

function Get-Value([string]$key) {
    if (-not (Test-Path -LiteralPath $key)) { return $null }
    (Get-Item -LiteralPath $key).GetValue('')
}

function Set-Value([string]$key, [string]$value) {
    if (-not (Test-Path -LiteralPath $key)) { New-Item -Path $key -Force | Out-Null }
    Set-Item -LiteralPath $key -Value $value
}

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $pr = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'HKLM write needs an elevated PowerShell. Re-run as Administrator.'
    }
}

switch ($Action) {

    'Status' {
        Write-Output "proxy : $proxy"
        Write-Output "live  : $liveKey"
        Write-Output "  -> $(Get-Value $liveKey)"
        if ((Get-Value $liveKey) -eq $proxy) {
            Write-Output '  STATE: proxy is active'
        } else {
            Write-Output '  STATE: stock LgLcdApi is active'
        }
        Write-Output "legacy: $legacyKey"
        Write-Output "  -> $(Get-Value $legacyKey)"
    }

    'Register' {
        if (-not (Test-Path -LiteralPath $proxy)) { throw "proxy not built: $proxy" }
        Assert-Admin

        $current = Get-Value $liveKey
        if ($current -eq $proxy) {
            Write-Output 'live key already points at the proxy'
        } else {
            @{ live = $current; legacy = (Get-Value $legacyKey); savedAt = (Get-Date).ToString('o') } |
                ConvertTo-Json | Set-Content -LiteralPath $backup -Encoding UTF8
            Write-Output "backed up -> $backup"
            Set-Value $liveKey $proxy
            Write-Output "registered -> $proxy"
        }

        if ((Get-Value $legacyKey) -ne $stockLcd) {
            Set-Value $legacyKey $stockLcd
            Write-Output "legacy key restored to stock -> $stockLcd"
        }

        Write-Output 'Restart GW2 so it re-resolves the CLSID.'
    }

    'Restore' {
        Assert-Admin
        if (-not (Test-Path -LiteralPath $backup)) { throw "no backup at $backup" }
        $saved = Get-Content -LiteralPath $backup -Raw | ConvertFrom-Json

        if ([string]::IsNullOrEmpty($saved.live)) {
            if (Test-Path -LiteralPath $liveKey) { Remove-Item -LiteralPath $liveKey -Force }
            Write-Output 'removed live ServerBinary (there was none originally)'
        } else {
            Set-Value $liveKey $saved.live
            Write-Output "restored -> $($saved.live)"
        }

        if (-not [string]::IsNullOrEmpty($saved.legacy)) {
            Set-Value $legacyKey $saved.legacy
            Write-Output "legacy restored -> $($saved.legacy)"
        }
        Write-Output 'Restart GW2 so it re-resolves the CLSID.'
    }
}
