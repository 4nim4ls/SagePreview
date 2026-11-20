# Build an UNSIGNED graphical setup exe from the already-tested staged Release folder.
# This DOES NOT run Setup and DOES NOT change the Windows registry.
[CmdletBinding()]
param([string] $InnoCompiler)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$stage = Join-Path $root 'dist\SagePreview-1.0.0'
$script = Join-Path $root 'installer\SagePreview.iss'
if (-not (Test-Path -LiteralPath $stage -PathType Container)) {
    throw "Staged Release not found at $stage. Run tools\Build-Release.ps1 first."
}
$required = @('SagePreview.exe','SagePreviewListener.exe','SageThumbnailProvider.dll',
  'SageKraThumbnailProvider.dll','SagePsdThumbnailProvider.dll','SageAvifThumbnailProvider.dll',
  'SageAvifCodecCheck.exe','synthetic-avif.avif','README.md','LICENSE','THIRD_PARTY.md','ZLIB-LICENSE.txt')
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $name) -PathType Leaf)) {
        throw "Missing staged file: $name. Re-run the Release build."
    }
}
if (-not $InnoCompiler) {
    $exe = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($exe) { $InnoCompiler = $exe.Source }
}
if (-not $InnoCompiler) {
    $bases = @(${env:ProgramFiles(x86)}, $env:ProgramFiles) | Where-Object { $_ }
    foreach ($base in $bases) {
        foreach ($folder in @('Inno Setup 7', 'Inno Setup 6')) {
            $candidate = Join-Path (Join-Path $base $folder) 'ISCC.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $InnoCompiler = $candidate
                break
            }
        }
        if ($InnoCompiler) { break }
    }
}
if (-not $InnoCompiler -or -not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
    throw 'Inno Setup compiler (ISCC.exe) not found. Install Inno Setup 6.3+ / 7 from jrsoftware.org or pass -InnoCompiler C:\...\ISCC.exe.'
}
Write-Host "Compiling GUI installer using $InnoCompiler"
& $InnoCompiler "/DStageDir=$stage" $script
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compiler failed (exit code $LASTEXITCODE)." }
Write-Host 'Created UNSIGNED GUI installer under dist. Verify final installation and signing/distribution decision before public release.'
