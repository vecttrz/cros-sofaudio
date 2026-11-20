/*
 * Rt1019Audio.c — Realtek RTL1019P GPIO speaker amplifier KMDF driver
 *
 * Hardware: Google Marasov / ASUS Chromebook Plus CX34 (ACPI\RTL1019).
 *
 * The Marasov amplifier is the RT1019P variant, controlled exclusively by
 * its SDB (shutdown) GPIO: SDB high = amplifier enabled, SDB low = shutdown.
 * Its ACPI _CRS has a GpioIo resource on GPP_A11 but NO I2C/SPB resource.
 * This is the same device Linux drives with sound/soc/codecs/rt1015p.c.
 *
 * Consequently this driver does NOT use I2C at all. It:
 *   1. Obtains the GPIO connection ID from CmResourceTypeConnection
 *      (CM_RESOURCE_CONNECTION_CLASS_GPIO / TYPE_GPIO_IO) in PrepareHardware.
 *   2. Opens a WDFIOTARGET to \Device\GPIO via RESOURCE_HUB_CREATE_PATH_FROM_ID.
 *   3. Asserts SDB (IOCTL_GPIO_WRITE_PINS) only while audio is streaming.
 *   4. Waits 300 ms on the first assertion, as the RT1019P datasheet/Linux
 *      driver requires before the amplifier output is valid.
 *   5. Registers with the CSAudio SOF host via the callback and IOCTL bridge.
 *
 * Copyright (c) 2026. Licensed under the MIT License.
 */

#include "Rt1019Audio.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, Rt1019_EvtDeviceAdd)
#pragma alloc_text(PAGE, Rt1019_EvtPrepareHardware)
#pragma alloc_text(PAGE, Rt1019_EvtReleaseHardware)
#pragma alloc_text(PAGE, Rt1019_EvtSelfManagedIoInit)
#pragma alloc_text(PAGE, Rt1019_EvtD0Entry)
#pragma alloc_text(PAGE, Rt1019_EvtD0Exit)
#pragma alloc_text(PAGE, Rt1019_InitializeHardware)
#pragma alloc_text(PAGE, Rt1019_SetPowerState)
#pragma alloc_text(PAGE, Rt1019_SetStreamState)
#pragma alloc_text(PAGE, Rt1019_SetVolume)
#endif

static int CsAudioArg2 = 1;

static VOID AmpLog(_In_ PRT1019_DEVICE_CONTEXT DevCtx, _In_ PCWSTR Name, _In_ ULONG Value);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
Rt1019_WriteSdb(
    _In_ PRT1019_DEVICE_CONTEXT DevCtx,
    _In_ BOOLEAN High
    );

/* =========================================================================
 * DriverEntry
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt1019Audio: DriverEntry (RTL1019P GPIO-only)\n"));

    WDF_DRIVER_CONFIG_INIT(&config, Rt1019_EvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "Rt1019Audio: WdfDriverCreate failed 0x%08X\n", status));
    }

    return status;
}

/* =========================================================================
 * EvtDeviceAdd
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt1019_EvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDFDEVICE device;
    PRT1019_DEVICE_CONTEXT devCtx;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_IO_QUEUE_CONFIG queueConfig;

    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt1019Audio: EvtDeviceAdd\n"));

    /* Register PnP and Power callbacks */
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware      = Rt1019_EvtPrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware      = Rt1019_EvtReleaseHardware;
    pnpCallbacks.EvtDeviceSelfManagedIoInit    = Rt1019_EvtSelfManagedIoInit;
    pnpCallbacks.EvtDeviceD0Entry              = Rt1019_EvtD0Entry;
    pnpCallbacks.EvtDeviceD0Exit               = Rt1019_EvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    /* Allocate device context */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, RT1019_DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "Rt1019Audio: WdfDeviceCreate failed 0x%08X\n", status));
        return status;
    }

    devCtx = GetRt1019Context(device);
    RtlZeroMemory(devCtx, sizeof(RT1019_DEVICE_CONTEXT));
    devCtx->Device       = device;
    devCtx->SpeakerRole  = SPEAKER_ROLE_STEREO;  /* Marasov has one mono-capable amp */
    devCtx->UID          = 0;

    /* Wait lock for stream state (held across the 300 ms calibration delay) */
    {
        WDF_OBJECT_ATTRIBUTES lockAttr;
        WDF_OBJECT_ATTRIBUTES_INIT(&lockAttr);
        lockAttr.ParentObject = device;
        status = WdfWaitLockCreate(&lockAttr, &devCtx->StateLock);
    }
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "Rt1019Audio: WdfWaitLockCreate failed 0x%08X\n", status));
        return status;
    }

    /* Default parallel queue for IOCTLs */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = Rt1019_EvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &devCtx->DefaultQueue);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "Rt1019Audio: WdfIoQueueCreate failed 0x%08X\n", status));
        return status;
    }

    /* Expose device interfaces to the CSAudio SOF host */
    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_RT1019_AMP, NULL);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
            "Rt1019Audio: Failed to create RT1019 amp interface 0x%08X\n", status));
    }

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_CSAUDIO_CODEC, NULL);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
            "Rt1019Audio: Failed to create CSAudio codec interface 0x%08X\n", status));
    }

    return STATUS_SUCCESS;
}

/* =========================================================================
 * EvtPrepareHardware — Parse the GPIO I/O resource and open its target
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt1019_EvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PRT1019_DEVICE_CONTEXT devCtx;
    ULONG resourceCount;
    BOOLEAN foundGpio = FALSE;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    PAGED_CODE();

    devCtx = GetRt1019Context(Device);

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt1019Audio: EvtPrepareHardware (looking for GPIO)\n"));

    resourceCount = WdfCmResourceListGetCount(ResourcesTranslated);

    for (ULONG i = 0; i < resourceCount; i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc =
            WdfCmResourceListGetDescriptor(ResourcesTranslated, i);

        if (desc->Type == CmResourceTypeConnection &&
            desc->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_GPIO &&
            desc->u.Connection.Type  == CM_RESOURCE_CONNECTION_TYPE_GPIO_IO) {

            devCtx->GpioConnectionId.LowPart  = desc->u.Connection.IdLowPart;
            devCtx->GpioConnectionId.HighPart = desc->u.Connection.IdHighPart;
            foundGpio = TRUE;

            KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
                "Rt1019Audio: Found SDB GPIO connection ID (0x%08X:0x%08X)\n",
                devCtx->GpioConnectionId.HighPart,
                devCtx->GpioConnectionId.LowPart));
            break;
        }
    }

    if (!foundGpio) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "Rt1019Audio: No GPIO I/O resource found in ACPI _CRS!\n"
            "             RT1019P needs its SDB line; check the DSDT.\n"));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* Open the GPIO controller target by resource-hub path (Microsoft pattern) */
    {
        WCHAR gpioPathBuffer[RESOURCE_HUB_PATH_SIZE];
        UNICODE_STRING gpioPath;
        WDF_OBJECT_ATTRIBUTES targetAttributes;
        WDF_IO_TARGET_OPEN_PARAMS openParams;

        RtlInitEmptyUnicodeString(&gpioPath, gpioPathBuffer, sizeof(gpioPathBuffer));

        status = RESOURCE_HUB_CREATE_PATH_FROM_ID(
            &gpioPath,
            devCtx->GpioConnectionId.LowPart,
            devCtx->GpioConnectionId.HighPart
        );
        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "Rt1019Audio: RESOURCE_HUB_CREATE_PATH_FROM_ID failed 0x%08X\n", status));
            return status;
        }

        WDF_OBJECT_ATTRIBUTES_INIT(&targetAttributes);
        targetAttributes.ParentObject = Device;

        status = WdfIoTargetCreate(Device, &targetAttributes, &devCtx->GpioTarget);
        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "Rt1019Audio: WdfIoTargetCreate failed 0x%08X\n", status));
            return status;
        }

        WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(
            &openParams,
            &gpioPath,
            GENERIC_WRITE
        );
        openParams.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;

        status = WdfIoTargetOpen(devCtx->GpioTarget, &openParams);
        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "Rt1019Audio: WdfIoTargetOpen(GPIO) failed 0x%08X\n", status));
            WdfObjectDelete(devCtx->GpioTarget);
            devCtx->GpioTarget = NULL;
            return status;
        }
    }

    /* Identify the ACPI instance for logging */
    {
        WDF_DEVICE_PROPERTY_DATA propertyData;
        WDFMEMORY mem = NULL;
        DEVPROPTYPE propType;

        WDF_DEVICE_PROPERTY_DATA_INIT(&propertyData, &DEVPKEY_Device_InstanceId);
        if (NT_SUCCESS(WdfDeviceAllocAndQueryPropertyEx(
                Device, &propertyData, PagedPool, WDF_NO_OBJECT_ATTRIBUTES,
                &mem, &propType))) {
            PWSTR instanceId = (PWSTR)WdfMemoryGetBuffer(mem, NULL);
            if (instanceId) {
                if (wcsstr(instanceId, L"\\1")) {
                    devCtx->SpeakerRole = SPEAKER_ROLE_RIGHT;
                    devCtx->UID = 1;
                } else if (wcsstr(instanceId, L"\\0")) {
                    devCtx->SpeakerRole = SPEAKER_ROLE_LEFT;
                    devCtx->UID = 0;
                }
                KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
                    "Rt1019Audio: ACPI instance %ws (UID=%d, Role=%d)\n",
                    instanceId, devCtx->UID, devCtx->SpeakerRole));
            }
            WdfObjectDelete(mem);
        }
    }

    /* Registry log: HKLM\SOFTWARE\CrosSofAudio\Amp[0|1] */
    RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, L"\\Registry\\Machine\\SOFTWARE\\CrosSofAudio");
    RtlStringCchPrintfW(devCtx->LogKey, ARRAYSIZE(devCtx->LogKey),
        L"\\Registry\\Machine\\SOFTWARE\\CrosSofAudio\\Amp%d", devCtx->UID);
    RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, devCtx->LogKey);
    AmpLog(devCtx, L"Driver", 1);            /* 1 = RTL1019P GPIO variant */
    AmpLog(devCtx, L"Uid", (ULONG)devCtx->UID);
    AmpLog(devCtx, L"Role", (ULONG)devCtx->SpeakerRole);
    AmpLog(devCtx, L"GpioIdLow", (ULONG)devCtx->GpioConnectionId.LowPart);
    AmpLog(devCtx, L"GpioIdHigh", (ULONG)devCtx->GpioConnectionId.HighPart);

    return STATUS_SUCCESS;
}

/* =========================================================================
 * EvtReleaseHardware — Close the GPIO target
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt1019_EvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PRT1019_DEVICE_CONTEXT devCtx;

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    PAGED_CODE();

    devCtx = GetRt1019Context(Device);

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt1019Audio: EvtReleaseHardware\n"));

    if (devCtx->CSAudioAPICallbackObj) {
        ExUnregisterCallback(devCtx->CSAudioAPICallbackObj);
        devCtx->CSAudioAPICallbackObj = NULL;
    }
    if (devCtx->CSAudioAPICallback) {
        ObfDereferenceObject(devCtx->CSAudioAPICallback);
        devCtx->CSAudioAPICallback = NULL;
    }

    if (devCtx->GpioTarget != NULL) {
        WdfIoTargetClose(devCtx->GpioTarget);
        WdfObjectDelete(devCtx->GpioTarget);
        devCtx->GpioTarget = NULL;
    }

    devCtx->Streaming = FALSE;
    devCtx->PowerActive = FALSE;
    return STATUS_SUCCESS;
}

/* =========================================================================
 * EvtSelfManagedIoInit — register the CSAudioCallbackAPI listener
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt1019_EvtSelfManagedIoInit(
    _In_ WDFDEVICE Device
    )
{
    PRT1019_DEVICE_CONTEXT devCtx = GetRt1019Context(Device);
    NTSTATUS status;

    PAGED_CODE();

    UNICODE_STRING callbackName;
    RtlInitUnicodeString(&callbackName, L"\\CallBack\\CsAudioCallbackAPI");

    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(
        &attributes,
        &callbackName,
        OBJ_KERNEL_HANDLE | OBJ_OPENIF | OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
        NULL,
        NULL
    );

    status = ExCreateCallback(&devCtx->CSAudioAPICallback, &attributes, TRUE, TRUE);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "Rt1019Audio: ExCreateCallback failed 0x%08X\n", status));
        return status;
    }

    devCtx->CSAudioAPICallbackObj = ExRegisterCallback(
        devCtx->CSAudioAPICallback,
        (PCALLBACK_FUNCTION)Rt1019_CsAudioCallbackFunction,
        devCtx
    );

    if (!devCtx->CSAudioAPICallbackObj) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "Rt1019Audio: ExRegisterCallback failed\n"));
        return STATUS_NO_CALLBACK_ACTIVE;
    }

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt1019Audio: Registered CsAudioCallbackAPI listener\n"));
    return STATUS_SUCCESS;
}

/* =========================================================================
 * CsAudioCallbackFunction — SOF host stream notification
 * ========================================================================= */

VOID
Rt1019_CsAudioCallbackFunction(
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2
    )
{
    PRT1019_DEVICE_CONTEXT devCtx = (PRT1019_DEVICE_CONTEXT)CallbackContext;
    CsAudioArg* arg = (CsAudioArg*)Argument1;

    if (!devCtx || !arg) {
        return;
    }
    if (Argument2 == &CsAudioArg2) {
        return;
    }

    CsAudioArg localArg;
    RtlZeroMemory(&localArg, sizeof(CsAudioArg));
    RtlCopyMemory(&localArg, arg, min(arg->argSz, sizeof(CsAudioArg)));

    if (localArg.endpointType != CSAudioEndpointTypeSpeaker) {
        return;
    }

    if (localArg.endpointRequest == CSAudioEndpointStop) {
        if (devCtx->CSAudioRequestsOn) {
            devCtx->CSAudioRequestsOn = FALSE;
            Rt1019_SetStreamState(devCtx, FALSE);
        }
    } else if (localArg.endpointRequest == CSAudioEndpointStart) {
        if (!devCtx->CSAudioRequestsOn) {
            devCtx->CSAudioRequestsOn = TRUE;
            Rt1019_SetStreamState(devCtx, TRUE);
        }
    }
}

/* =========================================================================
 * Rt1019_WriteSdb — write the single SDB GPIO pin
 *
 * IOCTL_GPIO_WRITE_PINS takes a bitmap of output pins: one ULONG holds up to
 * 32 pins and bit 0 corresponds to the first pin in the GpioIo resource.
 * The Marasov GpioIo resource contains exactly one pin (GPP_A11).
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt1019_WriteSdb(
    _In_ PRT1019_DEVICE_CONTEXT DevCtx,
    _In_ BOOLEAN High
    )
{
    ULONG pinBits = High ? 1u : 0u;
    ULONG readBits = 0;
    WDF_MEMORY_DESCRIPTOR writeDesc;
    WDF_MEMORY_DESCRIPTOR readDesc;
    NTSTATUS status;
    NTSTATUS readStatus;

    PAGED_CODE();
    if (!DevCtx->GpioTarget) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&writeDesc, &pinBits, sizeof(pinBits));
    status = WdfIoTargetSendIoctlSynchronously(
        DevCtx->GpioTarget,
        NULL,
        IOCTL_GPIO_WRITE_PINS,
        &writeDesc,
        NULL,
        NULL,
        NULL
    );

    /*
     * Telemetry: READ_PINS returns the current level of the pin bitmap.  For
     * this one-pin GpioIo resource bit 0 is GPP_A11/SDB.  The read is
     * deliberately best-effort: a failure here must not change the write
     * result, but it tells us whether the GPIO controller accepted the write
     * and whether the line really went high on stream start.
     */
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&readDesc, &readBits, sizeof(readBits));
    readStatus = WdfIoTargetSendIoctlSynchronously(
        DevCtx->GpioTarget,
        NULL,
        IOCTL_GPIO_READ_PINS,
        NULL,
        &readDesc,
        NULL,
        NULL
    );
    AmpLog(DevCtx, L"SdbWriteValue", pinBits);
    AmpLog(DevCtx, L"SdbReadStatus", (ULONG)readStatus);
    AmpLog(DevCtx, L"SdbReadLevel", NT_SUCCESS(readStatus) ? (readBits & 1u) : 0xFFFFFFFFu);

    return status;
}

/* =========================================================================
 * D0 power transitions
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt1019_EvtD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PRT1019_DEVICE_CONTEXT devCtx;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(PreviousState);
    PAGED_CODE();

    devCtx = GetRt1019Context(Device);

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt1019Audio: EvtD0Entry\n"));

    devCtx->PowerActive = TRUE;
    devCtx->Streaming = FALSE;

    /* Start with the amplifier shut down; SDB is asserted on stream start. */
    status = Rt1019_WriteSdb(devCtx, FALSE);
    AmpLog(devCtx, L"D0EntrySdbLow", (ULONG)status);
    return status;
}

_Use_decl_annotations_
NTSTATUS
Rt1019_EvtD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PRT1019_DEVICE_CONTEXT devCtx;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(TargetState);
    PAGED_CODE();

    devCtx = GetRt1019Context(Device);

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt1019Audio: EvtD0Exit\n"));

    status = Rt1019_SetStreamState(devCtx, FALSE);
    devCtx->PowerActive = FALSE;
    return status;
}

/* =========================================================================
 * Logging helper — HKLM\SOFTWARE\CrosSofAudio\Amp[0|1]
 * ========================================================================= */

static VOID AmpLog(_In_ PRT1019_DEVICE_CONTEXT DevCtx, _In_ PCWSTR Name, _In_ ULONG Value)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || DevCtx->LogKey[0] == 0) return;
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, DevCtx->LogKey, Name, REG_DWORD, &Value, sizeof(Value));
}

static VOID AmpSleepMs(_In_ ULONG Ms)
{
    LARGE_INTEGER interval;
    interval.QuadPart = -(LONGLONG)Ms * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

/* =========================================================================
 * Rt1019_InitializeHardware — first SDB assertion / calibration
 *
 * Equivalent of Linux rt1015p_power_on(): assert SDB, then wait 300 ms the
 * first time so the RT1019P charge pump and output stage settle. There is no
 * register programming: all I2S/TDM format setup lives in the SOF topology.
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt1019_InitializeHardware(
    _In_ PRT1019_DEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS status;

    PAGED_CODE();
    if (!DevCtx->GpioTarget) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    status = Rt1019_WriteSdb(DevCtx, TRUE);
    AmpLog(DevCtx, L"SdbOnStatus", (ULONG)status);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    if (!DevCtx->CalibrationDone) {
        AmpLog(DevCtx, L"FirstOnDelayMs", 300);
        AmpSleepMs(300);
        DevCtx->CalibrationDone = TRUE;
        AmpLog(DevCtx, L"CalibrationDone", 1);
    }

    return STATUS_SUCCESS;
}

/* =========================================================================
 * Rt1019_SetPowerState — direct SDB control (used by mute)
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt1019_SetPowerState(
    _In_ PRT1019_DEVICE_CONTEXT DevCtx,
    _In_ BOOLEAN PowerOn
    )
{
    PAGED_CODE();
    return Rt1019_WriteSdb(DevCtx, PowerOn);
}

/* =========================================================================
 * Rt1019_SetStreamState —Speaker enable / disable on stream transitions
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt1019_SetStreamState(
    _In_ PRT1019_DEVICE_CONTEXT DevCtx,
    _In_ BOOLEAN Start
    )
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    if (!DevCtx->GpioTarget) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    WdfWaitLockAcquire(DevCtx->StateLock, NULL);

    if (Start) {
        if (!DevCtx->Streaming && !DevCtx->Muted) {
            DevCtx->StartCount++;
            AmpLog(DevCtx, L"StartCount", DevCtx->StartCount);
            status = Rt1019_InitializeHardware(DevCtx);
            AmpLog(DevCtx, L"StartStatus", (ULONG)status);
            if (NT_SUCCESS(status)) {
                DevCtx->Streaming = TRUE;
            } else {
                Rt1019_WriteSdb(DevCtx, FALSE);
            }
        }
    } else if (DevCtx->Streaming || DevCtx->Muted) {
        /* Linux rt1015p_power_off(): deassert SDB immediately. */
        status = Rt1019_WriteSdb(DevCtx, FALSE);
        AmpLog(DevCtx, L"StopStatus", (ULONG)status);
        DevCtx->Streaming = FALSE;
    }

    WdfWaitLockRelease(DevCtx->StateLock);
    return status;
}

/* =========================================================================
 * Rt1019_SetVolume — RT1019P has no digital volume register; mute = SDB low.
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt1019_SetVolume(
    _In_ PRT1019_DEVICE_CONTEXT DevCtx,
    _In_ UINT8 VolumeDb,
    _In_ BOOLEAN Mute
    )
{
    NTSTATUS status = STATUS_SUCCESS;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(VolumeDb);   /* Windows volume is applied in the DSP */

    WdfWaitLockAcquire(DevCtx->StateLock, NULL);
    DevCtx->Muted = Mute;
    if (DevCtx->Streaming) {
        status = Rt1019_WriteSdb(DevCtx, (BOOLEAN)(!Mute));
    }
    WdfWaitLockRelease(DevCtx->StateLock);

    AmpLog(DevCtx, L"Muted", (ULONG)Mute);
    return status;
}

/* =========================================================================
 * EvtIoDeviceControl — IOCTL dispatch for the CSAudio SOF host
 * ========================================================================= */

_Use_decl_annotations_
VOID
Rt1019_EvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PRT1019_DEVICE_CONTEXT devCtx = GetRt1019Context(device);
    NTSTATUS status = STATUS_SUCCESS;
    size_t bytesReturned = 0;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode) {

    case IOCTL_CSAUDIO_REGISTER_CODEC: {
        PCSAUDIO_REGISTER_PARAMS params;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CSAUDIO_REGISTER_PARAMS), (PVOID*)&params, NULL);
        if (NT_SUCCESS(status)) {
            params->CodecType = CSAUDIO_CODEC_TYPE_SPEAKER;
            params->SupportedEndpoints = CSAUDIO_ENDPOINT_SPEAKER;
            RtlStringCbCopyA(params->DeviceName, sizeof(params->DeviceName),
                             "Realtek RT1019P GPIO Speaker Amplifier");
            bytesReturned = sizeof(CSAUDIO_REGISTER_PARAMS);
        }
        break;
    }

    case IOCTL_CSAUDIO_STREAM_START: {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
            "Rt1019Audio: IOCTL_CSAUDIO_STREAM_START\n"));
        devCtx->CSAudioRequestsOn = TRUE;
        status = Rt1019_SetStreamState(devCtx, TRUE);
        break;
    }

    case IOCTL_CSAUDIO_STREAM_STOP: {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
            "Rt1019Audio: IOCTL_CSAUDIO_STREAM_STOP\n"));
        devCtx->CSAudioRequestsOn = FALSE;
        status = Rt1019_SetStreamState(devCtx, FALSE);
        break;
    }

    case IOCTL_CSAUDIO_SET_VOLUME: {
        PCSAUDIO_VOLUME_PARAMS volParams;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CSAUDIO_VOLUME_PARAMS), (PVOID*)&volParams, NULL);
        if (NT_SUCCESS(status)) {
            status = Rt1019_SetVolume(devCtx, volParams->VolumeDbLevel, volParams->Muted);
        }
        break;
    }

    case IOCTL_CSAUDIO_SET_MUTE: {
        PCSAUDIO_VOLUME_PARAMS volParams;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CSAUDIO_VOLUME_PARAMS), (PVOID*)&volParams, NULL);
        if (NT_SUCCESS(status)) {
            status = Rt1019_SetVolume(devCtx, 0, TRUE);
        } else {
            status = Rt1019_SetVolume(devCtx, 0, TRUE);
        }
        break;
    }

    case IOCTL_CSAUDIO_READ_REG:
    case IOCTL_CSAUDIO_WRITE_REG:
        /* RT1019P has no I2C register map. Keep the IOCTLs defined for
         * compatibility but report them as unsupported instead of touching
         * a non-existent SPB target. */
        status = STATUS_NOT_SUPPORTED;
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
