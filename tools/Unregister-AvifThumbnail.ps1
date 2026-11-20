# Reverse only the registry keys installed by our own provider; do not alter
# the default app, other thumbnail providers, or other shell-extension keys.
$ErrorActionPreference = 'Stop'
$clsid = '{A012A89F-0BE7-46EF-A811-73EB24684431}'
$thumbCategory = '{E357FCCD-A995-4576-B01F-234630154E96}'
$extensionKey = "Software\Classes\.avif\shellex\$thumbCategory"
$classKey = "Software\Classes\CLSID\$clsid"
$root = [Microsoft.Win32.Registry]::CurrentUser

# Confirm this script is beside the DLL originally registered for this COM
# class. If the user has multiple build directories, refuse to remove the
# active registration from a different build accidentally.
$inproc = $root.OpenSubKey("$classKey\InprocServer32")
$registeredDll = ''
if ($null -ne $inproc) {
    try { $registeredDll = [string]$inproc.GetValue('') }
    finally { $inproc.Close() }
}
$expectedDll = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'SageAvifThumbnailProvider.dll'))
if ($registeredDll -and $registeredDll -ine $expectedDll) {
    throw "Provider currently registered from a different folder: $registeredDll. Run its local Unregister-AvifThumbnail.ps1 instead. No changes made."
}

$extension = $root.OpenSubKey($extensionKey)
if ($null -ne $extension) {
    try { $boundClsid = [string]$extension.GetValue('') }
    finally { $extension.Close() }
    if ($boundClsid -ieq $clsid) {
        $root.DeleteSubKey($extensionKey, $false)
        Write-Host 'Removed our .avif thumbnail association.'
    } else {
        Write-Host 'The .avif thumbnail association belongs to another handler; left unchanged.'
    }
}
if ($registeredDll -ieq $expectedDll) {
    $root.DeleteSubKeyTree($classKey, $false)
    Write-Host 'Removed our registered COM class.'
}
Write-Host 'Done. Refresh Explorer. A cached thumbnail may remain until its cache entry expires.'
