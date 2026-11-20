/*
 * cros_audio_test.c — Chromebook Audio Driver Diagnostic & Test Utility
 *
 * Verifies RT1019 and RTL5682S Windows 11 drivers:
 *   - Device interface enumeration
 *   - RTL5682S hardware register dumps over I2C (RT1019P is GPIO-only)
 *   - Headphone jack insertion / ejection monitoring in real-time
 *   - Playback stream start/stop and volume ramping tests
 *
 * Copyright (c) 2026. Licensed under the MIT License.
 */

#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <stdio.h>
#include <conio.h>

#include "..\Common\csaudio_codec.h"
#include "..\Rt5682SAudio\rt5682s_reg.h"

#pragma comment(lib, "setupapi.lib")

static HANDLE OpenDeviceInterface(const GUID* InterfaceGuid) {
    HDEVINFO devInfo = SetupDiGetClassDevs(InterfaceGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    SP_DEVICE_INTERFACE_DATA ifData;
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    HANDLE hDevice = INVALID_HANDLE_VALUE;

    if (SetupDiEnumDeviceInterfaces(devInfo, NULL, InterfaceGuid, 0, &ifData)) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetail(devInfo, &ifData, NULL, 0, &reqSize, NULL);

        PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(reqSize);
        if (detailData) {
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            if (SetupDiGetDeviceInterfaceDetail(devInfo, &ifData, detailData, reqSize, NULL, NULL)) {
                wprintf(L"  Found device path: %s\n", detailData->DevicePath);
                hDevice = CreateFile(
                    detailData->DevicePath,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
            }
            free(detailData);
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return hDevice;
}

static DWORD WINAPI JackMonitorThread(LPVOID lpParam) {
    HANDLE hRt5682S = (HANDLE)lpParam;
    CSAUDIO_JACK_STATE jackState = CSAUDIO_JACK_UNPLUGGED;
    DWORD bytesReturned = 0;

    printf("[JACK MONITOR] Background jack event monitor started...\n");

    while (TRUE) {
        BOOL success = DeviceIoControl(
            hRt5682S,
            IOCTL_CSAUDIO_REGISTER_JACK_NOTIFICATION,
            NULL, 0,
            &jackState, sizeof(jackState),
            &bytesReturned,
            NULL
        );

        if (!success) {
            Sleep(1000);
            continue;
        }

        printf("\n>>> [JACK EVENT DETECTED] New Status: ");
        switch (jackState) {
        case CSAUDIO_JACK_UNPLUGGED:
            printf("UNPLUGGED (No audio jack inserted)\n");
            break;
        case CSAUDIO_JACK_HEADPHONES_3POLE:
            printf("CONNECTED: Standard 3-Pole (TRS) Stereo Headphones (No Mic)\n");
            break;
        case CSAUDIO_JACK_HEADSET_4POLE:
            printf("CONNECTED: TRRS 4-Pole Headset with Microphone (CTIA/OMTP)\n");
            break;
        default:
            printf("Flags = 0x%02X\n", jackState);
            break;
        }
        printf("Prompt> ");
        fflush(stdout);
    }

    return 0;
}

int main(int argc, char* argv[]) {
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    printf("===============================================================\n");
    printf(" Chromebook SOF Audio (RT1019 + RTL5682S) Windows 11 Tester \n");
    printf("===============================================================\n\n");

    /* Test 1: Realtek RT1019 Speaker Amplifier */
    printf("[1] Scanning for RT1019 Smart Amplifier...\n");
    HANDLE hRt1019 = OpenDeviceInterface(&GUID_DEVINTERFACE_RT1019_AMP);

    if (hRt1019 != INVALID_HANDLE_VALUE) {
        printf("  [OK] Successfully connected to Rt1019Audio driver!\n");

        CSAUDIO_REGISTER_PARAMS regParams = {0};
        DWORD bytes = 0;
        if (DeviceIoControl(hRt1019, IOCTL_CSAUDIO_REGISTER_CODEC, &regParams, sizeof(regParams), &regParams, sizeof(regParams), &bytes, NULL)) {
            printf("  Device Name: %s\n", regParams.DeviceName);
        }

        /* RT1019P is controlled by a single SDB GPIO and has no I2C register
         * map. Confirm that register IOCTLs are explicitly unsupported. */
        CSAUDIO_REG_IO regIo = {0};
        regIo.Register = 0;
        if (!DeviceIoControl(hRt1019, IOCTL_CSAUDIO_READ_REG,
                             &regIo, sizeof(regIo), &regIo, sizeof(regIo),
                             &bytes, NULL) &&
            GetLastError() == ERROR_NOT_SUPPORTED) {
            printf("  [OK] GPIO-only amp: register IOCTLs correctly report NOT_SUPPORTED.\n");
        } else {
            printf("  [!] Unexpected response to register IOCTL on GPIO-only amp.\n");
        }

        /* Exercise the GPIO path: start raises SDB, stop lowers it again. */
        if (DeviceIoControl(hRt1019, IOCTL_CSAUDIO_STREAM_START,
                            NULL, 0, NULL, 0, &bytes, NULL)) {
            printf("  [OK] IOCTL_CSAUDIO_STREAM_START accepted (SDB driven high).\n");
            if (DeviceIoControl(hRt1019, IOCTL_CSAUDIO_STREAM_STOP,
                                NULL, 0, NULL, 0, &bytes, NULL)) {
                printf("  [OK] IOCTL_CSAUDIO_STREAM_STOP accepted (SDB driven low).\n");
            }
        }
    } else {
        printf("  [!] RT1019 amplifier not detected or driver not installed.\n");
    }

    printf("\n");

    /* Test 2: Realtek RTL5682S Headset Codec & Jack Detection */
    printf("[2] Scanning for RTL5682S Headset Codec...\n");
    HANDLE hRt5682S = OpenDeviceInterface(&GUID_DEVINTERFACE_RT5682S_CODEC);

    if (hRt5682S != INVALID_HANDLE_VALUE) {
        printf("  [OK] Successfully connected to Rt5682SAudio driver!\n");

        CSAUDIO_REGISTER_PARAMS regParams = {0};
        DWORD bytes = 0;
        if (DeviceIoControl(hRt5682S, IOCTL_CSAUDIO_REGISTER_CODEC, &regParams, sizeof(regParams), &regParams, sizeof(regParams), &bytes, NULL)) {
            printf("  Device Name: %s\n", regParams.DeviceName);
        }

        /* Read Device ID (Reg 0x58) */
        CSAUDIO_REG_IO regIo = {0};
        regIo.Register = RT5682S_REG_I2C_DEVICE_ID;
        if (DeviceIoControl(hRt5682S, IOCTL_CSAUDIO_READ_REG, &regIo, sizeof(regIo), &regIo, sizeof(regIo), &bytes, NULL)) {
            printf("  Silicon Device ID (Reg 0x58): 0x%04X\n", regIo.Value);
        }

        /* Automatic CTIA/OMTP detector result: GENERAL_STATUS[11:10]. */
        regIo.Register = RT5682S_REG_GENERAL_STATUS;
        if (DeviceIoControl(hRt5682S, IOCTL_CSAUDIO_READ_REG, &regIo, sizeof(regIo), &regIo, sizeof(regIo), &bytes, NULL)) {
            UINT16 micCode = (regIo.Value & RT5682S_MIC_DETECT_MASK) >> RT5682S_MIC_DETECT_SHIFT;
            const char* micType = (micCode == 0) ? "No microphone" :
                                  (micCode == 1) ? "OMTP headset microphone" :
                                  (micCode == 2) ? "CTIA headset microphone" :
                                                   "High-impedance/ambiguous headset";
            printf("  Headset Detector (Reg 0x82): 0x%04X, code %u (%s)\n",
                   regIo.Value, micCode, micType);
        }

        /* Query current jack status */
        CSAUDIO_JACK_STATE state = CSAUDIO_JACK_UNPLUGGED;
        if (DeviceIoControl(hRt5682S, IOCTL_CSAUDIO_GET_JACK_STATE, NULL, 0, &state, sizeof(state), &bytes, NULL)) {
            printf("  Initial Jack State: %s\n",
                (state == CSAUDIO_JACK_UNPLUGGED) ? "Unplugged" :
                (state == CSAUDIO_JACK_HEADPHONES_3POLE) ? "TRS Headphones Plugged" :
                (state == CSAUDIO_JACK_HEADSET_4POLE) ? "TRRS Headset with Mic Plugged" : "Unknown");
        }

        /* Spawn Jack Event Monitor thread */
        HANDLE hThread = CreateThread(NULL, 0, JackMonitorThread, (LPVOID)hRt5682S, 0, NULL);
        if (hThread) CloseHandle(hThread);
    } else {
        printf("  [!] RTL5682S codec not detected or driver not installed.\n");
    }

    printf("\nInteractive Commands:\n");
    printf("  [1] Test RT1019 Amp Stream Start / Stop\n");
    printf("  [2] Test RTL5682S Headphone Stream Start / Stop\n");
    printf("  [3] Query Live Jack State\n");
    printf("  [q] Exit Test Tool\n\n");

    while (TRUE) {
        printf("Prompt> ");
        char ch = (char)_getch();
        printf("%c\n", ch);

        if (ch == 'q' || ch == 'Q') break;

        if (ch == '1' && hRt1019 != INVALID_HANDLE_VALUE) {
            DWORD bytes = 0;
            printf("Starting RT1019 speaker stream...\n");
            DeviceIoControl(hRt1019, IOCTL_CSAUDIO_STREAM_START, NULL, 0, NULL, 0, &bytes, NULL);
            Sleep(1500);
            printf("Stopping RT1019 speaker stream...\n");
            DeviceIoControl(hRt1019, IOCTL_CSAUDIO_STREAM_STOP, NULL, 0, NULL, 0, &bytes, NULL);
        }
        else if (ch == '2' && hRt5682S != INVALID_HANDLE_VALUE) {
            DWORD bytes = 0;
            printf("Starting RTL5682S headphone stream...\n");
            DeviceIoControl(hRt5682S, IOCTL_CSAUDIO_STREAM_START, NULL, 0, NULL, 0, &bytes, NULL);
            Sleep(1500);
            printf("Stopping RTL5682S headphone stream...\n");
            DeviceIoControl(hRt5682S, IOCTL_CSAUDIO_STREAM_STOP, NULL, 0, NULL, 0, &bytes, NULL);
        }
        else if (ch == '3' && hRt5682S != INVALID_HANDLE_VALUE) {
            CSAUDIO_JACK_STATE state = CSAUDIO_JACK_UNPLUGGED;
            DWORD bytes = 0;
            if (DeviceIoControl(hRt5682S, IOCTL_CSAUDIO_GET_JACK_STATE, NULL, 0, &state, sizeof(state), &bytes, NULL)) {
                printf("Live Jack State: %s (Raw: 0x%02X)\n",
                    (state == CSAUDIO_JACK_UNPLUGGED) ? "Unplugged" :
                    (state == CSAUDIO_JACK_HEADPHONES_3POLE) ? "TRS Headphones Plugged" :
                    (state == CSAUDIO_JACK_HEADSET_4POLE) ? "TRRS Headset with Mic Plugged" : "Unknown",
                    state);
            }
        }
    }

    if (hRt1019 != INVALID_HANDLE_VALUE) CloseHandle(hRt1019);
    if (hRt5682S != INVALID_HANDLE_VALUE) CloseHandle(hRt5682S);

    printf("Exiting tester.\n");
    return 0;
}
