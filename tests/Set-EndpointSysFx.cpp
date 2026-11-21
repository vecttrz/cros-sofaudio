#define NOMINMAX
#define INITGUID
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <propkeydef.h>

DEFINE_PROPERTYKEY(PKEY_AudioEndpoint_Disable_SysFx_c, 0x1da5d803, 0xd492, 0x4edd,
                   0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e, 5);

static const GUID CLSID_PolicyConfigClient_c = {
    0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 }
};

struct DeviceShareMode;

MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfigX : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, BOOL, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX *, WAVEFORMATEX *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, BOOL, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, DeviceShareMode *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, DeviceShareMode *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, BOOL, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, BOOL, const PROPERTYKEY &, const PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, BOOL) = 0;
};

int wmain(int argc, wchar_t **argv)
{
    if (argc < 3) {
        wprintf(L"Usage: Set-EndpointSysFx <device-id> <0|1>\n");
        return 2;
    }
    unsigned long value = static_cast<unsigned long>(_wtoi(argv[2]));
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) { wprintf(L"CoInitializeEx 0x%08lx\n", hr); return 1; }

    IPolicyConfigX *cfg = nullptr;
    hr = CoCreateInstance(CLSID_PolicyConfigClient_c, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&cfg));
    if (FAILED(hr)) { wprintf(L"CoCreateInstance 0x%08lx\n", hr); CoUninitialize(); return 1; }

    PROPVARIANT var;
    ZeroMemory(&var, sizeof(var));
    var.vt = VT_UI4;
    var.uiVal = static_cast<ULONG>(value);

    hr = cfg->SetPropertyValue(argv[1], TRUE, PKEY_AudioEndpoint_Disable_SysFx_c, &var);
    wprintf(L"SetPropertyValue(Disable_SysFx=%lu) = 0x%08lx\n", value, hr);

    PROPVARIANT read;
    ZeroMemory(&read, sizeof(read));
    hr = cfg->GetPropertyValue(argv[1], TRUE, PKEY_AudioEndpoint_Disable_SysFx_c, &read);
    wprintf(L"GetPropertyValue = 0x%08lx vt=%u value=%lu\n", hr, read.vt,
            read.vt == VT_UI4 ? read.uiVal : 0xffffffffu);
    PropVariantClear(&read);

    cfg->Release();
    CoUninitialize();
    return FAILED(hr) ? 1 : 0;
}
