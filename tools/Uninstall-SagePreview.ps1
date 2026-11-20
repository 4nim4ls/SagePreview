# SagePreview 1.0.0 uninstaller. Removes only our own per-user registrations.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
$destination = Join-Path $env:LOCALAPPDATA 'Programs\SagePreview'
$marker = Join-Path $destination '.sagepreview-install.json'
if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
    throw "No SagePreview release installation marker at $destination. Refusing to remove an unknown folder."
}
$state = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
if ($state.Application -ne 'SagePreview') { throw 'Unrecognized install marker; no changes made.' }
$hkcu = [Microsoft.Win32.Registry]::CurrentUser
$thumbCategory = '{E357FCCD-A995-4576-B01F-234630154E96}'
$map = @{
    '.clip' = @{ Clsid='{768559DF-BA6B-4F4B-BCC0-BABDDA4911B8}'; Dll='SageThumbnailProvider.dll' }
    '.kra'  = @{ Clsid='{FA18D610-3865-450A-90A4-5CC8346E6C6A}'; Dll='SageKraThumbnailProvider.dll' }
    '.psd'  = @{ Clsid='{3B3B5629-77BF-4C18-8D45-348AED61E2C4}'; Dll='SagePsdThumbnailProvider.dll' }
    '.avif' = @{ Clsid='{A012A89F-0BE7-46EF-A811-73EB24684431}'; Dll='SageAvifThumbnailProvider.dll' }
}
function Read-Default([string] $path) {
    $key = $hkcu.OpenSubKey($path)
    if ($null -eq $key) { return '' }
    try { return [string]$key.GetValue('') }
    finally { $key.Close() }
}
# Stop only a listener running from this exact installation, not someone else's
# version in a development folder.
$exe = Join-Path $destination 'SagePreviewListener.exe'
foreach ($p in @(Get-Process -Name SagePreviewListener -ErrorAction SilentlyContinue)) {
    $path = try { $p.Path } catch { '' }
    if ($path -and [string]::Equals($path, $exe, [StringComparison]::OrdinalIgnoreCase)) {
        Stop-Process -Id $p.Id -Force -ErrorAction Stop
        Write-Host 'Stopped installed Spacebar listener.'
    }
}
foreach ($ext in @($state.RegisteredExtensions)) {
    if (-not $map.ContainsKey([string]$ext)) { continue }
    $item = $map[[string]$ext]
    $classKey = "Software\Classes\CLSID\$($item.Clsid)"
    $expectedDll = Join-Path $destination $item.Dll
    $activeDll = Read-Default "$classKey\InprocServer32"
    if (-not [string]::Equals($activeDll, $expectedDll, [StringComparison]::OrdinalIgnoreCase)) {
        Write-Warning "Skipping ${ext}: COM class no longer points to this installation."
        continue
    }
    $extensionKey = "Software\Classes\$ext\shellex\$thumbCategory"
    if ([string]::Equals((Read-Default $extensionKey), $item.Clsid, [StringComparison]::OrdinalIgnoreCase)) {
        $hkcu.DeleteSubKey($extensionKey, $false)
    }
    $hkcu.DeleteSubKeyTree($classKey, $false)
    Write-Host "Unregistered $ext thumbnails."
}
$runKeyPath = 'Software\Microsoft\Windows\CurrentVersion\Run'
$runKey = $hkcu.OpenSubKey($runKeyPath, $true)
if ($null -ne $runKey) {
    try {
        $runValue = [string]$runKey.GetValue('SagePreview')
        $expectedRun = '"' + $exe + '"'
        if ([string]::Equals($runValue, $expectedRun, [StringComparison]::OrdinalIgnoreCase)) {
            $runKey.DeleteValue('SagePreview', $false)
            Write-Host 'Removed SagePreview startup entry.'
        }
    } finally { $runKey.Close() }
}
# If Explorer's isolated thumbnail host still has a DLL open, leave the files
# rather than force-killing Explorer or unrelated dllhost processes.
$ownedFiles = @(
    'SagePreview.exe','SagePreviewListener.exe','SageThumbnailProvider.dll',
    'SageKraThumbnailProvider.dll','SagePsdThumbnailProvider.dll',
    'SageAvifThumbnailProvider.dll','SageAvifCodecCheck.exe',
    'synthetic-avif.avif','webp-codec-test.webp',
    'Install-SagePreview.ps1','Test-Release.ps1',
    'README.md','THIRD_PARTY.md','RELEASE_CHECKLIST.md','INSTALLATION.md','LICENSE','ZLIB-LICENSE.txt'
)
$locked = @()
foreach ($filename in $ownedFiles) {
    $file = Join-Path $destination $filename
    if (-not (Test-Path -LiteralPath $file)) { continue }
    try { Remove-Item -LiteralPath $file -Force -ErrorAction Stop }
    catch { $locked += $filename }
}
if ($locked.Count -gt 0) {
    Write-Warning "Some files are still in use: $($locked -join ', ')"
    Write-Warning 'Your registrations are removed. Sign out and back in, then rerun the uninstaller from this directory.'
} else {
    # Only remove the marker last. This leaves a retry path when DLLs were locked.
    $remaining = @(Get-ChildItem -LiteralPath $destination -Force | Where-Object { $_.Name -notin @('.sagepreview-install.json', 'Uninstall-SagePreview.ps1') })
    if ($remaining.Count -eq 0) {
        Remove-Item -LiteralPath (Join-Path $destination 'Uninstall-SagePreview.ps1') -Force
        Remove-Item -LiteralPath $marker -Force
        Remove-Item -LiteralPath $destination -Force
        Write-Host 'SagePreview removed for this user.'
    } else {
        Write-Warning 'Unexpected files remain in the installation directory; leaving them and the marker untouched.'
    }
}
