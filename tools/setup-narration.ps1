param(
    [string]$Destination = (Join-Path $env:APPDATA 'Ambrosia Software\EV Nova\NarrationRuntime'),
    [string]$StatusFile,
    [string]$SeedDirectory
)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + [IO.Path]::PathSeparator + $env:PSModulePath
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$novaRoot = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Path $novaRoot -Force | Out-Null
if (-not $StatusFile) { $StatusFile = Join-Path $novaRoot 'setup-status.txt' }
$novaLock = $null
function Set-NovaVoiceStatus([string]$Message) {
    [IO.File]::WriteAllText($StatusFile, $Message, [Text.UTF8Encoding]::new($false))
    Write-Output $Message
}
function Get-NovaVoiceDependency($Entry, [string]$Path) {
    if (-not (Test-Path -LiteralPath $Path) -or (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $Entry.sha256) {
        Invoke-WebRequest -UseBasicParsing -Uri $Entry.url -OutFile ($Path + '.part') -TimeoutSec 180
        if ((Get-FileHash -LiteralPath ($Path + '.part') -Algorithm SHA256).Hash -ine $Entry.sha256) { throw 'A narration download failed its checksum check.' }
        Move-Item -LiteralPath ($Path + '.part') -Destination $Path -Force
    }
}
function Invoke-NovaVoicePython([string[]]$PythonArguments) {
    $previous = $ErrorActionPreference
    try {
        # Windows PowerShell treats native stderr warnings as error records.
        # Only the process exit code determines whether installation failed.
        $ErrorActionPreference = 'Continue'
        & $novaPython @PythonArguments *>> (Join-Path $novaRoot 'install-log.txt')
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    if ($code -ne 0) { throw 'A speech setup step failed. See install-log.txt and try again.' }
}
try {
    $novaLock = [IO.File]::Open((Join-Path $novaRoot 'setup.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    $novaManifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'narration-downloads.json') -Raw | ConvertFrom-Json
    $novaRuntime = Join-Path $novaRoot '.cache/narrator'
    $novaVoices = Join-Path $novaRoot '.cache/narrator-voices'
    $novaDownloads = Join-Path $novaRoot 'Downloads'
    New-Item -ItemType Directory -Path $novaRuntime,$novaVoices,$novaDownloads,(Join-Path $novaRoot 'tools') -Force | Out-Null
    $novaPython = Join-Path $novaRuntime 'python.exe'
    if (-not (Test-Path -LiteralPath $novaPython)) {
        Set-NovaVoiceStatus 'Installing the speech runtime...'
        $novaArchive = Join-Path $novaDownloads 'python.zip'
        Get-NovaVoiceDependency $novaManifest.python $novaArchive
        Expand-Archive -LiteralPath $novaArchive -DestinationPath $novaRuntime -Force
        [IO.File]::WriteAllText((Join-Path $novaRuntime 'python311._pth'), "python311.zip`n.`nLib/site-packages`nimport site`n")
    }
    Set-NovaVoiceStatus 'Preparing the speech engine...'
    $novaPip = Join-Path $novaDownloads 'get-pip.py'
    Get-NovaVoiceDependency $novaManifest.pip $novaPip
    Invoke-NovaVoicePython @($novaPip, '--disable-pip-version-check', '--no-warn-script-location')
    Invoke-NovaVoicePython @('-m','pip','install','--disable-pip-version-check','--no-warn-script-location','--only-binary=:all:','piper-tts==1.8.0')
    $novaIds = @('en_US-lessac-medium','en_US-ryan-medium','en_GB-alba-medium','en_GB-alan-medium')
    foreach ($novaVoice in $novaIds) {
        $novaParts = $novaVoice.Split('-')
        Set-NovaVoiceStatus ('Installing voice: ' + $novaParts[1] + '...')
        if ($SeedDirectory) {
            foreach ($novaExtension in @('.onnx','.onnx.json','.MODEL_CARD')) {
                $novaSeed = Join-Path $SeedDirectory ($novaVoice + $novaExtension)
                $novaTarget = Join-Path $novaVoices ($novaVoice + $novaExtension)
                if ((Test-Path -LiteralPath $novaSeed) -and -not (Test-Path -LiteralPath $novaTarget)) { Copy-Item -LiteralPath $novaSeed -Destination $novaTarget }
            }
        }
        Invoke-NovaVoicePython @('-m','piper.download_voices','--data-dir',$novaVoices,$novaVoice)
        $novaCard = Join-Path $novaVoices ($novaVoice + '.MODEL_CARD')
        if (-not (Test-Path -LiteralPath $novaCard)) {
            $novaUrl = 'https://huggingface.co/rhasspy/piper-voices/resolve/main/en/{0}/{1}/{2}/MODEL_CARD' -f $novaParts[0],$novaParts[1],$novaParts[2]
            Invoke-WebRequest -UseBasicParsing -Uri $novaUrl -OutFile $novaCard -TimeoutSec 120
        }
        # Load each model before claiming it is installed; truncated downloads fail here.
        $novaValidate = @('-c','import sys; from piper import PiperVoice; PiperVoice.load(sys.argv[1])',(Join-Path $novaVoices ($novaVoice + '.onnx')))
        try { Invoke-NovaVoicePython $novaValidate }
        catch {
            Set-NovaVoiceStatus ('Repairing voice: ' + $novaParts[1] + '...')
            Invoke-NovaVoicePython @('-m','piper.download_voices','--data-dir',$novaVoices,'--force-redownload',$novaVoice)
            Invoke-NovaVoicePython $novaValidate
        }
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'nova_narrator.py') -Destination (Join-Path $novaRoot 'tools/nova_narrator.py') -Force
    Set-NovaVoiceStatus 'All four voices are installed. Select one and press Preview.'
} catch {
    Set-NovaVoiceStatus ('Installation failed: ' + $_.Exception.Message)
    exit 1
} finally {
    if ($novaLock) { $novaLock.Dispose() }
}
