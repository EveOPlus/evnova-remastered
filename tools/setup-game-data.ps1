param(
    [string]$Destination = (Join-Path (Split-Path -Parent $PSScriptRoot) 'EV Nova'),
    [string]$CacheDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) '.cache/downloads'),
    [string]$StatusFile,
    [switch]$BaseOnly
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
    if ($uri.Scheme -ne 'https' -or $uri.Host -ne 'download.escape-velocity.games') {
        throw 'The download source is not the configured EV community archive.'
    }
    New-Item -ItemType Directory -Path $Cache -Force | Out-Null
    $temporary = Get-NovaChildPath $Cache ($Package.archive + '.' + [Guid]::NewGuid().ToString('N') + '.part')
    try {
        Write-NovaSetupStatus ('Downloading ' + $Package.archive + '...') | Out-Host
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $progressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $uri -OutFile $temporary -UseBasicParsing
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

# Dot-sourcing exposes the file-safety helpers to the installer checks.
if ($MyInvocation.InvocationName -ne '.') {
    try { Install-NovaGameData } catch {
        Write-NovaSetupStatus ('Setup failed: ' + $_.Exception.Message)
        exit 1
    }
}
