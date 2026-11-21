param([Parameter(Mandatory=$true)][double]$Scalar)

$root = 'C:\Users\victor\Documents\Driver Audio\cros-sofaudio-rtl'
$id = '{0.0.1.00000000}.{f4d9114e-312c-4992-b350-7239a777eb57}'
$wav = 'C:\Users\victor\AppData\Local\Temp\opencode\mic-array-scalar.wav'

& "$root\tests\Set-CaptureVolume.exe" set $Scalar | Out-Null
& "$root\tests\Test-WasapiCaptureNative.exe" $id 2 $wav | Out-Null

$b = [IO.File]::ReadAllBytes($wav)
$ascii = [Text.Encoding]::ASCII.GetString($b, 0, 200)
$pos = $ascii.IndexOf('data')
$dataPos = $pos + 8
$frames = [int](($b.Length - $dataPos) / 8)

$sum = 0.0; $sumSq = 0.0; $peak = 0.0; $clip = 0
for ($i = 0; $i -lt $frames; $i++) {
    $v = [BitConverter]::ToSingle($b, $dataPos + $i * 8)
    $sum += $v
    $sumSq += $v * $v
    if ([Math]::Abs($v) -gt [Math]::Abs($peak)) { $peak = $v }
    if ([Math]::Abs($v) -gt 1.0) { $clip++ }
}
$mean = $sum / $frames
$rms = [Math]::Sqrt($sumSq / $frames)
$acrms = [Math]::Sqrt([Math]::Max(0.0, $rms * $rms - $mean * $mean))

$vol = & "$root\tests\Set-CaptureVolume.exe"
$dB = ($vol | Select-String 'CAPTURE/Multimedia scalar').Line

Write-Output ("scalar={0:N4} {1} mean={2:N4} rms={3:N4} ac_rms={4:N4} peak={5:N4} clipped={6}/{7}" -f $Scalar, $dB, $mean, $rms, $acrms, $peak, $clip, $frames)
