/*
 * Rt1019Audio.h — Realtek RTL1019P GPIO speaker amplifier driver
 *
 * The Google Marasov / ASUS CX34 exposes ACPI\RTL1019, which is the
 * GPIO-only RT1019P variant (Linux snd_soc_rt1015p), not the I2C RT1019.
 * Its only control line is the SDB (shutdown) GPIO: high = amplifier enabled,
 * low = shutdown. There are no I2C registers on this board.
 *
 * Copyright (c) 2026. Licensed under the MIT License.
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>
#include <initguid.h>
#include <devpkey.h>
#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>
#include <gpio.h>

#include "..\Common\csaudio_codec.h"

/* Ensure debug prints always emit to kernel debug output in both Release and Debug */
#ifdef KdPrintEx
#undef KdPrintEx
#endif
#define KdPrintEx(args) DbgPrintEx args

#define RT1019_POOL_TAG 'P919'

typedef enum _SPEAKER_CHANNEL_ROLE {
    SPEAKER_ROLE_MONO       = 0,
    SPEAKER_ROLE_LEFT       = 1,
    SPEAKER_ROLE_RIGHT      = 2,
    SPEAKER_ROLE_STEREO     = 3,
} SPEAKER_CHANNEL_ROLE;

typedef struct _RT1019_DEVICE_CONTEXT {
    /* KMDF handles */
    WDFDEVICE               Device;
    WDFIOTARGET             GpioTarget;
    LARGE_INTEGER           GpioConnectionId;
    WDFWAITLOCK             StateLock;
    WDFQUEUE                DefaultQueue;

    /* Hardware identification / state */
    SPEAKER_CHANNEL_ROLE    SpeakerRole;
    INT32                   UID;

    BOOLEAN                 PowerActive;       /* TRUE while the device is in D0 */
    BOOLEAN                 Streaming;         /* TRUE while an audio stream is active */
    BOOLEAN                 Muted;
    BOOLEAN                 CalibrationDone;   /* first SDB assertion needs 300 ms */
    ULONG                   StartCount;

    /* CSAudio sidecar communication */
    PCALLBACK_OBJECT        CSAudioAPICallback;
    PVOID                   CSAudioAPICallbackObj;
    BOOLEAN                 CSAudioRequestsOn;

    /* HKLM\SOFTWARE\CrosSofAudio\Amp */
    WCHAR                   LogKey[96];

} RT1019_DEVICE_CONTEXT, *PRT1019_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RT1019_DEVICE_CONTEXT, GetRt1019Context)

/* Driver lifecycle routines */
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD Rt1019_EvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE Rt1019_EvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE Rt1019_EvtReleaseHardware;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT Rt1019_EvtSelfManagedIoInit;
EVT_WDF_DEVICE_D0_ENTRY Rt1019_EvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT Rt1019_EvtD0Exit;

/* Queue & IOCTL handlers */
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL Rt1019_EvtIoDeviceControl;

/* Internal hardware helpers */
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Rt1019_InitializeHardware(
    _In_ PRT1019_DEVICE_CONTEXT DevCtx
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Rt1019_SetPowerState(
    _In_ PRT1019_DEVICE_CONTEXT DevCtx,
    _In_ BOOLEAN PowerOn
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Rt1019_SetStreamState(
    _In_ PRT1019_DEVICE_CONTEXT DevCtx,
    _In_ BOOLEAN Start
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Rt1019_SetVolume(
    _In_ PRT1019_DEVICE_CONTEXT DevCtx,
    _In_ UINT8 VolumeDb,
    _In_ BOOLEAN Mute
    );

VOID
Rt1019_CsAudioCallbackFunction(
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2
    );
