# Lightweight integrity checks for a staged Windows x64 Release folder.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$files = @(
    'SagePreview.exe','SagePreviewListener.exe',
    'SageThumbnailProvider.dll','SageKraThumbnailProvider.dll',
    'SagePsdThumbnailProvider.dll','SageAvifThumbnailProvider.dll',
    'SageAvifCodecCheck.exe','synthetic-avif.avif',
    'webp-codec-test.webp','Install-SagePreview.ps1',
    'Uninstall-SagePreview.ps1','README.md','LICENSE','THIRD_PARTY.md','ZLIB-LICENSE.txt'
)
$missing = @($files | Where-Object { -not (Test-Path -LiteralPath (Join-Path $root $_) -PathType Leaf) })
if ($missing.Count -gt 0) { throw "Incomplete staged release: $($missing -join ', ')" }
Write-Host 'Release files: present.'
# An absent AVIF WIC codec should be visible but is not a packaging failure.
& (Join-Path $root 'SageAvifCodecCheck.exe') (Join-Path $root 'synthetic-avif.avif')
if ($LASTEXITCODE -eq 0) {
    Write-Host 'AVIF WIC decoding: available.'
} elseif ($LASTEXITCODE -eq 77) {
    Write-Warning 'AVIF WIC decoding not available on this PC; document required optional Windows codecs.'
} else {
    throw "AVIF codec check failed unexpectedly (exit $LASTEXITCODE)."
}
Write-Host 'Release stage check complete. Real Explorer thumbnail and Spacebar tests still required.'
