param(
    [ValidateRange(5, 60)]
    [int]$Seconds = 20,

    [string]$DeviceId = '{0.0.1.00000000}.{f4d9114e-312c-4992-b350-7239a777eb57}',

    [string]$WavPath = 'C:\Users\victor\AppData\Local\Temp\opencode\mic-array-live.wav',

    [double]$MinBlockAcRms = 0.001,

    [int]$MinZeroCrossings = 100
)

$ErrorActionPreference = 'Stop'

$captureExe = Join-Path $PSScriptRoot 'Test-WasapiCaptureNative.exe'
$analyzeExe = Join-Path $PSScriptRoot 'Analyze-Wav.exe'

foreach ($exe in @($captureExe, $analyzeExe)) {
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "Missing test utility: $exe"
    }
}

Write-Host 'Speak continuously for the whole capture. Silence or a short startup sound is not a pass.'
Write-Host "Recording $Seconds seconds from $DeviceId"

& $captureExe $DeviceId $Seconds $WavPath | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "Capture failed with exit code $LASTEXITCODE"
}

$output = & $analyzeExe $WavPath
$output | ForEach-Object { Write-Host $_ }

$text = $output -join "`n"

$meanMatch = [regex]::Match($text, 'mean=(-?[0-9.]+)')
$acMatch = [regex]::Match($text, 'ac_rms=(-?[0-9.]+)')
$zcMatch = [regex]::Match($text, 'zero_crossings=([0-9]+)')
$blockMatch = [regex]::Match($text, 'block_ac_rms:([^\r\n]*)')

if (-not ($meanMatch.Success -and $acMatch.Success -and $zcMatch.Success -and $blockMatch.Success)) {
    throw 'Could not parse Analyze-Wav output.'
}

$mean = [double]::Parse($meanMatch.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
$acRms = [double]::Parse($acMatch.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
$zeroCrossings = [int]::Parse($zcMatch.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
$blocks = @($blockMatch.Groups[1].Value.Trim() -split '\s+' |
    Where-Object { $_ -ne '' } |
    ForEach-Object { [double]::Parse($_, [Globalization.CultureInfo]::InvariantCulture) })

# The first block contains the stream startup transient; ignore it.
$speechBlocks = @($blocks | Select-Object -Skip 1)
$activeBlocks = @($speechBlocks | Where-Object { $_ -ge $MinBlockAcRms }).Count
$requiredBlocks = [int][Math]::Ceiling($speechBlocks.Count * 0.75)

$passed = ($activeBlocks -ge $requiredBlocks) -and ($zeroCrossings -ge $MinZeroCrossings)

Write-Host ''
Write-Host ("Summary: mean={0:N6} ac_rms={1:N6} zero_crossings={2} active_blocks={3}/{4} threshold={5:N6}" -f `
    $mean, $acRms, $zeroCrossings, $activeBlocks, $speechBlocks.Count, $MinBlockAcRms)

if ($passed) {
    Write-Host 'RESULT: PASS - sustained alternating-capture signal detected.'
    exit 0
}

Write-Host 'RESULT: FAIL - no sustained speech-like signal. Do not publish this build.'
exit 1
