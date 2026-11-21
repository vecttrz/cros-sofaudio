/* Test-RegIo.cpp - Minimal RTL5682S I2C register reader/writer for diagnosis.
 *
 * Uso:
 *   Test-RegIo.exe read 0x210
 *   Test-RegIo.exe write 0x210 0xA1B7
 */
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <stdio.h>
#include <stdlib.h>

#include "..\Common\csaudio_codec.h"

#pragma comment(lib, "setupapi.lib")

static HANDLE OpenRt5682S(void)
{
    HDEVINFO devInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_RT5682S_CODEC,
                                           NULL, NULL,
                                           DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    SP_DEVICE_INTERFACE_DATA ifData;
    ifData.cbSize = sizeof(ifData);
    HANDLE hDevice = INVALID_HANDLE_VALUE;

    if (SetupDiEnumDeviceInterfaces(devInfo, NULL, &GUID_DEVINTERFACE_RT5682S_CODEC,
                                    0, &ifData)) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetail(devInfo, &ifData, NULL, 0, &reqSize, NULL);
        PSP_DEVICE_INTERFACE_DETAIL_DATA detail =
            (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(reqSize);
        if (detail) {
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            if (SetupDiGetDeviceInterfaceDetail(devInfo, &ifData, detail,
                                                reqSize, NULL, NULL)) {
                hDevice = CreateFile(detail->DevicePath,
                                     GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                     NULL);
            }
            free(detail);
        }
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return hDevice;
}

int __cdecl main(int argc, char** argv)
{
    if (argc < 3) {
        printf("Uso: Test-RegIo.exe read 0xREG | write 0xREG 0xVAL\n");
        return 2;
    }

    HANDLE h = OpenRt5682S();
    if (h == INVALID_HANDLE_VALUE) {
        printf("No se pudo abrir Rt5682SAudio (error %lu)\n", GetLastError());
        return 1;
    }

    CSAUDIO_REG_IO io;
    memset(&io, 0, sizeof(io));
    io.Register = (UINT16)strtoul(argv[2], NULL, 0);
    DWORD bytes = 0;
    BOOL ok;

    if (_stricmp(argv[1], "read") == 0) {
        ok = DeviceIoControl(h, IOCTL_CSAUDIO_READ_REG, &io, sizeof(io),
                             &io, sizeof(io), &bytes, NULL);
        if (!ok) {
            printf("read 0x%04X fallo: %lu\n", io.Register, GetLastError());
        } else {
            printf("read 0x%04X = 0x%04X\n", io.Register, io.Value);
        }
    } else if (_stricmp(argv[1], "write") == 0 && argc >= 4) {
        io.Value = (UINT16)strtoul(argv[3], NULL, 0);
        ok = DeviceIoControl(h, IOCTL_CSAUDIO_WRITE_REG, &io, sizeof(io),
                             &io, sizeof(io), &bytes, NULL);
        if (!ok) {
            printf("write 0x%04X fallo: %lu\n", io.Register, GetLastError());
        } else {
            printf("write 0x%04X = 0x%04X\n", io.Register, io.Value);
        }
    } else {
        printf("Comando desconocido\n");
        CloseHandle(h);
        return 2;
    }

    CloseHandle(h);
    return ok ? 0 : 1;
}
