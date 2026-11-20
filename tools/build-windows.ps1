param(
    [ValidateSet('release', 'debug')]
    [string]$Configuration = 'release',
    [int]$Jobs = 8,
    [string]$DependencyDirectory,
    [switch]$TestDriver
)

$ErrorActionPreference = 'Stop'
$novaRoot = Split-Path -Parent $PSScriptRoot
$novaVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $novaVsWhere)) {
    throw 'Install Visual Studio with Desktop development with C++ first.'
}
$novaVsRoot = & $novaVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $novaVsRoot) { throw 'No Visual Studio C++ installation was found.' }

& (Join-Path $novaVsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:VCPKG_ROOT = Join-Path $novaRoot '.cache\vcpkg'
$env:VCPKG_MAX_CONCURRENCY = [string]$Jobs
$env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $novaRoot '.cache\vcpkg-binaries'
New-Item -ItemType Directory -Path $env:VCPKG_DEFAULT_BINARY_CACHE -Force | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $env:VCPKG_ROOT 'vcpkg.exe'))) {
    throw 'Bootstrap .cache\vcpkg at revision 319504a5326aa870edde46438c5455fa76305a56 first; see docs/local_windows_build.md.'
}

if (-not $DependencyDirectory) { $DependencyDirectory = Join-Path $env:LOCALAPPDATA 'EVNovaBuild\dependencies' }
New-Item -ItemType Directory -Path $DependencyDirectory -Force | Out-Null
# HarfBuzz's pkg-config linker flags do not preserve spaces in dependency paths.
$novaDependencyPath = (Resolve-Path -LiteralPath $DependencyDirectory).Path
if ($novaDependencyPath -match '\s') {
    throw 'Use -DependencyDirectory with a path without spaces.'
}

Push-Location $novaRoot
try {
    $novaBuildName = if ($TestDriver) { "driver-$Configuration" } else { $Configuration }
    $novaBuildDirectory = Join-Path $novaRoot "build\$novaBuildName"
    $novaProbeOption = if ($TestDriver) { 'ON' } else { 'OFF' }
    cmake --preset $Configuration -B $novaBuildDirectory -DVCPKG_TARGET_TRIPLET=x64-windows-static "-DEVNOVA_ENABLE_PROBE=$novaProbeOption" "-DVCPKG_INSTALLED_DIR=$novaDependencyPath"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    cmake --build $novaBuildDirectory --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'The native build failed.' }
} finally {
    Pop-Location
}
