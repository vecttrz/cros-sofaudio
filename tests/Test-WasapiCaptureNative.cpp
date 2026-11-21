// Test de captura WASAPI: mide RMS real de un endpoint de captura.
// Compilar:
//   cl /nologo /W4 /EHsc /DUNICODE /D_UNICODE Test-WasapiCaptureNative.cpp /link ole32.lib winmm.lib
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

static const GUID IEEE_FLOAT_GUID = {
    0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }
};

static const PROPERTYKEY PKEY_Device_FriendlyNameLocal = {
    { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } },
    14
};
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")

static void PrintHr(const wchar_t *label, HRESULT hr)
{
    if (FAILED(hr))
        wprintf(L"%ls falló: 0x%08lx\n", label, hr);
}

static bool ReadFriendlyName(IMMDevice *device, std::wstring &name)
{
    IPropertyStore *store = nullptr;
    HRESULT hr = device->OpenPropertyStore(STGM_READ, &store);
    if (FAILED(hr))
        return false;

    PROPVARIANT value;
    PropVariantInit(&value);
    hr = store->GetValue(PKEY_Device_FriendlyNameLocal, &value);
    if (SUCCEEDED(hr) && value.vt == VT_LPWSTR && value.pwszVal)
        name = value.pwszVal;
    PropVariantClear(&value);
    store->Release();
    return true;
}

static void WriteWav(const wchar_t *path, const std::vector<BYTE> &data,
                     WORD channels, DWORD sampleRate, WORD bytesPerSample)
{
    FILE *file = nullptr;
    _wfopen_s(&file, path, L"wb");
    if (!file)
        return;

    DWORD dataSize = static_cast<DWORD>(data.size());
    const char riff[] = { 'R', 'I', 'F', 'F' };
    const char wave[] = { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' };
    const char dataTag[] = { 'd', 'a', 't', 'a' };
    DWORD riffSize = 36 + dataSize;
    DWORD fmtSize = 16;
    WORD formatTag = 1;
    WORD blockAlign = static_cast<WORD>(channels * bytesPerSample);
    WORD bitsPerSample = static_cast<WORD>(bytesPerSample * 8);
    DWORD byteRate = sampleRate * blockAlign;

    fwrite(riff, 1, 4, file);
    fwrite(&riffSize, 4, 1, file);
    fwrite(wave, 1, 8, file);
    fwrite(&fmtSize, 4, 1, file);
    fwrite(&formatTag, 2, 1, file);
    fwrite(&channels, 2, 1, file);
    fwrite(&sampleRate, 4, 1, file);
    fwrite(&byteRate, 4, 1, file);
    fwrite(&blockAlign, 2, 1, file);
    fwrite(&bitsPerSample, 2, 1, file);
    fwrite(dataTag, 1, 4, file);
    fwrite(&dataSize, 4, 1, file);
    if (!data.empty())
        fwrite(data.data(), 1, data.size(), file);
    fclose(file);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 3)
    {
        wprintf(L"Uso: Test-WasapiCaptureNative <DeviceId> <segundos> [salida.wav]\n");
        return 2;
    }

    const wchar_t *deviceId = argv[1];
    int seconds = _wtoi(argv[2]);
    if (seconds < 1 || seconds > 60)
        seconds = 5;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    PrintHr(L"CoInitializeEx", hr);

    IMMDeviceEnumerator *enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          IID_PPV_ARGS(&enumerator));
    if (FAILED(hr))
    {
        PrintHr(L"CoCreateInstance", hr);
        CoUninitialize();
        return 1;
    }

    IMMDevice *device = nullptr;
    hr = enumerator->GetDevice(deviceId, &device);
    if (FAILED(hr))
    {
        PrintHr(L"GetDevice", hr);
        enumerator->Release();
        CoUninitialize();
        return 1;
    }

    std::wstring friendlyName;
    ReadFriendlyName(device, friendlyName);

    IAudioClient *client = nullptr;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void **>(&client));
    if (FAILED(hr))
    {
        PrintHr(L"Activate", hr);
        device->Release();
        enumerator->Release();
        CoUninitialize();
        return 1;
    }

    WAVEFORMATEX *mixFormat = nullptr;
    hr = client->GetMixFormat(&mixFormat);
    if (FAILED(hr) || !mixFormat)
    {
        PrintHr(L"GetMixFormat", hr);
        client->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();
        return 1;
    }

    wprintf(L"dispositivo=%ls\n", deviceId);
    wprintf(L"nombre=%ls\n", friendlyName.c_str());
    bool isFloat = false;
    if (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mixFormat->cbSize >= 22)
    {
        BYTE *bytes = reinterpret_cast<BYTE *>(mixFormat);
        GUID subFormat;
        memcpy(&subFormat, bytes + 24, sizeof(GUID));
        isFloat = (memcmp(&subFormat, &IEEE_FLOAT_GUID, sizeof(GUID)) == 0);
    }
    else if (mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        isFloat = true;

    wprintf(L"formato=%u canales, %lu Hz, %u bits, tag=%u, float=%d\n",
            mixFormat->nChannels, mixFormat->nSamplesPerSec,
            mixFormat->wBitsPerSample, mixFormat->wFormatTag, isFloat ? 1 : 0);

    REFERENCE_TIME bufferDuration = seconds * 10000000LL;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                AUDCLNT_STREAMFLAGS_NOPERSIST,
                            bufferDuration, 0, mixFormat, nullptr);
    if (FAILED(hr))
    {
        PrintHr(L"Initialize", hr);
        CoTaskMemFree(mixFormat);
        client->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();
        return 1;
    }

    UINT32 bufferFrames = 0;
    client->GetBufferSize(&bufferFrames);

    IAudioCaptureClient *capture = nullptr;
    hr = client->GetService(IID_PPV_ARGS(&capture));
    if (FAILED(hr))
    {
        PrintHr(L"GetService", hr);
        client->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();
        return 1;
    }

    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    hr = client->SetEventHandle(event);
    PrintHr(L"SetEventHandle", hr);

    WORD channels = mixFormat->nChannels;
    DWORD sampleRate = mixFormat->nSamplesPerSec;
    WORD bytesPerSample = static_cast<WORD>(mixFormat->wBitsPerSample / 8);
    WORD blockAlign = mixFormat->nBlockAlign;
    std::vector<BYTE> wavData;
    bool saveWav = argc >= 4;
    if (saveWav)
        wavData.reserve(static_cast<size_t>(sampleRate) * channels * bytesPerSample * seconds);

    long long totalFrames = 0;
    double sumSquare = 0.0;
    long long peak = 0;
    double floatMin = 0.0;
    double floatMax = 0.0;
    int outOfRangeFloats = 0;
    int silentPackets = 0;
    int activePackets = 0;
    long long activePeakMin = LLONG_MAX;
    long long activePeakMax = 0;

    hr = client->Start();
    if (FAILED(hr))
    {
        PrintHr(L"Start", hr);
        CloseHandle(event);
        capture->Release();
        client->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();
        return 1;
    }

    ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds + 2) * 1000;
    while (GetTickCount64() < deadline &&
           totalFrames < static_cast<long long>(sampleRate) * seconds)
    {
        if (WaitForSingleObject(event, 500) != WAIT_OBJECT_0)
            continue;

        UINT32 packetSize = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packetSize)) && packetSize > 0)
        {
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 devicePosition = 0;
            UINT64 qpcPosition = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, &devicePosition, &qpcPosition);
            if (FAILED(hr))
                break;

            size_t byteCount = static_cast<size_t>(frames) * blockAlign;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data && byteCount > 0)
            {
                long long packetSum = 0;
                long long packetPeak = 0;
                for (size_t offset = 0; offset + bytesPerSample <= byteCount;
                     offset += bytesPerSample)
                {
                    long long sample = 0;
                    if (isFloat && bytesPerSample == 4)
                    {
                        float value = *reinterpret_cast<const float *>(data + offset);
                        if (value < floatMin) floatMin = value;
                        if (value > floatMax) floatMax = value;
                        if (value < -1.0f || value > 1.0f) outOfRangeFloats++;
                        sample = static_cast<long long>(value * 32768.0f);
                    }
                    else if (bytesPerSample == 2)
                        sample = *reinterpret_cast<const short *>(data + offset);
                    else if (bytesPerSample == 4)
                        sample = *reinterpret_cast<const int *>(data + offset) >> 8;
                    else if (bytesPerSample == 1)
                        sample = static_cast<signed char>(data[offset]) << 8;
                    long long absolute = llabs(sample);
                    packetSum += absolute * absolute;
                    if (absolute > packetPeak)
                        packetPeak = absolute;
                }
                sumSquare += static_cast<double>(packetSum);
                if (packetPeak > peak)
                    peak = packetPeak;
                if (packetPeak == 0)
                    silentPackets++;
                else
                {
                    activePackets++;
                    if (packetPeak < activePeakMin)
                        activePeakMin = packetPeak;
                    if (packetPeak > activePeakMax)
                        activePeakMax = packetPeak;
                }

                if (saveWav)
                    wavData.insert(wavData.end(), data, data + byteCount);
                totalFrames += frames;
            }
            capture->ReleaseBuffer(frames);
        }
    }

    client->Stop();
    CloseHandle(event);
    capture->Release();

    double samples = static_cast<double>(totalFrames) * channels;
    double rms = samples > 0 ? std::sqrt(sumSquare / samples) : 0.0;
    double rmsDbfs = rms > 0 ? 20.0 * std::log10(rms / 32768.0) : -999.0;
    double peakDbfs = peak > 0 ? 20.0 * std::log10(static_cast<double>(peak) / 32768.0) : -999.0;

    wprintf(L"resultado: tramas=%lld rms=%.1f (%.1f dBFS) pico=%lld (%.1f dBFS) "
            L"paquetesSilenciosos=%d paquetesActivos=%d picoActivoMin=%lld picoActivoMax=%lld\n",
            totalFrames, rms, rmsDbfs, peak, peakDbfs, silentPackets, activePackets,
            activePeakMin == LLONG_MAX ? 0 : activePeakMin, activePeakMax);
    if (isFloat)
        wprintf(L"float: min=%.4f max=%.4f fueraDeRango=%d\n",
                floatMin, floatMax, outOfRangeFloats);

    if (saveWav)
    {
        WriteWav(argv[3], wavData, channels, sampleRate, bytesPerSample);
        wprintf(L"wav=%ls (%u bytes)\n", argv[3], static_cast<unsigned>(wavData.size()));
    }

    CoTaskMemFree(mixFormat);
    client->Release();
    device->Release();
    enumerator->Release();
    CoUninitialize();
    return 0;
}
