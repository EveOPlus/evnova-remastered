$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'setup-game-data.ps1')

function Assert-Nova([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-NovaRejected([scriptblock]$Action) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert-Nova $rejected 'Expected unsafe or invalid input to be rejected'
}
function New-TestArchive([string]$Path, [hashtable]$Entries) {
    $zip = [IO.Compression.ZipFile]::Open($Path, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($name in $Entries.Keys) {
            $writer = [IO.StreamWriter]::new($zip.CreateEntry($name).Open())
            try { $writer.Write($Entries[$name]) } finally { $writer.Dispose() }
        }
    } finally { $zip.Dispose() }
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('evnova-setup-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    $target = Join-Path $testRoot 'install'
    foreach ($bad in @('../outside', 'C:\outside', '/outside', 'nested/../../outside', 'file:stream')) {
        Assert-NovaRejected { Get-NovaChildPath $target $bad }
    }
    $package = [pscustomobject]@{
        files = @([pscustomobject]@{source='data/resource'; target='Nova Files/resource'})
        archive='good.zip'; url='https://invalid.example/good.zip'; sha256='wrong'; size=1
    }
    $good = Join-Path $testRoot 'good.zip'
    New-TestArchive $good @{'data/resource'='original'; 'Pilots/private.plt'='excluded'}
    Install-NovaArchive $package $good $target
    $resource = Join-Path $target 'Nova Files/resource'
    Assert-Nova ((Get-Content $resource -Raw) -eq 'original') 'Resource was not installed'
    Assert-Nova (-not (Test-Path (Join-Path $target 'Pilots'))) 'An unlisted file was extracted'
    Set-Content -LiteralPath $resource -Value 'custom' -NoNewline
    Install-NovaArchive $package $good $target
    Assert-Nova ((Get-Content $resource -Raw) -eq 'custom') 'Existing resource was overwritten'
    Assert-Nova (Test-NovaPackageInstalled $package $target) 'Installed resources were not detected'
    Assert-NovaRejected { Get-NovaArchive $package $testRoot }
    $package.sha256 = (Get-FileHash $good -Algorithm SHA256).Hash
    Assert-Nova ((Get-NovaArchive $package $testRoot) -eq $good) 'Valid offline cache was rejected'
    $badZip = Join-Path $testRoot 'bad.zip'
    New-TestArchive $badZip @{'data/resource'='bad'; '../escaped'='bad'}
    Assert-NovaRejected { Install-NovaArchive $package $badZip (Join-Path $testRoot 'rejected') }
    Assert-Nova (-not (Test-Path (Join-Path $testRoot 'escaped'))) 'Archive escaped staging'
    Assert-Nova (-not (Test-Path (Join-Path $testRoot 'rejected'))) 'Rejected archive was partially installed'
    Assert-Nova (@(Get-ChildItem -LiteralPath $testRoot -Filter '.evnova-setup-*').Count -eq 0) 'Staging was not cleaned'
    $graphics = Join-Path $testRoot 'graphics.zip'
    $entries = @{'0123456789abcdef-1x1.evs'='test frame'; 'manifest.json'='{}'; 'provenance.json'='{}';
                 'Real-ESRGAN-LICENSE.txt'='license'; 'CREDITS.md'='original artists'; 'NOTICE.md'='not MIT artwork'}
    New-TestArchive $graphics $entries
    $graphicsPackage = [pscustomobject]@{
        id='test-4x-v1'; size=(Get-Item $graphics).Length;
        sha256=(Get-FileHash $graphics -Algorithm SHA256).Hash;
        file_count=$entries.Count; expanded_bytes=($entries.Values | ForEach-Object { [Text.Encoding]::UTF8.GetByteCount($_) } | Measure-Object -Sum).Sum
    }
    $graphicsTarget = Join-Path $testRoot 'test-4x-v1'
    Install-NovaGraphicsArchive $graphicsPackage $graphics $graphicsTarget
    Assert-Nova ((Get-Content (Join-Path $graphicsTarget '.installed') -Raw).Trim() -eq 'test-4x-v1') 'No completed graphics receipt'
    Assert-Nova (Test-Path (Join-Path $graphicsTarget 'CREDITS.md')) 'Graphics credits lost'
    $graphicsPackage.sha256 = 'bad'
    Assert-NovaRejected { Install-NovaGraphicsArchive $graphicsPackage $graphics $graphicsTarget }
    $graphicsPackage.sha256 = (Get-FileHash $graphics -Algorithm SHA256).Hash
    $graphicsPackage.file_count += 1
    Assert-NovaRejected { Install-NovaGraphicsArchive $graphicsPackage $graphics $graphicsTarget }
    $graphicsPackage.file_count -= 1
    Assert-NovaRejected { Install-NovaGraphicsArchive $graphicsPackage $graphics (Join-Path $testRoot 'wrong-destination') }
    $unsafeGraphics = Join-Path $testRoot 'unsafe-graphics.zip'
    $entries['../outside.evs'] = 'bad'
    New-TestArchive $unsafeGraphics $entries
    $graphicsPackage.size = (Get-Item $unsafeGraphics).Length
    $graphicsPackage.sha256 = (Get-FileHash $unsafeGraphics -Algorithm SHA256).Hash
    Assert-NovaRejected { Install-NovaGraphicsArchive $graphicsPackage $unsafeGraphics $graphicsTarget }
    Assert-Nova (-not (Test-Path (Join-Path $testRoot 'outside.evs'))) 'Graphics escaped staging'
    Assert-Nova (@(Get-ChildItem -LiteralPath $testRoot -Filter '.graphics-setup-*').Count -eq 0) 'Graphics staging was not cleaned'
    Write-Output 'Installer checks passed: graphics completeness, credits, hashes, offline cache, preservation and unsafe paths.'
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $resolved) -notlike 'evnova-setup-check-*') { throw 'Unsafe test cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
