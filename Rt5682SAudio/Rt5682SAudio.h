/*
 * Rt5682SAudio.h — Realtek RTL5682S KMDF Audio Codec & Jack Detection Header
 *
 * Implements driver context, jack state machine, and declarations for Windows 11.
 *
 * Copyright (c) 2026. Licensed under the MIT License.
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <spb.h>
#include <ntstrsafe.h>
#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>

#include "..\Common\csaudio_codec.h"
#include "..\Common\i2c_helper.h"
#include "rt5682s_reg.h"
#include "rt5682s_seq.h"

/* Ensure debug prints always emit to kernel debug output in both Release and Debug */
#ifdef KdPrintEx
#undef KdPrintEx
#endif
#define KdPrintEx(args) DbgPrintEx args

#define RT5682S_POOL_TAG '5288'

typedef struct _RT5682S_DEVICE_CONTEXT {
    /* KMDF Handles */
    WDFDEVICE               Device;
    WDFIOTARGET             SpbTarget;
    WDFINTERRUPT            Interrupt;
    WDFWORKITEM             JackWorkItem;
    WDFSPINLOCK             Lock;
    WDFQUEUE                DefaultQueue;
    WDFQUEUE                JackNotificationQueue; /* Pending IRPs awaiting jack events */

    /* Hardware Resources */
    LARGE_INTEGER           I2cConnectionId;
    ULONG                   GpioInterruptVector;

    /* Runtime State */
    BOOLEAN                 PowerActive;      /* TRUE when in D0 */
    BOOLEAN                 Streaming;        /* TRUE when headphone DAC streaming */
    BOOLEAN                 CaptureActive;    /* TRUE when mic ADC streaming */
    BOOLEAN                 Muted;            /* Headphone mute state */
    UINT8                   VolumeStep;       /* 0 (0dB) to 0x36 (-54dB) */
    UINT32                  SampleRate;       /* Default 48000 */

    /* Jack Detection State */
    CSAUDIO_JACK_STATE      CurrentJackState;
    BOOLEAN                 JackInserted;
    BOOLEAN                 MicPresent;       /* 4-pole TRRS detected */

    /* Jack polling (I2C_DEVICE_ID.GPIO2JD1 every NAU_JACK_POLL_MS) */
    WDFTIMER                JackTimer;
    WDFWAITLOCK             JackLock;
    BOOLEAN                 JackKnown;        /* first reading done */
    volatile BOOLEAN        JackPollStop;     /* set in D0Exit before stopping the timer */
    BOOLEAN                 JackActiveLow;    /* Config\JackActiveLow (default 1 on volmar) */
    UINT8                   JackCandidate;    /* debounce: 0/1 = pending new state, 0xFF = none */
    ULONG                   JackChanges;

    /* Chip identification (RTL5682S vendor/device IDs) */
    UINT16                  VendorIdReg;
    UINT16                  DeviceIdReg;
    UINT8                   SwId;       /* legacy telemetry field, always 0 */
    BOOLEAN                 HwInitDone;

} RT5682S_DEVICE_CONTEXT, *PRT5682S_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RT5682S_DEVICE_CONTEXT, GetRt5682SContext)

/* Driver lifecycle routines */
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD Rt5682S_EvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE Rt5682S_EvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE Rt5682S_EvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY Rt5682S_EvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT Rt5682S_EvtD0Exit;

EVT_WDF_TIMER Rt5682S_EvtJackTimer;

/* Interrupt & WorkItem Callbacks */
EVT_WDF_INTERRUPT_ISR Rt5682S_EvtInterruptIsr;
EVT_WDF_INTERRUPT_WORKITEM Rt5682S_EvtInterruptWorkItem;

/* Queue & IOCTL handlers */
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL Rt5682S_EvtIoDeviceControl;

/* Internal hardware helpers */
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Rt5682S_InitializeHardware(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Rt5682S_ConfigureClocks(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Rt5682S_EnableJackDetection(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Rt5682S_SetPlaybackState(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx,
    _In_ BOOLEAN Enable
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Rt5682S_SetCaptureState(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx,
    _In_ BOOLEAN Enable
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Rt5682S_SetVolume(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx,
    _In_ UINT8 VolumeStep,
    _In_ BOOLEAN Mute
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
Rt5682S_ProcessJackStateChange(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx
    );
