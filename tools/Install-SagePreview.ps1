# SagePreview 1.0.0, non-elevated per-user install from a staged Release folder.
# Intentional: never silently take ownership of another thumbnail provider.
[CmdletBinding()]
param(
    [switch] $StartOnLogin,
    [switch] $RegisterAvif,
    [switch] $DoNotLaunch
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not [Environment]::Is64BitProcess -or -not [Environment]::Is64BitOperatingSystem) {
    throw 'SagePreview is Windows 11 x64 only. Use 64-bit PowerShell.'
}
$source = $PSScriptRoot
$destination = Join-Path $env:LOCALAPPDATA 'Programs\SagePreview'
$marker = Join-Path $destination '.sagepreview-install.json'
$thumbCategory = '{E357FCCD-A995-4576-B01F-234630154E96}'
$hkcu = [Microsoft.Win32.Registry]::CurrentUser
$classes = [Microsoft.Win32.Registry]::ClassesRoot
$runSubkey = 'Software\Microsoft\Windows\CurrentVersion\Run'
$clsids = @(
    @{ Ext='.clip'; Clsid='{768559DF-BA6B-4F4B-BCC0-BABDDA4911B8}'; Dll='SageThumbnailProvider.dll' },
    @{ Ext='.kra';  Clsid='{FA18D610-3865-450A-90A4-5CC8346E6C6A}'; Dll='SageKraThumbnailProvider.dll' },
    @{ Ext='.psd';  Clsid='{3B3B5629-77BF-4C18-8D45-348AED61E2C4}'; Dll='SagePsdThumbnailProvider.dll' }
)
if ($RegisterAvif) {
    $clsids += @{ Ext='.avif'; Clsid='{A012A89F-0BE7-46EF-A811-73EB24684431}'; Dll='SageAvifThumbnailProvider.dll' }
}
$payload = @(
    'SagePreview.exe', 'SagePreviewListener.exe',
    'SageThumbnailProvider.dll', 'SageKraThumbnailProvider.dll',
    'SagePsdThumbnailProvider.dll', 'SageAvifThumbnailProvider.dll',
    'SageAvifCodecCheck.exe', 'synthetic-avif.avif', 'webp-codec-test.webp',
    'Install-SagePreview.ps1', 'Uninstall-SagePreview.ps1', 'Test-Release.ps1',
    'README.md', 'THIRD_PARTY.md', 'RELEASE_CHECKLIST.md',
    'INSTALLATION.md', 'LICENSE', 'ZLIB-LICENSE.txt'
)
function Read-Default([Microsoft.Win32.RegistryKey] $root, [string] $path) {
    $key = $root.OpenSubKey($path)
    if ($null -eq $key) { return '' }
    try { return [string] $key.GetValue('') }
    finally { $key.Close() }
}
function Set-Value([string] $subkey, [string] $name, [string] $value) {
    $key = $hkcu.CreateSubKey($subkey)
    if ($null -eq $key) { throw "Cannot write HKCU\$subkey" }
    try { $key.SetValue($name, $value, [Microsoft.Win32.RegistryValueKind]::String) }
    finally { $key.Close() }
}
function Remove-OwnAssociation($item, [string] $expectedDll) {
    $extensionKey = "Software\Classes\$($item.Ext)\shellex\$thumbCategory"
    $classKey = "Software\Classes\CLSID\$($item.Clsid)"
    $server = Read-Default $hkcu "$classKey\InprocServer32"
    # Protect newer installations and other programs from stale uninstall runs.
    if ($server -and -not [string]::Equals($server, $expectedDll, [StringComparison]::OrdinalIgnoreCase)) { return }
    $binding = Read-Default $hkcu $extensionKey
    if ([string]::Equals($binding, $item.Clsid, [StringComparison]::OrdinalIgnoreCase)) {
        $hkcu.DeleteSubKey($extensionKey, $false)
    }
    if ([string]::Equals($server, $expectedDll, [StringComparison]::OrdinalIgnoreCase)) {
        $hkcu.DeleteSubKeyTree($classKey, $false)
    }
}

if (Test-Path -LiteralPath $marker) {
    throw "SagePreview is already installed at $destination. Uninstall that release before reinstalling."
}
if (Test-Path -LiteralPath $destination) {
    throw "Destination folder already exists without our installation marker: $destination. No changes made."
}
foreach ($filename in $payload) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $filename) -PathType Leaf)) {
        throw "Release package is incomplete: missing $filename. Build and stage Release with CMake first."
    }
}
# Old manually launched listeners use the same mutex: require explicit shutdown
# instead of mysteriously installing a background process that cannot run.
foreach ($p in @(Get-Process -Name SagePreviewListener -ErrorAction SilentlyContinue)) {
    throw "A SagePreview listener is running (PID $($p.Id)). Stop the older listener with its --stop command, then retry."
}
if ($RegisterAvif) {
    & (Join-Path $source 'SageAvifCodecCheck.exe') (Join-Path $source 'synthetic-avif.avif')
    if ($LASTEXITCODE -ne 0) { throw 'AVIF WIC codec test failed; use install without -RegisterAvif. No changes made.' }
}

# Check effective HKCR bindings BEFORE writing anything. Existing foreign
# handlers are left alone, and old SagePreview development DLLs must be
# explicitly unregistered from their original directories first.
$register = @()
foreach ($item in $clsids) {
    $existingBinding = Read-Default $classes "$($item.Ext)\shellex\$thumbCategory"
    $classKey = "Software\Classes\CLSID\$($item.Clsid)"
    $registeredDll = Read-Default $hkcu "$classKey\InprocServer32"
    $expectedDll = Join-Path $destination $item.Dll
    if ($registeredDll -and -not [string]::Equals($registeredDll, $expectedDll, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$($item.Ext) still uses an older SagePreview development DLL: $registeredDll. Run its original Unregister-*Thumbnail.ps1 and retry."
    }
    if ($existingBinding -and -not [string]::Equals($existingBinding, $item.Clsid, [StringComparison]::OrdinalIgnoreCase)) {
        Write-Warning "$($item.Ext) already has a thumbnail provider. SagePreview will not overwrite it."
        continue
    }
    $register += $item
}

$registeredNow = @()
$createdRun = $false
try {
    New-Item -ItemType Directory -Path $destination -ErrorAction Stop | Out-Null
    foreach ($filename in $payload) {
        Copy-Item -LiteralPath (Join-Path $source $filename) -Destination (Join-Path $destination $filename) -ErrorAction Stop
    }
    foreach ($item in $register) {
        $classKey = "Software\Classes\CLSID\$($item.Clsid)\InprocServer32"
        $extensionKey = "Software\Classes\$($item.Ext)\shellex\$thumbCategory"
        # Track this entry before any registry write so rollback also covers partial writes.
        $registeredNow += $item
        Set-Value $classKey '' (Join-Path $destination $item.Dll)
        Set-Value $classKey 'ThreadingModel' 'Apartment'
        Set-Value $extensionKey '' $item.Clsid
        Write-Host "Registered $($item.Ext) thumbnails."
    }
    if ($StartOnLogin) {
        # Read the named value, not the default value.
        $runKey = $hkcu.OpenSubKey($runSubkey)
        $prior = if ($null -ne $runKey) { try { [string]$runKey.GetValue('SagePreview') } finally { $runKey.Close() } } else { '' }
        if ($prior) { throw 'An existing SagePreview startup entry was found. Refusing to replace it.' }
        Set-Value $runSubkey 'SagePreview' ('"' + (Join-Path $destination 'SagePreviewListener.exe') + '"')
        $createdRun = $true
    }
    [pscustomobject]@{
        Application = 'SagePreview'; Version = '1.0.0'
        InstalledAt = (Get-Date).ToUniversalTime().ToString('o')
        RegisteredExtensions = @($registeredNow | ForEach-Object { $_.Ext })
        StartupEntry = [bool]$StartOnLogin
    } | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
} catch {
    foreach ($item in $registeredNow) { Remove-OwnAssociation $item (Join-Path $destination $item.Dll) }
    if ($createdRun) {
        $runKey = $hkcu.OpenSubKey($runSubkey, $true)
        if ($null -ne $runKey) { try { $runKey.DeleteValue('SagePreview', $false) } finally { $runKey.Close() } }
    }
    if (Test-Path -LiteralPath $destination) {
        Remove-Item -LiteralPath $destination -Recurse -Force -ErrorAction SilentlyContinue
    }
    throw
}
Write-Host "SagePreview installed for this user at: $destination"
Write-Host 'No file defaults changed; the existing providers were left alone.'
if ($StartOnLogin) { Write-Host 'Spacebar listener will start at user sign-in.' }
if (-not $DoNotLaunch) {
    Start-Process -FilePath (Join-Path $destination 'SagePreviewListener.exe')
    Write-Host 'Spacebar listener launched.'
}
Write-Host 'Uninstall using the installed Uninstall-SagePreview.ps1 script.'
