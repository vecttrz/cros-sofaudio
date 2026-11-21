#define UNICODE
#define _UNICODE
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <stdio.h>

static const CLSID CLSID_MMDeviceEnumeratorLocal = __uuidof(MMDeviceEnumerator);
static const IID IID_IMMDeviceEnumeratorLocal = __uuidof(IMMDeviceEnumerator);
static const IID IID_IAudioEndpointVolumeLocal = __uuidof(IAudioEndpointVolume);

static void SetRole(IMMDeviceEnumerator *enumerator, EDataFlow flow, ERole role,
                    const wchar_t *label, float scalar, BOOL doSet)
{
    IMMDevice *device = nullptr;
    HRESULT hr = enumerator->GetDefaultAudioEndpoint(flow, role, &device);
    if (FAILED(hr)) {
        wprintf(L"%ls: no default, hr=0x%08lx\n", label, hr);
        return;
    }

    IAudioEndpointVolume *volume = nullptr;
    hr = device->Activate(IID_IAudioEndpointVolumeLocal, CLSCTX_ALL, nullptr,
                          reinterpret_cast<void **>(&volume));
    if (FAILED(hr)) {
        wprintf(L"%ls: no IAudioEndpointVolume, hr=0x%08lx\n", label, hr);
        device->Release();
        return;
    }

    float before = 0.0f;
    float dbBefore = 0.0f;
    volume->GetMasterVolumeLevelScalar(&before);
    volume->GetMasterVolumeLevel(&dbBefore);

    if (doSet) {
        volume->SetMasterVolumeLevelScalar(scalar, nullptr);
    }

    float after = 0.0f;
    float dbAfter = 0.0f;
    volume->GetMasterVolumeLevelScalar(&after);
    volume->GetMasterVolumeLevel(&dbAfter);
    wprintf(L"%ls scalar %.4f -> %.4f (dB %.2f -> %.2f)\n",
            label, before, after, dbBefore, dbAfter);

    volume->Release();
    device->Release();
}

int wmain(int argc, wchar_t **argv)
{
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) return 1;

    IMMDeviceEnumerator *enumerator = nullptr;
    hr = CoCreateInstance(CLSID_MMDeviceEnumeratorLocal, nullptr, CLSCTX_ALL,
                          IID_IMMDeviceEnumeratorLocal,
                          reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr)) {
        CoUninitialize();
        return 1;
    }

    BOOL doSet = FALSE;
    float scalar = 1.0f;
    if (argc >= 2 && _wcsicmp(argv[1], L"set") == 0 && argc >= 3) {
        scalar = (float)_wtof(argv[2]);
        if (scalar < 0.0f || scalar > 1.0f) {
            wprintf(L"scalar must be between 0 and 1\n");
            enumerator->Release();
            CoUninitialize();
            return 2;
        }
        doSet = TRUE;
    }

    SetRole(enumerator, eCapture, eMultimedia, L"CAPTURE/Multimedia", scalar, doSet);
    SetRole(enumerator, eCapture, eCommunications, L"CAPTURE/Communications", scalar, doSet);

    enumerator->Release();
    CoUninitialize();
    return 0;
}
