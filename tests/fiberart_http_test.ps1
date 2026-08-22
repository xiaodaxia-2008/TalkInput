param(
    [string]$AudioPath = (Join-Path $PSScriptRoot 'test_data/fiberart.m4a'),
    [string]$Endpoint = 'http://127.0.0.1:8766/v1/audio/transcriptions'
)

$ErrorActionPreference = 'Stop'

if (!(Test-Path -LiteralPath $AudioPath -PathType Leaf)) {
    throw "Audio file not found: $AudioPath"
}

$response = curl.exe -sS -X POST $Endpoint `
    -F "file=@$AudioPath" `
    -F 'model=gpt-4o-mini-transcribe'

if ($LASTEXITCODE -ne 0) {
    throw "HTTP transcription request failed with exit code $LASTEXITCODE."
}

$result = $response | ConvertFrom-Json
if ($result.error) {
    throw "Transcription failed: $($result.error.message)"
}

$text = [string]$result.text
if ([string]::IsNullOrWhiteSpace($text)) {
    throw 'Transcription returned empty text.'
}

Write-Output $text
