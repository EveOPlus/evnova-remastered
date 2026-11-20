param([string]$Source = (Join-Path $env:USERPROFILE 'Downloads/ARPIA2'))
$ErrorActionPreference = 'Stop'
$novaRoot = Split-Path -Parent $PSScriptRoot
$novaPack = Join-Path $novaRoot 'EV Nova\Campaigns\ARPIA2'
$novaCore = @('Data 1','Data 2','Graphics 1','Graphics 2','Music 1','Music 2','Ships')
$novaExtras = @('Variants','Secondary RocketL','Secondary Stingray')
foreach ($novaName in $novaCore) {
    if (-not (Test-Path -LiteralPath (Join-Path $Source "Plug-Ins\ARPIA2 - $novaName.rez"))) { throw "Missing ARPIA2 - $novaName.rez" }
}
foreach ($novaName in $novaExtras) {
    if (-not (Test-Path -LiteralPath (Join-Path $Source "Extra ARPIA2 add-ons\ARPIA2 - $novaName.rez"))) { throw "Missing ARPIA2 extra: $novaName" }
}
$novaFfmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source
New-Item -ItemType Directory -Path $novaPack -Force | Out-Null
foreach ($novaName in $novaCore) {
    Copy-Item -LiteralPath (Join-Path $Source "Plug-Ins\ARPIA2 - $novaName.rez") -Destination $novaPack -Force
}
foreach ($novaName in $novaExtras) {
    Copy-Item -LiteralPath (Join-Path $Source "Extra ARPIA2 add-ons\ARPIA2 - $novaName.rez") -Destination (Join-Path $novaPack "ZZ ARPIA2 - $novaName.rez") -Force
}
foreach ($novaMovie in @('gasminer.mov','credits.mov')) {
    $novaOriginal = Join-Path $Source "Plug-Ins\$novaMovie"
    Copy-Item -LiteralPath $novaOriginal -Destination $novaPack -Force
    & $novaFfmpeg -hide_banner -loglevel error -y -i $novaOriginal -vf 'pad=ceil(iw/2)*2:ceil(ih/2)*2' -r 25 -c:v mpeg1video -q:v 2 -c:a mp2 -ar 44100 -ac 2 -b:a 192k -f mpeg (Join-Path $novaPack "$novaMovie.mpg")
    if ($LASTEXITCODE -ne 0) { throw "Movie conversion failed: $novaMovie" }
}
$novaDocs = Join-Path $novaPack 'Documentation'
New-Item -ItemType Directory -Path $novaDocs -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $Source 'ReadMe (do, even cheaters).rtf') -Destination $novaDocs -Force
Copy-Item -LiteralPath (Join-Path $Source 'Extra ARPIA2 add-ons\README - variants & other extras.rtf') -Destination $novaDocs -Force
'ARPIA2: seven core archives, all three optional archives, and two converted movies.' | Set-Content -LiteralPath (Join-Path $novaPack 'installed.txt') -Encoding utf8
Write-Output "Installed full expansion at $novaPack"
