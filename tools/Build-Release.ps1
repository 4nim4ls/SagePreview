# Build, test and stage a Windows 11 x64 Release package (no registry writes).
# Works in ordinary PowerShell even when Visual Studio's CMake isn't on PATH.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'out\release-v1'
$stage = Join-Path $root 'dist\SagePreview-1.0.0'
$archive = Join-Path $root 'dist\SagePreview-1.0.0-win11-x64-portable.zip'
if (-not [Environment]::Is64BitProcess) { throw 'Run 64-bit PowerShell.' }

function Invoke-Checked([string] $exe, [string[]] $arguments) {
    Write-Host "$exe $($arguments -join ' ')"
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed (exit code $LASTEXITCODE)" }
}

# Visual Studio installs CMake in its own folder; it isn't always added to PATH.
# vswhere is Microsoft's supported VS installation discovery utility.
$vswhere = $null
$vswherePaths = @()
if (${env:ProgramFiles(x86)}) { $vswherePaths += (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe') }
if ($env:ProgramFiles) { $vswherePaths += (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe') }
foreach ($candidate in $vswherePaths) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $vswhere = $candidate; break }
}
$vsPath = $null
$vsVersion = $null
if ($vswhere) {
    # Prefer a VS instance with the native x64 C++ compiler installed.
    $vsPath = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    if ($vsPath) {
        $vsVersion = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion | Select-Object -First 1)
    }
}

$cmakeExe = $null
if ($vsPath) {
    $vsBundledCmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $vsBundledCmake -PathType Leaf) { $cmakeExe = $vsBundledCmake }
}
if (-not $cmakeExe) {
    $cmakeOnPath = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmakeOnPath) { $cmakeExe = $cmakeOnPath.Source }
}
if (-not $cmakeExe) {
    throw 'CMake not found. Open Visual Studio Installer > Modify > Desktop development with C++ > Individual components, and ensure C++ CMake tools for Windows is installed. Alternatively run from a Visual Studio Developer PowerShell with CMake on PATH.'
}
$ctestExe = Join-Path (Split-Path -Parent $cmakeExe) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestExe -PathType Leaf)) {
    $ctestOnPath = Get-Command ctest.exe -ErrorAction SilentlyContinue
    if ($ctestOnPath) { $ctestExe = $ctestOnPath.Source }
    else { throw "ctest.exe not found next to $cmakeExe or on PATH. Install the Visual Studio CMake tools." }
}

# Select the actual installed Visual Studio generator instead of assuming 2026.
if (-not $vsVersion) {
    throw 'Could not detect a Visual Studio C++ installation with vswhere.exe. Check Desktop development with C++ in Visual Studio Installer.'
}
$vsMajor = [int]($vsVersion.Split('.')[0])
switch ($vsMajor) {
    18 { $generator = 'Visual Studio 18 2026' }
    17 { $generator = 'Visual Studio 17 2022' }
    default { throw "Unsupported Visual Studio version $vsVersion. Use Visual Studio 2022 or 2026 with Desktop development with C++." }
}
$cmakeHelp = (& $cmakeExe --help) -join "`n"
if ($LASTEXITCODE -ne 0) { throw "Unable to run CMake: $cmakeExe" }
if ($cmakeHelp -notmatch [regex]::Escape($generator)) {
    throw "CMake at $cmakeExe does not support '$generator'. Update the Visual Studio CMake tools in Visual Studio Installer."
}
Write-Host "Using CMake: $cmakeExe"
Write-Host "Using generator: $generator"
Invoke-Checked $cmakeExe @('-S', $root, '-B', $build, '-G', $generator, '-A', 'x64', '-DBUILD_TESTING=ON')
Invoke-Checked $cmakeExe @('--build', $build, '--config', 'Release', '--parallel')
Invoke-Checked $ctestExe @('--test-dir', $build, '-C', 'Release', '--output-on-failure')
# The staging folder must be generated from scratch to avoid shipping obsolete binaries.
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
Invoke-Checked $cmakeExe @('--install', $build, '--config', 'Release', '--prefix', $stage)
& (Join-Path $stage 'Test-Release.ps1')
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
Write-Host "Staged Release: $stage"
Write-Host "Portable distribution ZIP: $archive"
Write-Host 'Important: this is UNSIGNED. Test the final installer and confirm signing/distribution decisions before public release.'
