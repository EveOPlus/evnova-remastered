param(
    [string]$Destination = (Join-Path (Split-Path -Parent $PSScriptRoot) 'EV Nova'),
    [string]$CacheDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) '.cache/downloads'),
    [string]$StatusFile,
    [switch]$BaseOnly,
    [switch]$GraphicsOnly
)

$ErrorActionPreference = 'Stop'
# A game launched from PowerShell 7 can inherit a module path that omits
# Windows PowerShell's modules (including Get-FileHash). Use this host's set.
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + [IO.Path]::PathSeparator + $env:PSModulePath
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression

function Write-NovaSetupStatus([string]$Message) {
    Write-Output $Message
    if ($StatusFile) {
        [IO.File]::WriteAllText($StatusFile, $Message, [Text.UTF8Encoding]::new($false))
    }
}

function Get-NovaChildPath([string]$Root, [string]$Relative) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or
        ($Relative -split '[/\\]') -contains '..') {
        throw "Invalid archive path: $Relative"
    }
    $childPath = [IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
    if (-not $childPath.StartsWith($rootPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Archive path leaves the installation folder: $Relative"
    }
    return $childPath
}

function Test-NovaPackageInstalled($Package, [string]$Target) {
    foreach ($file in $Package.files) {
        $path = Get-NovaChildPath $Target $file.target
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
            return $false
        }
    }
    return $true
}

function Get-NovaArchive($Package, [string]$Cache) {
    $cachePath = Get-NovaChildPath $Cache $Package.archive
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        if ((Get-FileHash -LiteralPath $cachePath -Algorithm SHA256).Hash -eq $Package.sha256) {
            return $cachePath
        }
    }
    $uri = [Uri]$Package.url
    $graphicsSource = $uri.Host -eq 'github.com' -and
        $uri.AbsolutePath -match '^/EveOPlus/evnova-remastered/releases/download/[^/]+/evnova-graphics-[a-z0-9.-]+\.zip$'
    if ($uri.Scheme -ne 'https' -or ($uri.Host -ne 'download.escape-velocity.games' -and -not $graphicsSource)) {
        throw 'The download source is not a configured community archive or graphics release.'
    }
    New-Item -ItemType Directory -Path $Cache -Force | Out-Null
    $temporary = Get-NovaChildPath $Cache ($Package.archive + '.' + [Guid]::NewGuid().ToString('N') + '.part')
    try {
        Write-NovaSetupStatus ('Downloading ' + $Package.archive + '...') | Out-Host
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $request = [Net.HttpWebRequest]::Create($uri)
        $request.UserAgent = 'EVNova-Community-Remaster'
        $request.Timeout = 30000
        $request.ReadWriteTimeout = 30000
        $response = $request.GetResponse()
        try {
            $inputStream = $response.GetResponseStream()
            $outputStream = [IO.File]::Create($temporary)
            try {
                $buffer = New-Object byte[] (1MB)
                [long]$received = 0
                $clock = [Diagnostics.Stopwatch]::StartNew()
                while (($count = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $received += $count
                    if ($received -gt $Package.size) { throw 'Download exceeds the expected archive size.' }
                    $outputStream.Write($buffer, 0, $count)
                    if ($clock.ElapsedMilliseconds -ge 500) {
                        Write-NovaSetupStatus ('Downloading: {0:N0} / {1:N0} MB' -f ($received / 1000000), ($Package.size / 1000000)) | Out-Host
                        $clock.Restart()
                    }
                }
            } finally { $outputStream.Dispose(); $inputStream.Dispose() }
        } finally { $response.Dispose() }
        if ((Get-Item -LiteralPath $temporary).Length -ne $Package.size -or
            (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $Package.sha256) {
            throw ('The downloaded archive did not match its verified checksum: ' + $Package.archive)
        }
        Move-Item -LiteralPath $temporary -Destination $cachePath -Force
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }
    return $cachePath
}

function Install-NovaArchive($Package, [string]$Archive, [string]$Target) {
    # Extract only the listed game resources and original documentation. The
    # original game executable is read as a resource container, never launched.
    $targetPath = [IO.Path]::GetFullPath($Target)
    $parentPath = Split-Path -Parent $targetPath
    New-Item -ItemType Directory -Path $parentPath -Force | Out-Null
    $stageName = '.evnova-setup-' + [Guid]::NewGuid().ToString('N')
    $stagePath = Get-NovaChildPath $parentPath $stageName
    New-Item -ItemType Directory -Path $stagePath | Out-Null
    try {
        $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
        try {
            # Validate the entire archive before writing any entry.
            foreach ($entry in $zip.Entries) {
                if ($entry.Name) { $null = Get-NovaChildPath $stagePath $entry.FullName }
            }
            foreach ($file in $Package.files) {
                $entry = $zip.GetEntry($file.source)
                if (-not $entry -or $entry.Length -le 0) { throw "Missing game resource: $($file.source)" }
                $path = Get-NovaChildPath $stagePath $file.target
                New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $path, $false)
            }
        } finally { $zip.Dispose() }
        foreach ($file in $Package.files) {
            $path = Get-NovaChildPath $targetPath $file.target
            # Keep player modifications and existing files. Missing/empty files
            # are repaired; saves, preferences and other plug-ins are untouched.
            if ((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 0) { continue }
            New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
            Copy-Item -LiteralPath (Get-NovaChildPath $stagePath $file.target) -Destination $path -Force
        }
    } finally {
        $verifiedStage = Get-NovaChildPath $parentPath $stageName
        if ($verifiedStage -ne $stagePath) { throw 'Unexpected staging folder; cleanup stopped.' }
        if (Test-Path -LiteralPath $verifiedStage) { Remove-Item -LiteralPath $verifiedStage -Recurse -Force }
    }
}

function Install-NovaGameData {
    $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'game-downloads.json') -Raw | ConvertFrom-Json
    $destinationPath = [IO.Path]::GetFullPath($Destination)
    foreach ($name in @('community', 'arpia2')) {
        if ($name -eq 'arpia2' -and $BaseOnly) { continue }
        $package = $manifest.packages.$name
        $target = if ($name -eq 'community') { $destinationPath } else { Join-Path $destinationPath 'Campaigns/ARPIA2' }
        if (-not (Test-NovaPackageInstalled $package $target)) {
            $archive = Get-NovaArchive $package $CacheDirectory
            Write-NovaSetupStatus ('Installing ' + $(if ($name -eq 'community') { 'Community Edition data' } else { 'ARPIA2 and its extras' }) + '...')
            Install-NovaArchive $package $archive $target
        }
        if (-not (Test-NovaPackageInstalled $package $target)) { throw "Installation is incomplete: $name" }
        $ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
        if ($ffmpeg) {
            $movies = if ($name -eq 'community') {
                @('Nova Files/Race 1.mov', 'Nova Files/Race 2.mov', 'Nova Files/Race 3.mov', 'Nova Files/Race 4.mov')
            } else { @('gasminer.mov', 'credits.mov') }
            foreach ($movie in $movies) {
                $inputPath = Join-Path $target $movie
                $outputPath = $inputPath + '.mpg'
                if (Test-Path -LiteralPath $outputPath) { continue }
                Write-NovaSetupStatus 'Preparing game movies...'
                & $ffmpeg.Source -hide_banner -loglevel error -y -i $inputPath -vf 'pad=ceil(iw/2)*2:ceil(ih/2)*2' -r 25 -c:v mpeg1video -q:v 2 -c:a mp2 -ar 44100 -ac 2 -b:a 192k -f mpeg $outputPath
                if ($LASTEXITCODE -ne 0) {
                    Remove-Item -LiteralPath $outputPath -Force -ErrorAction SilentlyContinue
                    throw "Movie preparation failed: $movie"
                }
            }
        }
        if ($name -eq 'arpia2') {
            'ARPIA2 core resources, all three extras, and original documentation.' | Set-Content -LiteralPath (Join-Path $target 'installed.txt') -Encoding utf8
        }
    }
    Write-NovaSetupStatus 'Setup complete.'
}

function Install-NovaGraphicsArchive($Package, [string]$Archive, [string]$Target) {
    $targetPath = [IO.Path]::GetFullPath($Target)
    # Avoid following a junction/symlink during extraction or receipt creation.
    for ($ancestor = $targetPath; $ancestor; $ancestor = Split-Path -Parent $ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and
            ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Graphics destination contains a linked folder.'
        }
    }
    $receiptPath = Get-NovaChildPath $targetPath '.installed'
    if ((Test-Path -LiteralPath $receiptPath) -and
        ((Get-Item -LiteralPath $receiptPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Graphics receipt is a linked file.'
    }
    if ((Split-Path -Leaf $targetPath) -ne $Package.id -or $Package.id -notmatch '^[a-z0-9-]+$') {
        throw 'Graphics must be installed in their versioned artwork folder.'
    }
    if ((Get-Item -LiteralPath $Archive).Length -ne $Package.size -or
        (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $Package.sha256) {
        throw 'The graphics archive did not match its verified checksum.'
    }
    $parent = Split-Path -Parent $targetPath
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $stageName = '.graphics-setup-' + [Guid]::NewGuid().ToString('N')
    $stage = Get-NovaChildPath $parent $stageName
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
        try {
            $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            [long]$expanded = 0
            foreach ($entry in $zip.Entries) {
                $null = Get-NovaChildPath $stage $entry.FullName
                if ($entry.FullName -cnotmatch '^([a-f0-9]{16}-[0-9]+x[0-9]+\.evs|manifest\.json|provenance\.json|Real-ESRGAN-LICENSE\.txt|CREDITS\.md|NOTICE\.md)$' -or
                    -not $seen.Add($entry.FullName) -or $entry.Length -le 0 -or $entry.Length -gt 32MB) {
                    throw "Unexpected graphics archive entry: $($entry.FullName)"
                }
                $expanded += $entry.Length
                if ($expanded -gt 1GB) { throw 'Graphics archive exceeds its extraction limit.' }
            }
            if ($zip.Entries.Count -ne $Package.file_count -or $expanded -ne $Package.expanded_bytes -or
                -not $seen.Contains('manifest.json') -or -not $seen.Contains('provenance.json') -or
                -not $seen.Contains('Real-ESRGAN-LICENSE.txt') -or -not $seen.Contains('CREDITS.md') -or
                -not $seen.Contains('NOTICE.md')) { throw 'Graphics archive is incomplete.' }
            foreach ($entry in $zip.Entries) {
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Get-NovaChildPath $stage $entry.FullName), $false)
            }
        } finally { $zip.Dispose() }
        # Incomplete installs never have a receipt. Only these verified artwork
        # files are replaced when repairing; pilots, resources and settings are not touched.
        New-Item -ItemType Directory -Path $targetPath -Force | Out-Null
        foreach ($file in Get-ChildItem -LiteralPath $stage -File) {
            $path = Get-NovaChildPath $targetPath $file.Name
            if ((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw 'Graphics destination contains a linked file.'
            }
            Copy-Item -LiteralPath $file.FullName -Destination $path -Force
        }
        [IO.File]::WriteAllText($receiptPath, $Package.id + "`n", [Text.UTF8Encoding]::new($false))
    } finally {
        $checkedStage = Get-NovaChildPath $parent $stageName
        if ($checkedStage -ne $stage) { throw 'Unexpected graphics staging folder; cleanup stopped.' }
        if (Test-Path -LiteralPath $checkedStage) { Remove-Item -LiteralPath $checkedStage -Recurse -Force }
    }
}

function Install-NovaGraphics {
    $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'game-downloads.json') -Raw | ConvertFrom-Json
    $package = $manifest.graphics
    if (-not $package) { throw 'The graphics download manifest is missing.' }
    $receipt = Get-NovaChildPath $Destination '.installed'
    if ((Test-Path -LiteralPath $receipt) -and (Get-Content -LiteralPath $receipt -Raw).Trim() -eq $package.id -and
        (Test-Path -LiteralPath (Get-NovaChildPath $Destination 'manifest.json'))) {
        Write-NovaSetupStatus 'Setup complete.'
        return
    }
    $archive = Get-NovaArchive $package $CacheDirectory
    Write-NovaSetupStatus 'Installing enhanced ships and planets...'
    Install-NovaGraphicsArchive $package $archive $Destination
    Write-NovaSetupStatus 'Setup complete.'
}

# Dot-sourcing exposes the file-safety helpers to the installer checks.
if ($MyInvocation.InvocationName -ne '.') {
    try {
        if ($GraphicsOnly) { Install-NovaGraphics } else { Install-NovaGameData }
    } catch {
        Write-NovaSetupStatus ('Setup failed: ' + $_.Exception.Message)
        exit 1
    }
}
