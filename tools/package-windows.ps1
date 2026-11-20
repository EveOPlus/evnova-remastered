param(
    [string]$GameData,
    [switch]$ReuseGameData,
    [ValidatePattern('^evnova(?:-[a-f0-9]{8})?\.exe$')]
    [string]$ExecutableName = 'evnova.exe'
)

$ErrorActionPreference = 'Stop'
$novaRoot = Split-Path -Parent $PSScriptRoot
if (-not $GameData) { $GameData = Join-Path $novaRoot 'EV Nova' }
$novaExe = Join-Path $novaRoot 'build\release\src\evnova.exe'
foreach ($novaRequired in @($novaExe, (Join-Path $GameData 'Nova.rez'), (Join-Path $GameData 'Nova Files'))) {
    if (-not (Test-Path -LiteralPath $novaRequired)) { throw "Required build or data is missing: $novaRequired" }
}
$novaPackage = Join-Path $novaRoot 'dist\EV Nova Remastered'
$novaPackageData = Join-Path $novaPackage 'EV Nova'
New-Item -ItemType Directory -Path $novaPackageData -Force | Out-Null
$novaInstalledExe = Join-Path $novaPackage $ExecutableName
Copy-Item -LiteralPath $novaExe -Destination $novaInstalledExe -Force
if ($ReuseGameData -and (-not (Test-Path -LiteralPath (Join-Path $novaPackageData 'Nova.rez')) -or -not (Test-Path -LiteralPath (Join-Path $novaPackageData 'Nova Files')))) {
    throw 'Reusing game data requires an existing installed data pack.'
}
if (-not $ReuseGameData) {
foreach ($novaAsset in @('Nova Files', 'Nova Plug-ins', 'Campaigns', 'Remastered Sprites', 'Nova.rez', 'EV Nova.exe', 'Geneva.ttf', 'Charcoal.ttf')) {
    $novaSource = Join-Path $GameData $novaAsset
    $novaTarget = Join-Path $novaPackageData $novaAsset
    if (Test-Path -LiteralPath $novaSource -PathType Container) {
        New-Item -ItemType Directory -Path $novaTarget -Force | Out-Null
        Get-ChildItem -LiteralPath $novaSource -Force | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $novaTarget -Recurse -Force
        }
    } elseif (Test-Path -LiteralPath $novaSource) {
        Copy-Item -LiteralPath $novaSource -Destination $novaTarget -Force
    }
}
}
Copy-Item -LiteralPath (Join-Path $novaRoot 'LICENSE.txt') -Destination $novaPackage -Force
foreach ($novaNotice in @('CREDITS.md', 'NOTICE.md')) {
    Copy-Item -LiteralPath (Join-Path $novaRoot $novaNotice) -Destination $novaPackage -Force
}
$novaPackageTools = Join-Path $novaPackage 'tools'
New-Item -ItemType Directory -Path $novaPackageTools -Force | Out-Null
foreach ($novaSetupFile in @('setup-game-data.ps1', 'game-downloads.json')) {
    Copy-Item -LiteralPath (Join-Path $novaRoot ('tools/' + $novaSetupFile)) -Destination $novaPackageTools -Force
}
$novaLicenses = Join-Path $novaPackage 'licenses'
New-Item -ItemType Directory -Path $novaLicenses -Force | Out-Null
$novaDependencyEntry = Get-Content -LiteralPath (Join-Path $novaRoot 'build\release\CMakeCache.txt') |
    Where-Object { $_ -match '^VCPKG_INSTALLED_DIR:[^=]+=' } | Select-Object -First 1
if (-not $novaDependencyEntry) { throw 'The release build does not identify its dependency directory.' }
$novaShare = Join-Path ($novaDependencyEntry -replace '^[^=]+=', '') 'x64-windows-static\share'
Get-ChildItem -LiteralPath $novaShare -Directory | ForEach-Object {
    $novaCopyright = Join-Path $_.FullName 'copyright'
    if (Test-Path -LiteralPath $novaCopyright) {
        Copy-Item -LiteralPath $novaCopyright -Destination (Join-Path $novaLicenses ($_.Name + '.txt')) -Force
    }
}
Copy-Item -LiteralPath (Join-Path $novaRoot 'src\third_party\README.md') -Destination (Join-Path $novaLicenses 'vendored-audio.txt') -Force
Copy-Item -LiteralPath (Join-Path $novaRoot 'src\third_party\pl_mpeg.h') -Destination (Join-Path $novaLicenses 'pl_mpeg-MIT.h') -Force
@'
EV NOVA COMMUNITY REMASTER

Open evnova.exe to play. Choose New Pilot to create your pilot.

Hold left mouse: steer toward the pointer and thrust when aligned
Release left mouse: automatically brake to a complete stop
Hold right mouse: contextual actions for ships, planets, or empty space
Release right mouse: select the highlighted action; center/outside cancels
Mouse wheel: zoom the world without shrinking the HUD
Wheel over minimap: zoom radar; wheel in galaxy map: zoom the chart
System Markets in the radial menu: compare prices; select a port > Auto Dock
Galaxy map > Pop out: drag its slim title bar to move, wheel to zoom
Drag blank chart space to pan; drag edges to resize; X closes the panel
Shift-click a destination in either map to route; double-press J follows it
Asteroid radial > Auto-mine: approach, fire mining lasers, scoop the yield
W: select secondary weapon; hold Left Ctrl: fire selected secondary
F11: toggle fullscreen (the game starts maximized)
Escape: close the radial menu

Auto Dock / Nearest Port approaches and lands through normal flight controls.
Manual steering cancels auto-docking. Mouse or keyboard flight cancels braking.
Landing completes auto-docking, so Leave launches without docking again.

Mission and ship hail dialogs are read aloud automatically.
Set Prefs > Narration offers four saved voice choices and a Preview button.
F6 / Read again: replay the current text
F8 / Stop: stop the current reading
F7 / Voice: toggle narration (saved for the next session)
Closing a dialog stops its narration.
Use Install voices in that panel to download all four voices and their runtime.
Voices are shared between campaigns and kept when the game updates.

Arrow keys: fly and turn
Space: primary weapons
L: land
J: hyperspace; automatically fly clear first if too close to jump
Departure stays out of the map; jump transitions run twice as fast.
M: galaxy map
Shift-click a map destination: plot the shortest known route automatically
Press J once for one jump. Double-press J quickly to follow the whole route.
While following a route, press J again or steer to cancel.
Low fuel stops automatic travel; refuel and double-press J to resume the route.
Jump transitions use a quick dark fade, with no white flash.
Unreachable destinations leave your existing route intact.
Manual steering or changing navigation cancels automatic jump departure.
Save Pilot / S in the main menu: choose a folder and filename, then save.
Save and Open Pilot start in the active campaign's normal Pilots folder.
Save Pilot uses the original About Nova button; About Nova is now in Set Prefs.
Remastered radar has clearer ship, planet, station and target icons.

Preferences > Extra Prefs includes interface, mission text and flight scaling.
Remastered graphics is on by default: original cockpit themes, sharper 4x
reconstructed ship and planet artwork, layered stars, subtle nebulae and soft
engine/weapon/planet glow. Toggle it off
for the classic scene. The setting applies immediately and is saved.
Disabled ships can still use the radial menu; auto-docking requires working engines.
It also has one ARPIA2 expansion switch for the full campaign and all extras.
Restart the game after switching. ARPIA2 uses separate pilots; choose New Pilot
when starting that campaign for the first time. Its story opens in a bar after
your combat rating reaches Average (100), before joining another storyline.
Preferences > Key Settings lets you change the controls.

Saves and preferences are under:
%APPDATA%\Ambrosia Software\EV Nova

This local build uses the native C++/SDL3 engine from:
https://github.com/wraitii/evnova-decomp
Base revision: e3dd30407939296bf19e96f94a499cc6bb2cafd1

Original game data is stored in the EV Nova subfolder. Keep that folder beside
evnova.exe. The old executable there is used as a resource source.
Engine and dependency notices are included with this package.
'@ | Set-Content -LiteralPath (Join-Path $novaPackage 'READ ME.txt') -Encoding utf8

# Include the same updater, manifest and helpers as the public engine package.
python (Join-Path $novaRoot 'tools/package_release.py')
if ($LASTEXITCODE -ne 0) { throw 'Could not prepare versioned engine metadata.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$novaPublicZip = [IO.Compression.ZipFile]::OpenRead((Join-Path $novaRoot 'dist/evnova-windows-x64.zip'))
try {
    $novaPackageRoot = [IO.Path]::GetFullPath($novaPackage).TrimEnd('\') + '\'
    foreach ($novaEntry in $novaPublicZip.Entries) {
        $novaRelative = $novaEntry.FullName.Substring('EV Nova Remastered/'.Length)
        if ($novaRelative -eq 'evnova.exe') { continue }
        $novaDestination = [IO.Path]::GetFullPath((Join-Path $novaPackageRoot $novaRelative))
        if (-not $novaDestination.StartsWith($novaPackageRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe package path' }
        New-Item -ItemType Directory -Path (Split-Path -Parent $novaDestination) -Force | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($novaEntry,$novaDestination,$true)
    }
} finally { $novaPublicZip.Dispose() }

$novaShell = New-Object -ComObject WScript.Shell
$novaShortcut = $novaShell.CreateShortcut((Join-Path $novaRoot 'Play EV Nova Remake.lnk'))
$novaShortcut.TargetPath = $novaInstalledExe
$novaShortcut.WorkingDirectory = $novaPackage
$novaShortcut.IconLocation = (Join-Path $novaPackageData 'EV Nova.exe') + ',0'
$novaShortcut.Description = 'Play EV Nova Community Remaster'
$novaShortcut.Save()
Write-Output $novaPackage
