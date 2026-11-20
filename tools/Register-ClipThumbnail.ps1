# Opt-in, current-user-only .clip thumbnail registration. No administrator needed.
# Run this ONLY after SageThumbProbe succeeds on your own .clip file.
$ErrorActionPreference = 'Stop'
$clsid = '{768559DF-BA6B-4F4B-BCC0-BABDDA4911B8}'
$thumbCategory = '{E357FCCD-A995-4576-B01F-234630154E96}'
$extensionKey = "Software\Classes\.clip\shellex\$thumbCategory"
$serverKey = "Software\Classes\CLSID\$clsid\InprocServer32"
$path = Join-Path $PSScriptRoot 'SageThumbnailProvider.dll'
if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "SageThumbnailProvider.dll not found beside this script: $path"
}
$dllPath = (Resolve-Path -LiteralPath $path).ProviderPath

# Check the effective Shell binding BEFORE touching any keys. Do not silently
# replace another program's existing thumbnail handler.
$existing = [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey(".clip\shellex\$thumbCategory")
if ($null -ne $existing) {
    try { $value = [string]$existing.GetValue('') } finally { $existing.Close() }
    if ($value -and $value -ine $clsid) {
        throw "A different .clip thumbnail provider is already registered: $value. No changes made."
    }
}
$server = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($serverKey)
if ($null -ne $server) {
    try { $previousDll = [string]$server.GetValue('') } finally { $server.Close() }
    if ($previousDll -and $previousDll -ine $dllPath) {
        throw "This provider CLSID is already registered from another folder: $previousDll. No changes made."
    }
}

$server = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($serverKey)
try {
    $server.SetValue('', $dllPath, [Microsoft.Win32.RegistryValueKind]::String)
    $server.SetValue('ThreadingModel', 'Apartment', [Microsoft.Win32.RegistryValueKind]::String)
} finally { $server.Close() }
$extension = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($extensionKey)
try { $extension.SetValue('', $clsid, [Microsoft.Win32.RegistryValueKind]::String) }
finally { $extension.Close() }

Write-Host "Registered .clip thumbnail provider for this Windows user only: $dllPath"
Write-Host 'No default application or other extension handlers were changed.'
Write-Host 'Refresh Explorer or open a new folder. Existing cached thumbnails may persist.'
