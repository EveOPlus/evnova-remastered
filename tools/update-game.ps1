param(
    [string]$InstallDirectory = (Split-Path -Parent $PSScriptRoot),
    [ValidateSet('Check', 'Apply')][string]$Mode = 'Check',
    [ValidatePattern('^evnova(?:-[a-f0-9]{8})?\.exe$')][string]$ExecutableName = 'evnova.exe',
    [int]$ParentProcessId = 0,
    [switch]$NoLaunch,
    [switch]$LibraryOnly
)

$ErrorActionPreference = 'Stop'
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + [IO.Path]::PathSeparator + $env:PSModulePath
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.IO.Compression.FileSystem
$script:NovaRepository = 'EveOPlus/evnova-remastered'

function Get-NovaVersion([string]$Text) {
    if ($Text -notmatch '^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-preview\.(0|[1-9][0-9]*))?$') {
        throw "Unsupported release version: $Text"
    }
    return @([long]$Matches[1], [long]$Matches[2], [long]$Matches[3],
        $(if ($Matches[4]) { [long]$Matches[4] } else { [long]::MaxValue }))
}

function Compare-NovaVersion([string]$Left, [string]$Right) {
    $a = Get-NovaVersion $Left
    $b = Get-NovaVersion $Right
    for ($i = 0; $i -lt 4; $i++) {
        if ($a[$i] -gt $b[$i]) { return 1 }
        if ($a[$i] -lt $b[$i]) { return -1 }
    }
    return 0
}

function Get-NovaUpdatePath([string]$Root, [string]$Relative) {
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or
        ($Relative -split '[/\\]') -contains '..' -or $Relative -match '[<>"|?*]') {
        throw "Unsafe update path: $Relative"
    }
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $path = [IO.Path]::GetFullPath((Join-Path $base $Relative))
    if (-not $path.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw 'Update path escaped its folder' }
    # Existing junctions/symlinks must not redirect writes outside the package.
    for ($p = $path; $p.Length -ge $base.TrimEnd('\').Length; $p = Split-Path -Parent $p) {
        if (Test-Path -LiteralPath $p) {
            if ((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Update path contains a link: $p"
            }
        }
        if (-not $p -or $p -eq (Split-Path -Parent $p)) { break }
    }
    return $path
}

function Test-NovaEngineFile([string]$Name) {
    return $Name -cmatch '^(evnova\.exe|release\.json|START HERE\.txt|LICENSE\.txt|CREDITS\.md|NOTICE\.md|README\.md|docs/[a-z0-9_]+\.md|licenses/[A-Za-z0-9_.-]+\.(txt|md)|src/third_party/(README\.md|dr_mp3\.h|pl_mpeg\.h)|tools/(setup-game-data\.ps1|game-downloads\.json|setup-narration\.ps1|narration-downloads\.json|nova_narrator\.py|update-game\.ps1))$'
}

function Write-NovaJson([string]$Path, $Value) {
    $temporary = $Path + '.tmp'
    [IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temporary, $Path, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $Path) }
}

function Get-NovaRelease($Releases, [string]$CurrentVersion) {
    $best = $null
    foreach ($release in $Releases) {
        if ($release.draft -or ($release.prerelease -and $CurrentVersion -notmatch '-preview\.')) { continue }
        try { $newer = (Compare-NovaVersion $release.tag_name $CurrentVersion) -gt 0 } catch { continue }
        if (-not $newer) { continue }
        $asset = @($release.assets | Where-Object { $_.name -ceq 'evnova-windows-x64.zip' -and $_.state -eq 'uploaded' })
        if ($asset.Count -ne 1 -or $asset[0].digest -notmatch '^sha256:[a-fA-F0-9]{64}$') { continue }
        $expectedUrl = 'https://github.com/' + $script:NovaRepository + '/releases/download/' + $release.tag_name + '/evnova-windows-x64.zip'
        if ($asset[0].browser_download_url -cne $expectedUrl -or $asset[0].size -gt 268435456 -or $asset[0].size -le 0) { continue }
        if ($null -eq $best -or (Compare-NovaVersion $release.tag_name $best.version) -gt 0) {
            $best = [pscustomobject]@{version=$release.tag_name.TrimStart('v'); url=$expectedUrl; sha256=$asset[0].digest.Substring(7); size=$asset[0].size}
        }
    }
    return $best
}

function Expand-NovaUpdate([string]$Archive, [string]$Stage, $Expected) {
    if ((Get-Item -LiteralPath $Archive).Length -ne $Expected.size -or
        (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ine $Expected.sha256) {
        throw 'Update download failed its SHA-256 or size check'
    }
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $files = @{}
        $total = 0L
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName -notlike 'EV Nova Remastered/*') { throw 'Unexpected update archive layout' }
            $name = $entry.FullName.Substring('EV Nova Remastered/'.Length)
            if (-not (Test-NovaEngineFile $name) -or $files.ContainsKey($name)) { throw "Unexpected or duplicate update file: $name" }
            $total += $entry.Length
            if ($total -gt 536870912 -or $files.Count -ge 200) { throw 'Update archive exceeds limits' }
            $files[$name] = $entry
        }
        foreach ($required in @('evnova.exe','release.json','tools/update-game.ps1','CREDITS.md','LICENSE.txt')) {
            if (-not $files.ContainsKey($required)) { throw "Missing update file: $required" }
        }
        foreach ($name in $files.Keys) {
            $destination = Get-NovaUpdatePath $Stage $name
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($files[$name], $destination)
        }
        $manifest = Get-Content -LiteralPath (Join-Path $Stage 'release.json') -Raw | ConvertFrom-Json
        if ($manifest.schema -ne 1 -or $manifest.repository -cne $script:NovaRepository -or
            (Compare-NovaVersion $manifest.version $Expected.version) -ne 0) { throw 'Update manifest does not match the release' }
        $listed = @{}
        foreach ($item in $manifest.files) {
            if ($listed.ContainsKey($item.path) -or $item.path -eq 'release.json' -or -not $files.ContainsKey($item.path) -or
                $item.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid update file manifest' }
            $listed[$item.path] = $true
            if ((Get-FileHash -LiteralPath (Get-NovaUpdatePath $Stage $item.path) -Algorithm SHA256).Hash -ine $item.sha256) {
                throw "Update file hash mismatch: $($item.path)"
            }
        }
        if ($listed.Count -ne $files.Count - 1) { throw 'Update archive contains unlisted files' }
        return $manifest
    } finally { $zip.Dispose() }
}

function Copy-NovaUpdateFile([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    $temporary = $Destination + '.update-tmp'
    [IO.File]::Copy($Source, $temporary, $true)
    if (Test-Path -LiteralPath $Destination) { [IO.File]::Replace($temporary, $Destination, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $Destination) }
}

function Restore-NovaUpdate([string]$Root, [string]$Work) {
    $journal = Get-NovaUpdatePath $Work 'transaction.json'
    if (-not (Test-Path -LiteralPath $journal)) { return }
    $entries = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
    $entries = @($entries) # PowerShell 5 emits a JSON array as one pipeline object.
    [array]::Reverse($entries)
    foreach ($entry in $entries) {
        $target = Get-NovaUpdatePath $Root $entry.path
        if ($entry.existed) { Copy-NovaUpdateFile (Get-NovaUpdatePath $Work ('rollback/' + $entry.path)) $target }
        elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
    }
    Remove-Item -LiteralPath $journal -Force
}

function Install-NovaUpdate([string]$Root, [string]$Work, [string]$Stage, $Manifest, [string]$ExeName) {
    Restore-NovaUpdate $Root $Work
    # Refuse before changing any files if another game instance holds the engine.
    foreach ($name in @('evnova.exe', $ExeName) | Select-Object -Unique) {
        $path = Get-NovaUpdatePath $Root $name
        if (Test-Path -LiteralPath $path) {
            $handle = [IO.File]::Open($path, 'Open', 'ReadWrite', 'None')
            $handle.Dispose()
        }
    }
    $entries = @()
    $names = @($Manifest.files.path | Where-Object { $_ -ne 'evnova.exe' }) + @('evnova.exe')
    if ($ExeName -ne 'evnova.exe') { $names += $ExeName }
    $names += 'release.json' # Commit version only after every engine file succeeds.
    try {
        foreach ($name in $names) {
            if ($name -ne $ExeName -and -not (Test-NovaEngineFile $name)) { throw 'Unlisted installation path' }
            $target = Get-NovaUpdatePath $Root $name
            $existed = Test-Path -LiteralPath $target
            if ($existed) { Copy-NovaUpdateFile $target (Get-NovaUpdatePath $Work ('rollback/' + $name)) }
            $entries += [pscustomobject]@{path=$name; existed=$existed}
            Write-NovaJson (Join-Path $Work 'transaction.json') $entries
            $sourceName = if ($name -eq $ExeName) { 'evnova.exe' } else { $name }
            Copy-NovaUpdateFile (Get-NovaUpdatePath $Stage $sourceName) $target
        }
        Remove-Item -LiteralPath (Join-Path $Work 'transaction.json') -Force
    } catch {
        $failure = $_
        try { Restore-NovaUpdate $Root $Work }
        catch { Write-Warning ('Rollback failed: ' + $_.Exception.Message) }
        throw $failure
    }
}

function Invoke-NovaUpdate {
    $root = [IO.Path]::GetFullPath($InstallDirectory)
    $work = Get-NovaUpdatePath $root '.evnova-update'
    $lock = $null
    $stage = $null
    $canLaunch = $true
    try {
        New-Item -ItemType Directory -Path $work -Force | Out-Null
        if ($Mode -eq 'Apply' -and $ParentProcessId -gt 0) {
            $parent = Get-Process -Id $ParentProcessId -ErrorAction SilentlyContinue
            if ($parent -and -not $parent.WaitForExit(15000)) { throw 'The previous game has not exited yet' }
        }
        for ($attempt = 0; $attempt -lt 100; $attempt++) {
            try { $lock = [IO.File]::Open((Join-Path $work 'lock'), 'OpenOrCreate', 'ReadWrite', 'None'); break }
            catch { if ($Mode -eq 'Check') { return }; Start-Sleep -Milliseconds 100 }
        }
        if (-not $lock) { throw 'Another update is still running' }
        $installed = Get-Content -LiteralPath (Get-NovaUpdatePath $root 'release.json') -Raw | ConvertFrom-Json
        if ($installed.schema -ne 1 -or $installed.repository -cne $script:NovaRepository) { throw 'Unrecognized installation' }
        $pending = Join-Path $work 'pending.json'
        $archive = Join-Path $work 'package.zip'
        if ($Mode -eq 'Check') {
            if (Test-Path -LiteralPath $pending) { return }
            $headers = @{'User-Agent'='EVNova-Community-Remaster'; Accept='application/vnd.github+json'; 'X-GitHub-Api-Version'='2022-11-28'}
            $releases = Invoke-RestMethod -Uri ('https://api.github.com/repos/' + $script:NovaRepository + '/releases?per_page=100') -Headers $headers -TimeoutSec 20
            $release = Get-NovaRelease $releases $installed.version
            if (-not $release) { [IO.File]::WriteAllText((Join-Path $work 'status.txt'), 'Up to date.'); return }
            [IO.File]::WriteAllText((Join-Path $work 'status.txt'), ('Downloading ' + $release.version + '...'))
            Invoke-WebRequest -UseBasicParsing -Uri $release.url -OutFile ($archive + '.part') -TimeoutSec 120
            $stage = Get-NovaUpdatePath $work ('stage-' + [guid]::NewGuid().ToString('N'))
            $null = Expand-NovaUpdate ($archive + '.part') $stage $release
            Move-Item -LiteralPath ($archive + '.part') -Destination $archive -Force
            Write-NovaJson $pending $release
            [IO.File]::WriteAllText((Join-Path $work 'status.txt'), ('Version ' + $release.version + ' is ready. Restart to install.'))
        } else {
            Restore-NovaUpdate $root $work
            $installed = Get-Content -LiteralPath (Get-NovaUpdatePath $root 'release.json') -Raw | ConvertFrom-Json
            if (-not (Test-Path -LiteralPath $pending)) { return }
            $release = Get-Content -LiteralPath $pending -Raw | ConvertFrom-Json
            if ((Compare-NovaVersion $release.version $installed.version) -le 0) {
                Remove-Item -LiteralPath $pending -Force
                return
            }
            $stage = Get-NovaUpdatePath $work ('stage-' + [guid]::NewGuid().ToString('N'))
            $manifest = Expand-NovaUpdate $archive $stage $release
            Install-NovaUpdate $root $work $stage $manifest $ExecutableName
            Remove-Item -LiteralPath $pending -Force
            [IO.File]::WriteAllText((Join-Path $work 'status.txt'), ('Installed ' + $release.version + '.'))
        }
    } catch {
        if (Test-Path -LiteralPath $work) {
            [IO.File]::WriteAllText((Join-Path $work 'status.txt'), ('Update failed; keeping the installed game. ' + $_.Exception.Message))
        }
        if ($Mode -eq 'Apply' -and (Test-Path -LiteralPath (Join-Path $work 'transaction.json'))) {
            $canLaunch = $false
        } elseif ($Mode -eq 'Apply' -and (Test-Path -LiteralPath (Join-Path $work 'pending.json'))) {
            # A failed download/install is retried from GitHub on a later launch.
            Remove-Item -LiteralPath (Join-Path $work 'pending.json') -Force
        }
        Write-Warning $_.Exception.Message
    } finally {
        if ($stage -and (Test-Path -LiteralPath $stage)) {
            $checked = Get-NovaUpdatePath $work (Split-Path -Leaf $stage)
            if ($checked -ne $stage -or (Split-Path -Leaf $stage) -notlike 'stage-*') { throw 'Unsafe cleanup path' }
            Remove-Item -LiteralPath $stage -Recurse -Force
        }
        if ($lock) { $lock.Dispose() }
        if ($Mode -eq 'Apply' -and -not $NoLaunch -and $canLaunch) {
            Start-Process -FilePath (Get-NovaUpdatePath $root $ExecutableName) -ArgumentList '--skip-update' -WorkingDirectory $root -WindowStyle Hidden | Out-Null
        }
    }
}

if (-not $LibraryOnly) { Invoke-NovaUpdate }
