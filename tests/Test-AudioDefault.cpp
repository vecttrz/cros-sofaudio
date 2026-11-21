#define UNICODE
#define _UNICODE
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <stdio.h>
#include <wchar.h>

static const CLSID CLSID_MMDeviceEnumeratorLocal = __uuidof(MMDeviceEnumerator);
static const IID IID_IMMDeviceEnumeratorLocal = __uuidof(IMMDeviceEnumerator);
static const IID IID_IAudioEndpointVolumeLocal = __uuidof(IAudioEndpointVolume);

static void PrintDevice(IMMDevice *device, const wchar_t *role)
{
    if (!device) return;

    LPWSTR id = nullptr;
    if (SUCCEEDED(device->GetId(&id))) {
        wprintf(L"%ls ID=%ls\n", role, id ? id : L"(null)");
        CoTaskMemFree(id);
    }

    IPropertyStore *store = nullptr;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store))) {
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &var)) && var.pwszVal) {
            wprintf(L"%ls Nombre=%ls\n", role, var.pwszVal);
        }
        PropVariantClear(&var);
        store->Release();
    }

    IAudioEndpointVolume *volume = nullptr;
    HRESULT hrVolume = device->Activate(IID_IAudioEndpointVolumeLocal, CLSCTX_ALL,
                                         nullptr, reinterpret_cast<void **>(&volume));
    if (FAILED(hrVolume)) {
        wprintf(L"%ls IAudioEndpointVolume QI fallo: 0x%08lx\n", role, hrVolume);
        return;
    }

    BOOL mute = FALSE;
    float scalar = 0.0f;
    float db = 0.0f;
    wprintf(L"%ls Mutehr=0x%08lx Scalarhr=0x%08lx Dbhr=0x%08lx\n", role,
            volume->GetMute(&mute),
            volume->GetMasterVolumeLevelScalar(&scalar),
            volume->GetMasterVolumeLevel(&db));
    wprintf(L"%ls Mute=%ls VolumenScalar=%.4f VolumenDb=%.2f\n", role,
            mute ? L"TRUE" : L"FALSE", scalar, db);
    volume->Release();
}

int wmain()
{
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
        wprintf(L"CoInitialize fallo: 0x%08lx\n", hr);
        return 1;
    }

    IMMDeviceEnumerator *enumerator = nullptr;
    hr = CoCreateInstance(CLSID_MMDeviceEnumeratorLocal, nullptr, CLSCTX_ALL,
                          IID_IMMDeviceEnumeratorLocal,
                          reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr)) {
        wprintf(L"CoCreateInstance fallo: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }

    for (int i = 0; i < 2; i++) {
        EDataFlow flow = (i == 0) ? eCapture : eRender;
        const wchar_t *flowName = (i == 0) ? L"CAPTURE" : L"RENDER";
        for (int roleIndex = 0; roleIndex < 2; roleIndex++) {
            ERole role = (roleIndex == 0) ? eMultimedia : eCommunications;
            const wchar_t *roleName = (roleIndex == 0) ? L"Multimedia" : L"Comunicaciones";
            IMMDevice *device = nullptr;
            hr = enumerator->GetDefaultAudioEndpoint(flow, role, &device);
            if (FAILED(hr)) {
                wprintf(L"%ls/%ls: sin default, hr=0x%08lx\n", flowName, roleName, hr);
                continue;
            }
            wchar_t label[128];
            swprintf(label, 128, L"%ls/%ls", flowName, roleName);
            PrintDevice(device, label);
            device->Release();
        }
    }

    enumerator->Release();
    CoUninitialize();
    return 0;
}
