param([string]$Python = 'python')

$ErrorActionPreference = 'Stop'
$novaRoot = Split-Path -Parent $PSScriptRoot
$novaEnvironment = Join-Path $novaRoot '.cache\narrator'
$novaPython = Join-Path $novaEnvironment 'Scripts\python.exe'
$novaVoices = Join-Path $novaRoot '.cache\narrator-voices'
if (-not (Test-Path -LiteralPath $novaPython)) {
    & $Python -m venv $novaEnvironment
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the narration environment.' }
}
& $novaPython -m pip install 'piper-tts==1.8.0'
if ($LASTEXITCODE -ne 0) { throw 'Could not install the narration engine.' }
New-Item -ItemType Directory -Path $novaVoices -Force | Out-Null
& $novaPython -m piper.download_voices --data-dir $novaVoices en_US-lessac-medium en_US-ryan-medium en_GB-alba-medium en_GB-alan-medium
if ($LASTEXITCODE -ne 0) { throw 'Could not download the narration voice.' }
foreach ($novaVoice in @('en_US-lessac-medium', 'en_US-ryan-medium', 'en_GB-alba-medium', 'en_GB-alan-medium')) {
    $novaParts = $novaVoice.Split('-')
    $novaCard = 'https://huggingface.co/rhasspy/piper-voices/resolve/main/en/{0}/{1}/{2}/MODEL_CARD' -f $novaParts[0], $novaParts[1], $novaParts[2]
    Invoke-WebRequest -Uri $novaCard -OutFile (Join-Path $novaVoices ($novaVoice + '.MODEL_CARD'))
}
Write-Output 'Local narration is ready.'
