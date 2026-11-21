param(
    [Parameter(Mandatory = $true)]
    [string]$DeviceId,
    [ValidateRange(1, 30)]
    [int]$Seconds = 5,
    [string]$OutputWav = ''
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class WasapiCapture
{
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    private class MMDeviceEnumerator { }

    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDeviceEnumerator
    {
        int EnumAudioEndpoints(EDataFlow dataFlow, int dwStateMask, out IMMDeviceCollection ppDevices);
        int GetDefaultAudioEndpoint(EDataFlow dataFlow, ERole role, out IMMDevice ppEndpoint);
        int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string pwstrId, out IMMDevice ppDevice);
        int RegisterEndpointNotificationCallback(IntPtr pClient);
        int UnregisterEndpointNotificationCallback(IntPtr pClient);
    }

    [ComImport, Guid("29772E18-0958-4E43-B9B1-2B578DBE3E01"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDeviceCollection
    {
        int GetCount(out int pcDevices);
        int Item(int nDevice, out IMMDevice ppDevice);
    }

    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDevice
    {
        int Activate(ref Guid iid, int dwClsCtx, IntPtr pActivationParams, [MarshalAs(UnmanagedType.IUnknown)] out object ppInterface);
        int OpenPropertyStore(int stgm, out IPropertyStore ppstg);
        int GetId([MarshalAs(UnmanagedType.LPWStr)] out string ppstrId);
        int GetState(out int pdwState);
    }

    [ComImport, Guid("886D8EEB-6CF2-5038-8888-99DBA9109FCE"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IPropertyStore
    {
        int GetCount(out int cProps);
        int GetAt(int iProp, out PROPERTYKEY pkey);
        int GetValue(ref PROPERTYKEY key, out PROPVARIANT pv);
        int SetValue(ref PROPERTYKEY key, ref PROPVARIANT propvar);
        int Commit();
    }

    [ComImport, Guid("1CB9AD4C-DBFA-4C32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IAudioClient
    {
        int Initialize(int shareMode, int streamFlags, long hnsBufferDuration, long hnsPeriodicity, IntPtr pFormat, IntPtr audioSessionGuid);
        int GetBufferSize(out int numBufferFrames);
        int GetStreamLatency(out long hnsLatency);
        int GetCurrentPadding(out int numPaddingFrames);
        int IsFormatSupported(int shareMode, IntPtr pFormat, out IntPtr closestMatchFormat);
        int GetMixFormat(out IntPtr ppDeviceFormat);
        int GetDevicePeriod(out long hnsDefaultDevicePeriod, out long hnsMinimumDevicePeriod);
        int Start();
        int Stop();
        int Reset();
        int SetEventHandle(IntPtr eventHandle);
        int GetService(ref Guid riid, [MarshalAs(UnmanagedType.IUnknown)] out object ppv);
    }

    [ComImport, Guid("BDED968F-763C-4355-9CF9-58C3A3F6B1B7"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IAudioCaptureClient
    {
        int GetBuffer(out IntPtr pData, out int numFramesToRead, out int dwFlags, out long u64DevicePosition, out long u64QPCPosition);
        int ReleaseBuffer(int numFramesRead);
        int GetNextPacketSize(out int numFramesInNextPacket);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PROPERTYKEY
    {
        public Guid fmtid;
        public int pid;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PROPVARIANT
    {
        public short vt;
        public short wReserved1;
        public short wReserved2;
        public short wReserved3;
        public IntPtr union1;
        public IntPtr union2;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WAVEFORMATEX
    {
        public ushort wFormatTag;
        public ushort nChannels;
        public uint nSamplesPerSec;
        public uint nAvgBytesPerSec;
        public ushort nBlockAlign;
        public ushort wBitsPerSample;
        public ushort cbSize;
    }

    private enum EDataFlow { eRender = 0, eCapture = 1 }
    private enum ERole { eConsole = 0, eMultimedia = 1, eCommunications = 2 }

    private const int CLSCTX_ALL = 23;
    private const int DEVICE_STATE_ACTIVE = 1;
    private const int AUDCLNT_SHAREMODE_SHARED = 0;
    private const int AUDCLNT_STREAMFLAGS_EVENTCALLBACK = 0x00040000;
    private const int AUDCLNT_STREAMFLAGS_NOPERSIST = 0x00080000;
    private const int AUDCLNT_S_BUFFER_EMPTY = 1;
    private const long MILLISECONDS = 10000L;

    private static readonly Guid IID_IMMDeviceEnumerator = new Guid("A95664D2-9614-4F35-A746-DE8DB63617E6");
    private static readonly Guid IID_IAudioClient = new Guid("1CB9AD4C-DBFA-4C32-B178-C2F568A703B2");
    private static readonly Guid IID_IAudioCaptureClient = new Guid("BDED968F-763C-4355-9CF9-58C3A3F6B1B7");
    private static readonly PROPERTYKEY PKEY_Device_FriendlyName = new PROPERTYKEY
    {
        fmtid = new Guid("A45C254E-DF1C-4EFD-8020-67D146A850E0"),
        pid = 14
    };

    public static string Capture(string deviceId, int seconds, string outputWav)
    {
        object enumeratorObject = new MMDeviceEnumerator();
        IMMDeviceEnumerator enumerator = (IMMDeviceEnumerator)enumeratorObject;
        IMMDevice device;
        int hr = enumerator.GetDevice(deviceId, out device);
        if (hr != 0)
            throw new InvalidOperationException("GetDevice falló con 0x" + hr.ToString("X8"));

        string friendlyName = "";

        object clientObject;
        Guid iidClient = IID_IAudioClient;
        hr = device.Activate(ref iidClient, CLSCTX_ALL, IntPtr.Zero, out clientObject);
        if (hr != 0)
            throw new InvalidOperationException("Activate IAudioClient falló con 0x" + hr.ToString("X8"));
        IAudioClient client = (IAudioClient)clientObject;

        IntPtr formatPointer = IntPtr.Zero;
        hr = client.GetMixFormat(out formatPointer);
        if (hr != 0 || formatPointer == IntPtr.Zero)
            throw new InvalidOperationException("GetMixFormat falló con 0x" + hr.ToString("X8"));

        WAVEFORMATEX format = (WAVEFORMATEX)Marshal.PtrToStructure(formatPointer, typeof(WAVEFORMATEX));
        int formatSize = 18 + format.cbSize;
        byte[] formatBytes = new byte[formatSize];
        Marshal.Copy(formatPointer, formatBytes, 0, formatSize);
        Marshal.FreeCoTaskMem(formatPointer);

        long bufferDuration = 1000L * MILLISECONDS;
        hr = client.Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
            bufferDuration, 0, formatBytes == null ? IntPtr.Zero : AllocHGlobal(formatBytes), IntPtr.Zero);
        if (hr != 0)
            throw new InvalidOperationException("Initialize falló con 0x" + hr.ToString("X8"));

        int bufferFrames;
        hr = client.GetBufferSize(out bufferFrames);
        if (hr != 0)
            throw new InvalidOperationException("GetBufferSize falló con 0x" + hr.ToString("X8"));

        object captureObject;
        Guid iidCapture = IID_IAudioCaptureClient;
        hr = client.GetService(ref iidCapture, out captureObject);
        if (hr != 0)
            throw new InvalidOperationException("GetService IAudioCaptureClient falló con 0x" + hr.ToString("X8"));
        IAudioCaptureClient capture = (IAudioCaptureClient)captureObject;

        IntPtr eventHandle = CreateEvent(IntPtr.Zero, false, false, null);
        if (eventHandle == IntPtr.Zero)
            throw new InvalidOperationException("CreateEvent falló");
        hr = client.SetEventHandle(eventHandle);
        if (hr != 0)
            throw new InvalidOperationException("SetEventHandle falló con 0x" + hr.ToString("X8"));

        int blockAlign = format.nBlockAlign;
        int bytesPerSample = format.wBitsPerSample / 8;
        int channels = format.nChannels;
        int sampleRate = (int)format.nSamplesPerSec;
        long totalFrames = 0;
        long sumSquare = 0;
        long peak = 0;
        int silentBuffers = 0;
        int nonSilentBuffers = 0;
        int minPeak = int.MaxValue;
        int maxPeak = 0;

        System.Collections.Generic.List<byte> pcm = null;
        if (!string.IsNullOrEmpty(outputWav))
            pcm = new System.Collections.Generic.List<byte>(sampleRate * channels * bytesPerSample * seconds);

        hr = client.Start();
        if (hr != 0)
            throw new InvalidOperationException("Start falló con 0x" + hr.ToString("X8"));

        DateTime deadline = DateTime.UtcNow.AddSeconds(seconds + 2);
        while (DateTime.UtcNow < deadline && totalFrames < (long)sampleRate * seconds)
        {
            if (WaitForSingleObject(eventHandle, 500) != 0)
                continue;

            int packetSize;
            while (capture.GetNextPacketSize(out packetSize) == 0 && packetSize > 0)
            {
                IntPtr data;
                int frames;
                int flags;
                long devicePosition;
                long qpcPosition;
                hr = capture.GetBuffer(out data, out frames, out flags, out devicePosition, out qpcPosition);
                if (hr != 0)
                    break;

                int byteCount = frames * blockAlign;
                if ((flags & AUDCLNT_S_BUFFER_EMPTY) == 0 && byteCount > 0)
                {
                    long packetSum = 0;
                    long packetPeak = 0;
                    for (int i = 0; i < byteCount; i += bytesPerSample)
                    {
                        long sample = 0;
                        if (bytesPerSample == 2)
                            sample = Marshal.ReadInt16(data, i);
                        else if (bytesPerSample == 4)
                            sample = Marshal.ReadInt32(data, i) >> 8;
                        else if (bytesPerSample == 1)
                            sample = (sbyte)Marshal.ReadByte(data, i) << 8;
                        long absolute = Math.Abs(sample);
                        packetSum += absolute * absolute;
                        if (absolute > packetPeak)
                            packetPeak = absolute;
                    }
                    sumSquare += packetSum;
                    if (packetPeak > peak)
                        peak = packetPeak;
                    if (packetPeak == 0)
                        silentBuffers++;
                    else
                    {
                        nonSilentBuffers++;
                        if (packetPeak < minPeak) minPeak = (int)packetPeak;
                        if (packetPeak > maxPeak) maxPeak = (int)packetPeak;
                    }

                    if (pcm != null)
                    {
                        byte[] managed = new byte[byteCount];
                        Marshal.Copy(data, managed, 0, byteCount);
                        pcm.AddRange(managed);
                    }
                    totalFrames += frames;
                }

                capture.ReleaseBuffer(frames);
            }
        }

        client.Stop();
        CloseHandle(eventHandle);
        Marshal.ReleaseComObject(capture);
        Marshal.ReleaseComObject(client);
        Marshal.ReleaseComObject(device);
        Marshal.ReleaseComObject(enumerator);

        double rms = totalFrames > 0 ? Math.Sqrt((double)sumSquare / (totalFrames * channels)) : 0;
        double rmsDbfs = rms > 0 ? 20 * Math.Log10(rms / 32768.0) : -999;
        double peakDbfs = peak > 0 ? 20 * Math.Log10(peak / 32768.0) : -999;

        string result = string.Format(
            "device={0} name={1} format={2}ch/{3}Hz/{4}bit frames={5} rms={6:F1} ({7:F1} dBFS) peak={8} ({9:F1} dBFS) silentPackets={10} activePackets={11} activePeakMin={12} activePeakMax={13}",
            deviceId, friendlyName, channels, sampleRate, format.wBitsPerSample, totalFrames,
            rms, rmsDbfs, peak, peakDbfs, silentBuffers, nonSilentBuffers,
            minPeak == int.MaxValue ? 0 : minPeak, maxPeak);

        if (pcm != null)
            WriteWav(outputWav, pcm.ToArray(), channels, sampleRate, bytesPerSample);

        return result;
    }

    private static IntPtr AllocHGlobal(byte[] bytes)
    {
        IntPtr pointer = Marshal.AllocHGlobal(bytes.Length);
        Marshal.Copy(bytes, 0, pointer, bytes.Length);
        return pointer;
    }

    private static void WriteWav(string path, byte[] data, int channels, int sampleRate, int bytesPerSample)
    {
        using (System.IO.FileStream stream = new System.IO.FileStream(path, System.IO.FileMode.Create))
        using (System.IO.BinaryWriter writer = new System.IO.BinaryWriter(stream))
        {
            int dataLength = data.Length;
            writer.Write(System.Text.Encoding.ASCII.GetBytes("RIFF"));
            writer.Write(36 + dataLength);
            writer.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt "));
            writer.Write(16);
            writer.Write((short)1);
            writer.Write((short)channels);
            writer.Write(sampleRate);
            writer.Write(sampleRate * channels * bytesPerSample);
            writer.Write((short)(channels * bytesPerSample));
            writer.Write((short)(bytesPerSample * 8));
            writer.Write(System.Text.Encoding.ASCII.GetBytes("data"));
            writer.Write(dataLength);
            writer.Write(data);
        }
    }

    [DllImport("kernel32.dll")]
    private static extern IntPtr CreateEvent(IntPtr lpEventAttributes, bool bManualReset, bool bInitialState, string lpName);

    [DllImport("kernel32.dll")]
    private static extern int WaitForSingleObject(IntPtr hHandle, int dwMilliseconds);

    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr hHandle);
}
'@

[WasapiCapture]::Capture($DeviceId, $Seconds, $OutputWav)
