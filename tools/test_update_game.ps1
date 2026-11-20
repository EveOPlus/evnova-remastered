param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'update-game.ps1') -LibraryOnly
function Assert-Nova($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Reject-Nova([scriptblock]$Action) { $rejected=$false; try { & $Action } catch { $rejected=$true }; Assert-Nova $rejected 'Expected rejection' }
$root = Join-Path ([IO.Path]::GetTempPath()) ('evnova-update-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
try {
    Assert-Nova ((Compare-NovaVersion '0.1.0-preview.10' '0.1.0-preview.2') -gt 0) 'Preview sorting'
    Assert-Nova ((Compare-NovaVersion '0.1.0' '0.1.0-preview.10') -gt 0) 'Stable promotion'
    Assert-Nova ((Compare-NovaVersion '1.0.0' '1.0.0') -eq 0) 'Equal version'
    Reject-Nova { Get-NovaVersion 'nightly' }
    $asset = [pscustomobject]@{name='evnova-windows-x64.zip';state='uploaded';digest=('sha256:' + ('a'*64));size=1024;browser_download_url='https://github.com/EveOPlus/evnova-remastered/releases/download/v0.2.0-preview.2/evnova-windows-x64.zip'}
    $release = [pscustomobject]@{draft=$false;prerelease=$true;tag_name='v0.2.0-preview.2';assets=@($asset)}
    Assert-Nova ($null -eq (Get-NovaRelease @($release) '0.1.0')) 'Stable accepted a preview'
    Assert-Nova ($null -ne (Get-NovaRelease @($release) '0.1.0-preview.1')) 'Preview release missing'
    $release.draft=$true
    Assert-Nova ($null -eq (Get-NovaRelease @($release) '0.1.0-preview.1')) 'Draft accepted'
    $release.draft=$false; $asset.browser_download_url='https://example.com/game.zip'
    Assert-Nova ($null -eq (Get-NovaRelease @($release) '0.1.0-preview.1')) 'Untrusted download accepted'
    $stage=Join-Path $root 'stage'; $install=Join-Path $root 'installed'; $work=Join-Path $install '.evnova-update'
    New-Item -ItemType Directory -Path $stage,$install,$work -Force | Out-Null
    $names=@('docs/automatic_updates.md','docs/arpia2.md','README.md','evnova.exe','CREDITS.md','LICENSE.txt','tools/update-game.ps1')
    $manifest=[ordered]@{schema=1;repository='EveOPlus/evnova-remastered';version='0.1.0-preview.2';files=@()}
    foreach ($name in $names) {
        $path=Get-NovaUpdatePath $stage $name
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        [IO.File]::WriteAllText($path,'new ' + $name)
        $manifest.files+=@{path=$name;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
    Write-NovaJson (Join-Path $stage 'release.json') $manifest
    $archive=Join-Path $root 'package.zip'
    $zip=[IO.Compression.ZipFile]::Open($archive,'Create')
    foreach ($name in $names + @('release.json')) { $null=[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $stage $name),('EV Nova Remastered/' + $name)) }
    $zip.Dispose()
    $expected=[pscustomobject]@{version='0.1.0-preview.2';size=(Get-Item $archive).Length;sha256=(Get-FileHash $archive -Algorithm SHA256).Hash}
    $validated=Join-Path $root 'validated'
    $checked=Expand-NovaUpdate $archive $validated $expected
    $badArchive=Join-Path $root 'unsafe.zip'
    $zip=[IO.Compression.ZipFile]::Open($badArchive,'Create')
    $entry=$zip.CreateEntry('EV Nova Remastered/../escaped.exe')
    $writer=[IO.StreamWriter]::new($entry.Open()); $writer.Write('unsafe'); $writer.Dispose(); $zip.Dispose()
    $badExpected=[pscustomobject]@{version='0.1.0-preview.2';size=(Get-Item $badArchive).Length;sha256=(Get-FileHash $badArchive -Algorithm SHA256).Hash}
    Reject-Nova { Expand-NovaUpdate $badArchive (Join-Path $root 'unsafe-stage') $badExpected }
    Assert-Nova (-not (Test-Path (Join-Path $root 'escaped.exe'))) 'Archive traversal escaped staging'
    $expected.sha256='b'*64
    Reject-Nova { Expand-NovaUpdate $archive (Join-Path $root 'bad-hash') $expected }
    Reject-Nova { Get-NovaUpdatePath $install '../pilot.plt' }
    Assert-Nova (-not (Test-NovaEngineFile 'Pilots/private.plt')) 'Pilot path allowed'
    Assert-Nova (-not (Test-NovaEngineFile 'EV Nova/Nova.rez')) 'Game data path allowed'
    foreach ($name in @('evnova.exe','README.md','Pilots/private.plt','EV Nova/Nova.rez','Narration/voice.onnx')) {
        $path=Get-NovaUpdatePath $install $name
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        [IO.File]::WriteAllText($path,'original')
    }
    # A locked later file forces rollback of an earlier newly installed document.
    $locked=[IO.File]::Open((Join-Path $install 'README.md'),'Open','Read','None')
    try { Reject-Nova { Install-NovaUpdate $install $work $validated $checked 'evnova.exe' } }
    finally { $locked.Dispose() }
    Assert-Nova (-not (Test-Path (Join-Path $install 'docs/automatic_updates.md'))) 'Partial update not rolled back'
    Assert-Nova (-not (Test-Path (Join-Path $install 'docs/arpia2.md'))) 'Multiple-file rollback failed'
    Assert-Nova ((Get-Content (Join-Path $install 'evnova.exe') -Raw) -eq 'original') 'Engine changed after failure'
    Install-NovaUpdate $install $work $validated $checked 'evnova.exe'
    Assert-Nova ((Get-Content (Join-Path $install 'evnova.exe') -Raw) -eq 'new evnova.exe') 'Engine not updated'
    foreach ($name in @('Pilots/private.plt','EV Nova/Nova.rez','Narration/voice.onnx')) {
        Assert-Nova ((Get-Content (Join-Path $install $name) -Raw) -eq 'original') 'User data changed'
    }
    Write-Output 'Updater checks passed: numeric versions, channels, drafts, source restrictions, hashes, safe paths, rollback and user data preservation.'
} finally {
    $absolute=[IO.Path]::GetFullPath($root)
    $temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if (-not $absolute.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $absolute) -notlike 'evnova-update-test-*') { throw 'Unsafe test cleanup' }
    Remove-Item -LiteralPath $absolute -Recurse -Force
}
