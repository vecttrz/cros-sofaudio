Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

[Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IMMDevice3
{
    void Activate(ref Guid iid, int dwClsCtx, IntPtr pActivationParams, out IntPtr ppInterface);
    void OpenPropertyStore(int stgmAccess, out IntPtr ppProperties);
    void GetId(out IntPtr ppstrId);
    void GetState(out int pdwState);
}

[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IMMDeviceEnumerator3
{
    void EnumAudioEndpoints(int dataFlow, int dwStateMask, out IntPtr ppDevices);
    void GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice3 ppEndpoint);
    void GetDevice([MarshalAs(UnmanagedType.LPWStr)] string pwstrId, out IMMDevice3 ppDevice);
    void RegisterEndpointNotificationCallback(IntPtr pClient);
    void UnregisterEndpointNotificationCallback(IntPtr pClient);
}

[Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IAudioEndpointVolume
{
    void RegisterControlChangeNotify(IntPtr pNotify);
    void UnregisterControlChangeNotify(IntPtr pNotify);
    void GetChannelCount(out int count);
    void SetMasterVolumeLevel(float level, ref Guid guid);
    void SetMasterVolumeLevelScalar(float level, ref Guid guid);
    void GetMasterVolumeLevel(out float level);
    void GetMasterVolumeLevelScalar(out float level);
    void SetChannelVolumeLevel(int index, float level, ref Guid guid);
    void SetChannelVolumeLevelScalar(int index, float level, ref Guid guid);
    void GetChannelVolumeLevel(int index, out float level);
    void GetChannelVolumeLevelScalar(int index, out float level);
    void SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid guid);
    void GetMute(out bool mute);
}

public static class ToneTest
{
    public static string PrepareVolume()
    {
        Type t = Type.GetTypeFromCLSID(new Guid("BCDE0395-E52F-467C-8E3D-C4579291692E"));
        object obj = Activator.CreateInstance(t);
        IMMDeviceEnumerator3 en = (IMMDeviceEnumerator3)obj;
        IMMDevice3 dev;
        en.GetDefaultAudioEndpoint(0, 0, out dev);
        Guid iid = new Guid("5CDF2C82-841E-4546-9722-0CF74078229A");
        IntPtr volPtr;
        dev.Activate(ref iid, 1, IntPtr.Zero, out volPtr);
        IAudioEndpointVolume vol = (IAudioEndpointVolume)Marshal.GetObjectForIUnknown(volPtr);
        float scalar;
        bool mute;
        vol.GetMasterVolumeLevelScalar(out scalar);
        vol.GetMute(out mute);
        string res = "before: volume=" + scalar + " mute=" + mute + "; ";
        Guid ctx = Guid.Empty;
        vol.SetMasterVolumeLevelScalar(1.0f, ref ctx);
        vol.SetMute(false, ref ctx);
        vol.GetMasterVolumeLevelScalar(out scalar);
        vol.GetMute(out mute);
        res += "after: volume=" + scalar + " mute=" + mute;
        Marshal.ReleaseComObject(vol);
        Marshal.Release(volPtr);
        Marshal.ReleaseComObject(dev);
        Marshal.ReleaseComObject(obj);
        return res;
    }
}
'@

[ToneTest]::PrepareVolume()

$rate = 48000
$seconds = 4
$freq = 1000
$samples = $rate * $seconds
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
# RIFF header placeholder
$bw.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF'))
$bw.Write([int](36 + $samples * 4))
$bw.Write([System.Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
$bw.Write([int]16)
$bw.Write([int16]1)    # PCM
$bw.Write([int16]2)    # channels
$bw.Write([int]$rate)
$bw.Write([int]($rate * 4))
$bw.Write([int16]4)    # block align
$bw.Write([int16]16)   # bits
$bw.Write([System.Text.Encoding]::ASCII.GetBytes('data'))
$bw.Write([int]($samples * 4))
for ($i = 0; $i -lt $samples; $i++) {
    $t = $i / [double]$rate
    $v = [int]([Math]::Sin(2 * [Math]::PI * $freq * $t) * 28000)
    $bw.Write([int16]$v)
    $bw.Write([int16]$v)
}
$bw.Flush()
$wavPath = Join-Path $env:TEMP 'sof_loud_tone.wav'
[System.IO.File]::WriteAllBytes($wavPath, $ms.ToArray())
$bw.Close(); $ms.Close()
"Playing $($seconds)s 1000 Hz tone from $wavPath"
$player = New-Object System.Media.SoundPlayer($wavPath)
$player.PlaySync()
'play done'
