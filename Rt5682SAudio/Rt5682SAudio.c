/*
 * Rt5682SAudio.c — Realtek RTL5682S KMDF Audio Codec & Jack Detection Driver
 *
 * Implements:
 *   1. DriverEntry and EvtDriverDeviceAdd
 *   2. ACPI SPB (I2C) and GPIO Interrupt resource parsing
 *   3. Hardware initialization, FLL clock synthesis, and headphone driver staging
 *   4. Hardware Jack Detection engine (TRS headphones, TRRS headset, button events)
 *   5. Pop-free volume ramping and power transitions (EvtDeviceD0Entry / EvtDeviceD0Exit)
 *   6. CSAudio bus / Sidecar IOCTL dispatch and asynchronous jack event notifications
 *
 * Copyright (c) 2026. Licensed under the MIT License.
 */

#include "Rt5682SAudio.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, Rt5682S_EvtDeviceAdd)
#pragma alloc_text(PAGE, Rt5682S_EvtPrepareHardware)
#pragma alloc_text(PAGE, Rt5682S_EvtReleaseHardware)
#pragma alloc_text(PAGE, Rt5682S_EvtD0Entry)
#pragma alloc_text(PAGE, Rt5682S_EvtD0Exit)
#pragma alloc_text(PAGE, Rt5682S_InitializeHardware)
#pragma alloc_text(PAGE, Rt5682S_ConfigureClocks)
#pragma alloc_text(PAGE, Rt5682S_EnableJackDetection)
#pragma alloc_text(PAGE, Rt5682S_SetPlaybackState)
#pragma alloc_text(PAGE, Rt5682S_SetCaptureState)
#pragma alloc_text(PAGE, Rt5682S_SetVolume)
#pragma alloc_text(PAGE, Rt5682S_ProcessJackStateChange)
#endif

static ULONG NauReadConfigDword(_In_ PCWSTR Name, _In_ ULONG Default);

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
        "Rt5682SAudio: DriverEntry initializing\n"));

    WDF_DRIVER_CONFIG_INIT(&config, Rt5682S_EvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "Rt5682SAudio: WdfDriverCreate failed 0x%08X\n", status));
    }

    return status;
}

/* =========================================================================
 * EvtDeviceAdd
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt5682S_EvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDFDEVICE device;
    PRT5682S_DEVICE_CONTEXT devCtx;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_IO_QUEUE_CONFIG queueConfig;

    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt5682SAudio: EvtDeviceAdd\n"));

    /* Register PnP and Power Callbacks */
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware = Rt5682S_EvtPrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware = Rt5682S_EvtReleaseHardware;
    pnpCallbacks.EvtDeviceD0Entry         = Rt5682S_EvtD0Entry;
    pnpCallbacks.EvtDeviceD0Exit          = Rt5682S_EvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    /* Allocate Device Context */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, RT5682S_DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "Rt5682SAudio: WdfDeviceCreate failed 0x%08X\n", status));
        return status;
    }

    devCtx = GetRt5682SContext(device);
    RtlZeroMemory(devCtx, sizeof(RT5682S_DEVICE_CONTEXT));
    devCtx->Device           = device;
    devCtx->VolumeStep       = 0;                 /* 0 dB */
    devCtx->SampleRate       = 48000;             /* Standard 48 kHz */
    devCtx->CurrentJackState = CSAUDIO_JACK_UNPLUGGED;

    /* Create Spinlock for hardware state synchronization */
    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &devCtx->Lock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Configure default parallel I/O queue for IOCTLs */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = Rt5682S_EvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &devCtx->DefaultQueue);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "Rt5682SAudio: WdfIoQueueCreate failed 0x%08X\n", status));
        return status;
    }

    /* Jack polling timer (passive level: it does I2C reads) */
    {
        WDF_OBJECT_ATTRIBUTES attr;
        WDF_TIMER_CONFIG timerConfig;

        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = device;
        status = WdfWaitLockCreate(&attr, &devCtx->JackLock);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        /* one-shot, re-armed by the callback (passive-level timers are kept non-periodic) */
        WDF_TIMER_CONFIG_INIT(&timerConfig, Rt5682S_EvtJackTimer);
        timerConfig.AutomaticSerialization = FALSE;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = device;
        attr.ExecutionLevel = WdfExecutionLevelPassive;
        status = WdfTimerCreate(&timerConfig, &attr, &devCtx->JackTimer);
        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
                "Rt5682SAudio: jack timer creation failed 0x%08X\n", status));
            devCtx->JackTimer = NULL;
        }
    }

    /* Create dedicated manual queue to hold pending jack state change notifications */
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &devCtx->JackNotificationQueue);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
            "Rt5682SAudio: JackNotificationQueue creation failed 0x%08X\n", status));
    }

    /* Expose Device Interfaces */
    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_RT5682S_CODEC, NULL);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
            "Rt5682SAudio: Failed to create RT5682S interface 0x%08X\n", status));
    }

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_CSAUDIO_CODEC, NULL);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
            "Rt5682SAudio: Failed to create CSAudio codec interface 0x%08X\n", status));
    }

    return STATUS_SUCCESS;
}

/* =========================================================================
 * EvtPrepareHardware — Parse ACPI I2C & Interrupt Resources
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt5682S_EvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PRT5682S_DEVICE_CONTEXT devCtx;
    ULONG resourceCount;
    BOOLEAN foundI2c = FALSE;
    BOOLEAN foundIrq = FALSE;
    NTSTATUS status;

    PAGED_CODE();
    devCtx = GetRt5682SContext(Device);

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt5682SAudio: EvtPrepareHardware\n"));

    /* Step 1: Scan ResourcesTranslated */
    if (ResourcesTranslated != NULL) {
        resourceCount = WdfCmResourceListGetCount(ResourcesTranslated);
        for (ULONG i = 0; i < resourceCount; i++) {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
            if (!desc) continue;

            if (desc->Type == CmResourceTypeConnection) {
                devCtx->I2cConnectionId.LowPart  = desc->u.Connection.IdLowPart;
                devCtx->I2cConnectionId.HighPart = desc->u.Connection.IdHighPart;
                foundI2c = TRUE;
                KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
                    "Rt5682SAudio: Found I2C connection ID in translated list (0x%08X:0x%08X)\n",
                    devCtx->I2cConnectionId.HighPart,
                    devCtx->I2cConnectionId.LowPart));
            } else if (desc->Type == CmResourceTypeInterrupt) {
                devCtx->GpioInterruptVector = desc->u.Interrupt.Vector;
                foundIrq = TRUE;
                KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
                    "Rt5682SAudio: Found GPIO Jack Detect Interrupt Vector 0x%08X\n",
                    devCtx->GpioInterruptVector));
            }
        }
    }

    /* Step 2: Fallback scan ResourcesRaw if I2C connection not found */
    if (!foundI2c && ResourcesRaw != NULL) {
        resourceCount = WdfCmResourceListGetCount(ResourcesRaw);
        for (ULONG i = 0; i < resourceCount; i++) {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = WdfCmResourceListGetDescriptor(ResourcesRaw, i);
            if (!desc) continue;

            if (desc->Type == CmResourceTypeConnection) {
                devCtx->I2cConnectionId.LowPart  = desc->u.Connection.IdLowPart;
                devCtx->I2cConnectionId.HighPart = desc->u.Connection.IdHighPart;
                foundI2c = TRUE;
                KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
                    "Rt5682SAudio: Found I2C connection ID in raw list (0x%08X:0x%08X)\n",
                    devCtx->I2cConnectionId.HighPart,
                    devCtx->I2cConnectionId.LowPart));
                break;
            }
        }
    }

    if (!foundI2c) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
            "Rt5682SAudio: No I2C connection resource discovered (device idle)\n"));
        return STATUS_SUCCESS;
    }

    /* Open SPB I2C target via Resource Hub path */
    {
        WCHAR devicePathBuffer[RESOURCE_HUB_PATH_SIZE];
        UNICODE_STRING devicePath;
        WDF_OBJECT_ATTRIBUTES targetAttributes;
        WDF_IO_TARGET_OPEN_PARAMS openParams;

        RtlInitEmptyUnicodeString(&devicePath, devicePathBuffer, sizeof(devicePathBuffer));

        status = RESOURCE_HUB_CREATE_PATH_FROM_ID(
            &devicePath,
            devCtx->I2cConnectionId.LowPart,
            devCtx->I2cConnectionId.HighPart
        );

        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
                "Rt5682SAudio: RESOURCE_HUB_CREATE_PATH_FROM_ID returned 0x%08X\n", status));
            return STATUS_SUCCESS;
        }

        WDF_OBJECT_ATTRIBUTES_INIT(&targetAttributes);
        targetAttributes.ParentObject = Device;

        status = WdfIoTargetCreate(Device, &targetAttributes, &devCtx->SpbTarget);
        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
                "Rt5682SAudio: WdfIoTargetCreate returned 0x%08X\n", status));
            return STATUS_SUCCESS;
        }

        WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(
            &openParams,
            &devicePath,
            (GENERIC_READ | GENERIC_WRITE)
        );
        openParams.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;

        status = WdfIoTargetOpen(devCtx->SpbTarget, &openParams);
        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
                "Rt5682SAudio: WdfIoTargetOpen returned 0x%08X (deferred)\n", status));
            return STATUS_SUCCESS;
        }
    }

    /* Create GPIO Interrupt object if an interrupt resource was discovered */
    if (foundIrq && devCtx->Interrupt == NULL) {
        WDF_INTERRUPT_CONFIG interruptConfig;
        WDF_INTERRUPT_CONFIG_INIT(
            &interruptConfig,
            Rt5682S_EvtInterruptIsr,
            NULL
        );
        interruptConfig.EvtInterruptWorkItem = Rt5682S_EvtInterruptWorkItem;
        interruptConfig.PassiveHandling      = TRUE; /* I2C transactions require passive level */

        status = WdfInterruptCreate(
            Device,
            &interruptConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
            &devCtx->Interrupt
        );

        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
                "Rt5682SAudio: WdfInterruptCreate returned 0x%08X (polling fallback)\n", status));
        } else {
            KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
                "Rt5682SAudio: Hardware Jack Detect Interrupt bound successfully\n"));
        }
    }

    return STATUS_SUCCESS;
}

/* =========================================================================
 * EvtReleaseHardware — Close I2C target
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt5682S_EvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PRT5682S_DEVICE_CONTEXT devCtx;

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    PAGED_CODE();

    devCtx = GetRt5682SContext(Device);

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt5682SAudio: EvtReleaseHardware\n"));

    if (devCtx->SpbTarget != NULL) {
        WdfIoTargetClose(devCtx->SpbTarget);
        WdfObjectDelete(devCtx->SpbTarget);
        devCtx->SpbTarget = NULL;
    }

    return STATUS_SUCCESS;
}

/* =========================================================================
 * EvtD0Entry — Power Up & Codec Initialization
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt5682S_EvtD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PRT5682S_DEVICE_CONTEXT devCtx;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(PreviousState);
    PAGED_CODE();

    devCtx = GetRt5682SContext(Device);

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt5682SAudio: EvtD0Entry (powering up)\n"));

    devCtx->PowerActive = TRUE;

    if (!devCtx->SpbTarget) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
            "Rt5682SAudio: EvtD0Entry - No I2C target, skipping hardware initialization\n"));
        return STATUS_SUCCESS;
    }

    /* Keep polling and the codec's automatic detector on the same polarity.
     * Volmar's coreboot property is active-low; the live GPIO is high while
     * unplugged and low while a plug is inserted. */
    devCtx->JackActiveLow = NauReadConfigDword(L"JackActiveLow", 1) ? TRUE : FALSE;

    status = Rt5682S_InitializeHardware(devCtx);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
            "Rt5682SAudio: Rt5682S_InitializeHardware returned 0x%08X (deferred initialization)\n", status));
        return STATUS_SUCCESS;
    }

    /* Jack detection: polled (the old interrupt set-up used guessed register
     * bits and is no longer enabled). First reading is applied immediately. */
    devCtx->JackKnown = FALSE;
    devCtx->JackCandidate = 0xFF;
    Rt5682S_ProcessJackStateChange(devCtx);
    devCtx->JackPollStop = FALSE;
    if (devCtx->JackTimer) {
        WdfTimerStart(devCtx->JackTimer, WDF_REL_TIMEOUT_IN_MS(NAU_JACK_POLL_MS));
    }

    return STATUS_SUCCESS;
}

/* =========================================================================
 * EvtD0Exit — Power Down & Click Suppression
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt5682S_EvtD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PRT5682S_DEVICE_CONTEXT devCtx;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(TargetState);
    PAGED_CODE();

    devCtx = GetRt5682SContext(Device);

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt5682SAudio: EvtD0Exit (powering down)\n"));

    devCtx->JackPollStop = TRUE;
    if (devCtx->JackTimer) {
        WdfTimerStop(devCtx->JackTimer, TRUE);
        WdfTimerStop(devCtx->JackTimer, TRUE);   /* in case the callback re-armed meanwhile */
    }
    devCtx->PowerActive = FALSE;

    /* Ramp down headphone volume smoothly */
    status = Rt5682S_SetVolume(devCtx, RT5682S_HP_VOL_MIN, TRUE);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
            "Rt5682SAudio: EvtD0Exit - SetVolume failed 0x%08X\n", status));
    }

    /* Power down DAC and ADC paths */
    status = Rt5682S_SetPlaybackState(devCtx, FALSE);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
            "Rt5682SAudio: EvtD0Exit - SetPlaybackState failed 0x%08X\n", status));
    }
    status = Rt5682S_SetCaptureState(devCtx, FALSE);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL,
            "Rt5682SAudio: EvtD0Exit - SetCaptureState failed 0x%08X\n", status));
    }

    return STATUS_SUCCESS;
}

/* =========================================================================
 * Codec register log: HKLM\SOFTWARE\CrosSofAudio\Codec
 * ========================================================================= */

#define NAUL_REG_ROOT  L"\\Registry\\Machine\\SOFTWARE\\CrosSofAudio"
#define NAUL_REG_LOG   L"\\Registry\\Machine\\SOFTWARE\\CrosSofAudio\\Codec"

static VOID NauLog(_In_ PCWSTR Name, _In_ ULONG Value)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return;
    RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, NAUL_REG_ROOT);
    RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, NAUL_REG_LOG);
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, NAUL_REG_LOG, Name, REG_DWORD, &Value, sizeof(Value));
}

static VOID NauLogReg(_In_ PRT5682S_DEVICE_CONTEXT DevCtx, _In_ PCWSTR Name, _In_ UINT16 Reg)
{
    UINT16 v = 0;
    if (NT_SUCCESS(I2cReadRegister16(DevCtx->SpbTarget, Reg, &v))) {
        NauLog(Name, v);
    } else {
        NauLog(Name, 0xDEAD0000u | Reg);
    }
}

static VOID NauSnapshot(_In_ PRT5682S_DEVICE_CONTEXT DevCtx)
{
    NauLogReg(DevCtx, L"R02_HP_CTRL_1",     RT_HP_CTRL_1);
    NauLogReg(DevCtx, L"R08_I2C_CTRL",      RT_I2C_CTRL);
    NauLogReg(DevCtx, L"R10_CBJ_CTRL_1",    RT_CBJ_CTRL_1);
    NauLogReg(DevCtx, L"R11_CBJ_CTRL_2",    RT_CBJ_CTRL_2);
    NauLogReg(DevCtx, L"R19_DAC1_DIG_VOL",  RT_DAC1_DIG_VOL);
    NauLogReg(DevCtx, L"R61_PWR_DIG_1",     RT_PWR_DIG_1);
    NauLogReg(DevCtx, L"R62_PWR_DIG_2",     RT_PWR_DIG_2);
    NauLogReg(DevCtx, L"R63_PWR_ANLG_1",    RT_PWR_ANLG_1);
    NauLogReg(DevCtx, L"R65_PWR_ANLG_3",    RT_PWR_ANLG_3);
    NauLogReg(DevCtx, L"R70_I2S1_SDP",      RT_I2S1_SDP);
    NauLogReg(DevCtx, L"R73_ADDA_CLK_1",    RT_ADDA_CLK_1);
    NauLogReg(DevCtx, L"R79_TDM_CTRL",      RT_TDM_CTRL);
    NauLogReg(DevCtx, L"R7A_TDM_ADDA_CTRL_1", RT_TDM_ADDA_CTRL_1);
    NauLogReg(DevCtx, L"R7B_TDM_ADDA_CTRL_2", RT_TDM_ADDA_CTRL_2);
    NauLogReg(DevCtx, L"R80_GLB_CLK",       RT_GLB_CLK);
    NauLogReg(DevCtx, L"R91_HP_CHARGE_PUMP_2", RT_HP_CHARGE_PUMP_2);
    NauLogReg(DevCtx, L"R9A_PLL_CTRL_3",    RT_PLL_CTRL_3);
    NauLogReg(DevCtx, L"R9B_PLL_CTRL_4",    RT_PLL_CTRL_4);
    NauLogReg(DevCtx, L"RBE_INT_ST_1",      RT_INT_ST_1);
    NauLogReg(DevCtx, L"RF0_AJD1_CTRL",     RT_AJD1_CTRL);
    NauLogReg(DevCtx, L"R210_SAR_IL_CMD_1", RT_SAR_IL_CMD_1);
    NauLogReg(DevCtx, L"RFE_VENDOR_ID",     RT_VENDOR_ID);
    NauLogReg(DevCtx, L"RFF_DEVICE_ID",     RT_DEVICE_ID);
}

static VOID NauSleepMs(_In_ ULONG Ms)
{
    LARGE_INTEGER d;
    d.QuadPart = -10000LL * (LONGLONG)Ms;
    KeDelayExecutionThread(KernelMode, FALSE, &d);
}

#define NAU_UPD(reg, mask, val) \
    do { status = I2cUpdateBits16(DevCtx->SpbTarget, (UINT16)(reg), (UINT16)(mask), (UINT16)(val)); \
         if (!NT_SUCCESS(status)) { NauLog(L"LastFailedReg", (reg)); return status; } } while (0)
#define NAU_WR(reg, val) \
    do { status = I2cWriteRegister16(DevCtx->SpbTarget, (UINT16)(reg), (UINT16)(val)); \
         if (!NT_SUCCESS(status)) { NauLog(L"LastFailedReg", (reg)); return status; } } while (0)

/*
 * Jack pin level from RTL5682S AJD1_CTRL[4] (RT5682S_JDH_RS_MASK).
 * Volmar/Marasov declares JKDET active-low in coreboot: 0 = inserted,
 * 1 = unplugged.  JackActiveLow is kept only as a diagnostic/config value so
 * existing registry tooling does not break; the polarity is a chip property.
 */
static BOOLEAN NauJackRead(_In_ PRT5682S_DEVICE_CONTEXT DevCtx, _Out_ BOOLEAN* Inserted, _Out_opt_ UINT16* Raw)
{
    UINT16 jackStatus = 0;
    *Inserted = FALSE;
    if (!DevCtx->SpbTarget) return FALSE;
    if (!NT_SUCCESS(I2cReadRegister16(DevCtx->SpbTarget, RT_AJD1_CTRL, &jackStatus))) return FALSE;
    if (Raw) *Raw = jackStatus;
    *Inserted = ((jackStatus & RT_JDH_RS_MASK) == RT_JACK_INSERTED);
    return TRUE;
}

static BOOLEAN NauJackInserted(_In_ PRT5682S_DEVICE_CONTEXT DevCtx)
{
    BOOLEAN ins = FALSE;
    (void)NauJackRead(DevCtx, &ins, NULL);
    return ins;
}

static ULONG NauReadConfigDword(_In_ PCWSTR Name, _In_ ULONG Default)
{
    ULONG value = Default, def = Default;
    RTL_QUERY_REGISTRY_TABLE table[2];
    RtlZeroMemory(table, sizeof(table));
    table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    table[0].Name = (PWSTR)Name;
    table[0].EntryContext = &value;
    table[0].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_DWORD;
    table[0].DefaultData = &def;
    table[0].DefaultLength = sizeof(def);
    if (!NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
            L"\\Registry\\Machine\\SOFTWARE\\CrosSofAudio", table, NULL, NULL)))
        return Default;
    return value;
}

/* rt5682s_configure_sysclk(RT5682S_CLK_DIS / RT5682S_CLK_INTERNAL) as done
 * by system_clock_control() when a stream ends. */
static NTSTATUS NauSysclkIdle(_In_ PRT5682S_DEVICE_CONTEXT DevCtx)
{
    /* On the real RTL5682S the jack-detection clock is derived from RC_CLK
     * (RC_CLK_CTRL) and PLLB is configured once during hardware init.  There
     * is no NAU-style FLL to park here, so keep the configured clocks intact. */
    UNREFERENCED_PARAMETER(DevCtx);
    return STATUS_SUCCESS;
}

typedef struct _RT_REG_VALUE {
    UINT16 Reg;
    UINT16 Val;
} RT_REG_VALUE;

static NTSTATUS RtWriteBurst(_In_ PRT5682S_DEVICE_CONTEXT DevCtx,
                             _In_reads_(Count) const RT_REG_VALUE* Regs,
                             _In_ ULONG Count)
{
    for (ULONG i = 0; i < Count; i++) {
        NTSTATUS st = I2cWriteRegister16(DevCtx->SpbTarget, Regs[i].Reg, Regs[i].Val);
        if (!NT_SUCCESS(st)) {
            NauLog(L"LastFailedReg", Regs[i].Reg);
            return st;
        }
    }
    return STATUS_SUCCESS;
}

/* Configure the codec clocks for the SOF SSP0 configuration:
 * MCLK 19.2 MHz (from the platform), BCLK 3.072 MHz, 48 kHz, 32-bit slots.
 * PLLB converts 19.2 MHz -> 24.576 MHz (N=6, M=2, K=3, prescaler by-passed).
 * This mirrors rt5682s_update_reclock() in the known-good CoolStar driver. */
static NTSTATUS Rt5682S_ConfigureReclock(_In_ PRT5682S_DEVICE_CONTEXT DevCtx)
{
    NTSTATUS status;

    /* PLLB source = MCLK, N/M/K from the 19.2 -> 24.576 MHz table. */
    NAU_UPD(RT_GLB_CLK, RT_PLL_SRC_MASK, RT_PLL_SRC_MCLK);
    NAU_UPD(RT_PLL_CTRL_3, RT_PLLB_N_MASK, 6);
    NAU_UPD(RT_PLL_CTRL_4, RT_PLLB_M_MASK | RT_PLLB_K_MASK,
            (2u << RT_PLLB_M_SFT) | 3u);
    NAU_UPD(RT_PLL_CTRL_6,
            RT_PLLB_SEL_PS_MASK | RT_PLLB_BYP_PS_MASK |
            RT_PLLB_M_BP_MASK | RT_PLLB_K_BP_MASK,
            0);
    NAU_UPD(RT_PLL_CTRL_7, RT_PLLB_SRC_MASK, RT_PLLB_SRC_DFIN);

    /* 2 slots, 32-bit, TX/RX enabled.  CTRL_1 bit 15 and CTRL_2 bit 7 are
     * the values used by the working Alder Lake initialization. */
    NAU_UPD(RT_TDM_ADDA_CTRL_1, RT_TDM_CTRL1_IF_EN, RT_TDM_CTRL1_IF_EN);
    NAU_UPD(RT_TDM_ADDA_CTRL_2, RT_TDM_EN, RT_TDM_EN);
    NAU_UPD(RT_TDM_CTRL,
            RT_TDM_TX_CH_MASK | RT_TDM_RX_CH_MASK | RT_TDM_ADC_DL_MASK, 0);
    NAU_UPD(RT_TDM_TCON_CTRL_1,
            RT_TDM_DF_MASK | RT_TDM_BCLK_MS1_MASK | RT_TDM_CL_MASK |
            RT_TDM_MS_MASK,
            RT_TDM_DF_I2S | RT_TDM_CL_32 | RT_TDM_MS_S);
    NAU_UPD(RT_I2S1_SDP,
            RT_SEL_ADCDAT_MASK | RT_I2S1_MONO_MASK |
            RT_I2S1_TX_CHL_MASK | RT_I2S1_RX_CHL_MASK |
            RT_I2S1_DL_MASK | RT_I2S_DF_MASK,
            RT_SEL_ADCDAT_OUT | RT_I2S1_TX_CHL_32 | RT_I2S1_RX_CHL_32 |
            RT_I2S1_DL_32 | RT_I2S_DF_I2S);

    /* System clock = PLLB; also select it for the I2S master dividers. */
    NAU_UPD(RT_GLB_CLK, RT_SCLK_SRC_MASK, RT_CLK_SRC_PLL2 << RT_SCLK_SRC_SFT);
    NAU_UPD(RT_ADDA_CLK_1, RT_I2S_M_CLK_SRC_MASK,
            RT_CLK_SRC_PLL2 << RT_I2S_M_CLK_SRC_SFT);
    NAU_UPD(RT_I2S2_M_CLK_CTRL_1, RT_I2S2_M_CLK_SRC_MASK,
            RT_CLK_SRC_PLL2 << RT_I2S2_M_CLK_SRC_SFT);

    /* Power up PLLB (LDO, bias, core, then release reset). */
    NAU_UPD(RT_PWR_ANLG_3, RT_PLLB_POWER_MASK, RT_PLLB_POWER_MASK);

    return STATUS_SUCCESS;
}

/* Real RTL5682S init: Linux patch list + calibration + Alder Lake defaults +
 * jack detection setup.  Replaces the legacy NAU-style sequence. */
static NTSTATUS NauInitRegs(_In_ PRT5682S_DEVICE_CONTEXT DevCtx)
{
    NTSTATUS status;
    UINT16 calStatus = 0;

    static const RT_REG_VALUE patchList[] = {
        { RT_I2C_CTRL,                  0x0007 },
        { RT_DIG_IN_CTRL_1,             0x0000 },
        { RT_CHOP_DAC_2,                0x2020 },
        { RT_VREF_REC_OP_FB_CAP_CTRL_2, 0x0101 },
        { RT_VREF_REC_OP_FB_CAP_CTRL_1, 0x80C0 },
        { RT_HP_CALIB_CTRL_9,           0x0002 },
        { RT_DEPOP_1,                   0x0000 },
        { RT_HP_CHARGE_PUMP_2,          0x3C15 },
        { RT_DAC1_DIG_VOL,              0xFEFE },
        { RT_SAR_IL_CMD_2,              0xAC00 },
        { RT_SAR_IL_CMD_3,              0x024C },
        { RT_CBJ_CTRL_6,                0x0804 },
    };

    static const RT_REG_VALUE alderClocks[] = {
        { RT_RC_CLK_CTRL,       0xC009 },
        { RT_I2S2_M_CLK_CTRL_1, 0x0020 },
        { RT_GLB_CLK,           0x0000 },
        { RT_ADDA_CLK_1,        0x1021 },
    };

    static const RT_REG_VALUE finishInit[] = {
        { RT_PLL_TRACK_2,       0x0100 },
        { RT_STO1_ADC_DIG_VOL,  0x2FAF },
        { RT_REC_MIXER,         0x1940 },
        { RT_I2S2_SDP,          0x4000 },
        { RT_PLL_TRACK_3,       0x0100 },
        { RT_GPIO_CTRL_1,       0x6960 },
        { RT_DMIC_CTRL_1,       0x0800 },
        { RT_HP_CTRL_2,         0x6001 },
        { RT_STO1_ADC_MIXER,    0x6064 },
        { RT_PWR_DIG_1,         0x8DD1 },
        { RT_PWR_DIG_2,         0x840A },
        { RT_PWR_ANLG_2,        0x8001 },
        { RT_PWR_ANLG_3,        0x52A1 },
        { RT_CLK_DET,           0x8000 },
        { RT_DEPOP_1,           0x007B },
        { RT_BIAS_CUR_CTRL_12,  0xA82A },
    };

    static const RT_REG_VALUE alderDefaults[] = {
        { RT_HP_CTRL_1,         0x8080 },
        { RT_DAC1_DIG_VOL,      0xECEC },
        { RT_STO1_DAC_MIXER,    0x2080 },
        { RT_I2S1_SDP,          0x0000 },
        { RT_TDM_ADDA_CTRL_1,   RT_TDM_CTRL1_IF_EN },
        { RT_TDM_ADDA_CTRL_2,   RT_TDM_EN },
    };

    status = RtWriteBurst(DevCtx, patchList, RTL_NUMBER_OF(patchList));
    if (!NT_SUCCESS(status)) return status;

    NAU_UPD(RT_PWR_DIG_2, RT_DLDO_I_LIMIT_MASK, RT_DLDO_I_LIMIT_DIS);
    NauSleepMs(20);

    /* HP calibration (rt5682s_calibrate). */
    NAU_WR(RT_PWR_ANLG_1, 0xAA80);
    NauSleepMs(15);
    NAU_WR(RT_PWR_ANLG_1, 0xFA80);
    NAU_WR(RT_PWR_DIG_1, 0x01C0);
    NAU_WR(RT_MICBIAS_2, 0x0380);
    NAU_WR(RT_GLB_CLK, 0x8000);
    NAU_WR(RT_ADDA_CLK_1, 0x1001);
    NAU_WR(RT_CHOP_DAC_2, 0x3030);
    NAU_WR(RT_CHOP_ADC, 0xB000);
    NAU_WR(RT_STO1_ADC_MIXER, 0x686C);
    NAU_WR(RT_CAL_REC, 0x5151);
    NAU_WR(RT_HP_CALIB_CTRL_2, 0x0321);
    NAU_WR(RT_HP_LOGIC_CTRL_2, 0x0004);
    NAU_WR(RT_HP_CALIB_CTRL_1, 0x7C00);
    NAU_WR(RT_HP_CALIB_CTRL_1, 0xFC00);
    for (ULONG i = 0; i < 60; i++) {
        status = I2cReadRegister16(DevCtx->SpbTarget, RT_HP_CALIB_ST_1, &calStatus);
        if (!NT_SUCCESS(status)) return status;
        if (!(calStatus & 0x8000)) break;
        NauSleepMs(10);
    }
    NauLog(L"HpCalibStatus", calStatus);

    /* Restore non-calibration register values. */
    NAU_WR(RT_MICBIAS_2, 0x0180);
    NAU_WR(RT_CAL_REC, 0x5859);
    NAU_WR(RT_STO1_ADC_MIXER, 0xC0C4);
    NAU_WR(RT_HP_CALIB_CTRL_2, 0x0320);
    NAU_WR(RT_PWR_DIG_1, 0x00C0);
    NAU_WR(RT_PWR_ANLG_1, 0x0800);
    NAU_WR(RT_GLB_CLK, 0x0000);

    NAU_UPD(RT_MICBIAS_2,
            RT_PWR_CLK25M_MASK | RT_PWR_CLK1M_MASK,
            RT_PWR_CLK25M_PD | RT_PWR_CLK1M_PU);
    NAU_UPD(RT_PWR_ANLG_1, RT_PWR_BG, RT_PWR_BG);
    NAU_UPD(RT_HP_LOGIC_CTRL_2, RT_HP_SIG_SRC_MASK, RT_HP_SIG_SRC_1BIT_CTL);
    NAU_UPD(RT_HP_CHARGE_PUMP_2, RT_PM_HP_MASK, RT_PM_HP_HV);
    NAU_UPD(RT_HP_AMP_DET_CTRL_1,
            RT_CP_SW_SIZE_MASK, RT_CP_SW_SIZE_L | RT_CP_SW_SIZE_S);

    status = RtWriteBurst(DevCtx, alderClocks, RTL_NUMBER_OF(alderClocks));
    if (!NT_SUCCESS(status)) return status;
    status = RtWriteBurst(DevCtx, finishInit, RTL_NUMBER_OF(finishInit));
    if (!NT_SUCCESS(status)) return status;
    status = RtWriteBurst(DevCtx, alderDefaults, RTL_NUMBER_OF(alderDefaults));
    if (!NT_SUCCESS(status)) return status;

    status = Rt5682S_ConfigureReclock(DevCtx);
    if (!NT_SUCCESS(status)) return status;

    /* Embedded jack detection (RT5682S_JD1), as in rt5682s_set_jack_detect(). */
    NAU_UPD(RT_CBJ_CTRL_5, RT_JD_FAST_OFF_SRC_MASK, RT_JD_FAST_OFF_SRC_JDH);
    NAU_UPD(RT_CBJ_CTRL_2, RT_EXT_JD_SRC_MASK, RT_EXT_JD_SRC_MANUAL);
    NAU_UPD(RT_CBJ_CTRL_1,
            RT_EMB_JD_EN | RT_DET_TYPE | RT_POL_FAST_OFF_HIGH | RT_MIC_CAP_MASK,
            RT_EMB_JD_EN | RT_DET_TYPE | RT_POL_FAST_OFF_HIGH | RT_MIC_CAP_HS);
    NAU_UPD(RT_SAR_IL_CMD_1, RT_SAR_POW_MASK, RT_SAR_POW_EN);
    NAU_UPD(RT_GPIO_CTRL_1, RT_GP1_PIN_MASK, RT_GP1_PIN_IRQ);
    NAU_UPD(RT_PWR_ANLG_3, RT_PWR_BGLDO, RT_PWR_BGLDO);
    NAU_UPD(RT_PWR_ANLG_2, RT_PWR_JD_MASK, RT_PWR_JD_ENABLE);
    NAU_UPD(RT_RC_CLK_CTRL, RT_POW_IRQ | RT_POW_JDH, RT_POW_IRQ | RT_POW_JDH);
    NAU_UPD(RT_IRQ_CTRL_2, RT_JD1_EN_MASK | RT_JD1_POL_MASK,
            RT_JD1_EN | RT_JD1_POL_NOR);

    /* Button detection windows (16 ms). */
    NAU_UPD(RT_4BTN_IL_CMD_4,
            RT_4BTN_IL_HOLD_WIN_MASK | RT_4BTN_IL_CLICK_WIN_MASK,
            (16u << RT_4BTN_IL_HOLD_WIN_SFT) | 16u);
    NAU_UPD(RT_4BTN_IL_CMD_5,
            RT_4BTN_IL_HOLD_WIN_MASK | RT_4BTN_IL_CLICK_WIN_MASK,
            (16u << RT_4BTN_IL_HOLD_WIN_SFT) | 16u);
    NAU_UPD(RT_4BTN_IL_CMD_6,
            RT_4BTN_IL_HOLD_WIN_MASK | RT_4BTN_IL_CLICK_WIN_MASK,
            (16u << RT_4BTN_IL_HOLD_WIN_SFT) | 16u);
    NAU_UPD(RT_4BTN_IL_CMD_7,
            RT_4BTN_IL_HOLD_WIN_MASK | RT_4BTN_IL_CLICK_WIN_MASK,
            (16u << RT_4BTN_IL_HOLD_WIN_SFT) | 16u);

    NauLog(L"Rt5682SInit", 1);
    return STATUS_SUCCESS;
}

/* =========================================================================
 * Rt5682S_InitializeHardware — reset, identify, Linux init_regs, clock idle.
 * The headphone path stays powered down until a stream starts.
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt5682S_InitializeHardware(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS status;
    UINT16 vendorId = 0;
    UINT16 deviceId = 0;

    PAGED_CODE();

    if (!DevCtx->SpbTarget) return STATUS_INVALID_DEVICE_STATE;

    DevCtx->HwInitDone = FALSE;
    DevCtx->Streaming = FALSE;
    DevCtx->CaptureActive = FALSE;

    /* Linux rt5682s_reset() / CoolStar BOOTCODEC(): write 0 to reset. */
    NAU_WR(RT_RESET, RT_RESET_VALUE);
    NauSleepMs(50);

    status = I2cReadRegister16(DevCtx->SpbTarget, RT_VENDOR_ID, &vendorId);
    if (!NT_SUCCESS(status)) {
        NauLog(L"InitStatus", (ULONG)status);
        return status;
    }
    status = I2cReadRegister16(DevCtx->SpbTarget, RT_DEVICE_ID, &deviceId);
    if (!NT_SUCCESS(status)) {
        NauLog(L"InitStatus", (ULONG)status);
        return status;
    }
    DevCtx->VendorIdReg = vendorId;
    DevCtx->DeviceIdReg = deviceId;
    NauLog(L"VendorIdReg", vendorId);
    NauLog(L"DeviceIdReg", deviceId);
    if (deviceId != RT_DEVICE_ID_VALUE) {
        NauLog(L"WrongDeviceId", deviceId);
        return STATUS_NO_SUCH_DEVICE;
    }
    DevCtx->SwId = 0; /* legacy field retained for telemetry compatibility */

    status = NauInitRegs(DevCtx);
    if (!NT_SUCCESS(status)) { NauLog(L"InitStatus", (ULONG)status); return status; }

    status = Rt5682S_EnableJackDetection(DevCtx);
    if (!NT_SUCCESS(status)) { NauLog(L"InitStatus", (ULONG)status); return status; }

    status = NauSysclkIdle(DevCtx);
    if (!NT_SUCCESS(status)) { NauLog(L"InitStatus", (ULONG)status); return status; }

    DevCtx->HwInitDone = TRUE;
    NauLog(L"InitStatus", 0);
    return STATUS_SUCCESS;
}

/* Kept for reference; clocks are now configured per stream (NauPlaybackOn). */
_Use_decl_annotations_
NTSTATUS
Rt5682S_ConfigureClocks(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx
    )
{
    UNREFERENCED_PARAMETER(DevCtx);
    PAGED_CODE();
    return STATUS_SUCCESS;
}

/* Shared SSP0 setup for RTL5682S playback and capture.  The codec is a clock
 * consumer and the SOF SSP provides MCLK/BCLK/FSYNC.  PLLB/TDM were already
 * configured by Rt5682S_ConfigureReclock(); only the AIF and analog power
 * domains are changed when a stream starts. */
static NTSTATUS NauStreamClockOn(_In_ PRT5682S_DEVICE_CONTEXT DevCtx)
{
    NTSTATUS status;

    /* I2S1 must be a slave and ADCDAT must be an output for capture. */
    NAU_UPD(RT_I2S2_SDP, RT_I2S2_MS_MASK, 0);
    NAU_UPD(RT_TDM_TCON_CTRL_1, RT_TDM_MS_MASK, RT_TDM_MS_S);
    NAU_UPD(RT_I2S1_SDP,
            RT_SEL_ADCDAT_MASK | RT_I2S1_MONO_MASK |
            RT_I2S1_TX_CHL_MASK | RT_I2S1_RX_CHL_MASK |
            RT_I2S1_DL_MASK | RT_I2S_DF_MASK,
            RT_SEL_ADCDAT_OUT | RT_I2S1_TX_CHL_32 | RT_I2S1_RX_CHL_32 |
            RT_I2S1_DL_32 | RT_I2S_DF_I2S);

    /* Enable digital and mixer power domains used by the headphone path. */
    NAU_UPD(RT_PWR_DIG_1, RT_PWR_I2S1, RT_PWR_I2S1);
    NAU_UPD(RT_PWR_DIG_2, RT_PWR_DAC_S1F, RT_PWR_DAC_S1F);
    NAU_UPD(RT_PWR_ANLG_3,
            RT_PWR_LDO_MB1 | RT_PWR_LDO_MB2,
            RT_PWR_LDO_MB1 | RT_PWR_LDO_MB2);

    return STATUS_SUCCESS;
}

/* Power up the RTL5682S stereo DAC and headphone output.  Order follows the
 * Linux rt5682s DAPM widgets and the working CoolStar Alder Lake defaults.
 * The DSP owns the loudness; the codec stays at 0 dB. */
static NTSTATUS NauPlaybackOn(_In_ PRT5682S_DEVICE_CONTEXT DevCtx)
{
    NTSTATUS status = STATUS_SUCCESS;
    if (!DevCtx->CaptureActive) {
        status = NauStreamClockOn(DevCtx);
        if (!NT_SUCCESS(status)) return status;
    }

    /* DAC mixer -> DAC L1/R1 -> headphone amplifier. */
    NAU_UPD(RT_PWR_MIXER, RT_PWR_STO1_DAC_L | RT_PWR_STO1_DAC_R,
            RT_PWR_STO1_DAC_L | RT_PWR_STO1_DAC_R);
    NAU_UPD(RT_PWR_DIG_1, RT_PWR_DAC_L1 | RT_PWR_DAC_R1,
            RT_PWR_DAC_L1 | RT_PWR_DAC_R1);
    NAU_UPD(RT_PWR_DIG_2, RT_PWR_DAC_S1F, RT_PWR_DAC_S1F);
    /* Known-good Alder Lake cros-feed routing (0x2080). */
    NAU_WR(RT_STO1_DAC_MIXER, 0x2080);

    NAU_UPD(RT_HP_CHARGE_PUMP_2,
            RT_PWR_CP | RT_PWR_HP_L | RT_PWR_HP_R,
            RT_PWR_CP | RT_PWR_HP_L | RT_PWR_HP_R);
    NauSleepMs(20);

    /* Linux rt5682s_hp_amp_event(): depop, charge-pump, bias and HPO paths. */
    NAU_UPD(RT_DEPOP_1, RT_OUT_HP_L_EN | RT_OUT_HP_R_EN,
            RT_OUT_HP_L_EN | RT_OUT_HP_R_EN);
    NauSleepMs(15);
    NAU_UPD(RT_DEPOP_1,
            RT_LDO_PUMP_EN | RT_PUMP_EN | RT_CAPLESS_L_EN | RT_CAPLESS_R_EN,
            RT_LDO_PUMP_EN | RT_PUMP_EN | RT_CAPLESS_L_EN | RT_CAPLESS_R_EN);
    NAU_WR(RT_BIAS_CUR_CTRL_11, 0x6666);
    NAU_WR(RT_BIAS_CUR_CTRL_12, 0xA82A);
    NAU_UPD(RT_HP_CTRL_2,
            RT_HPO_L_PATH_MASK | RT_HPO_R_PATH_MASK | RT_HPO_SEL_IP_EN_SW,
            RT_HPO_L_PATH_MASK | RT_HPO_R_PATH_MASK | RT_HPO_IP_EN_GATING);
    NauSleepMs(5);
    NAU_UPD(RT_HP_AMP_DET_CTRL_1,
            RT_CP_SW_SIZE_MASK, RT_CP_SW_SIZE_L | RT_CP_SW_SIZE_S);

    /* 0x8080 is the neutral analog gain for this board; DAC volume 0xFE/0xFE
     * is 0 dB.  Windows mute is handled in Rt5682S_SetVolume(). */
    NAU_WR(RT_HP_CTRL_1, RT_HP_CTRL_1_0DB);
    NAU_UPD(RT_DAC1_DIG_VOL,
            RT_DAC_L1_VOL_MASK | RT_DAC_R1_VOL_MASK,
            (RT_DAC_VOL_0DB << 8) | RT_DAC_VOL_0DB);

    return STATUS_SUCCESS;
}

/* Stream stop: power down the headphone output in reverse order. */
static NTSTATUS NauPlaybackOff(_In_ PRT5682S_DEVICE_CONTEXT DevCtx)
{
    NTSTATUS status;

    /* Mute the DAC before removing the analog supplies to avoid a pop. */
    NAU_UPD(RT_DAC1_DIG_VOL,
            RT_DAC_L1_VOL_MASK | RT_DAC_R1_VOL_MASK, RT_DAC_VOL_MUTE);
    NAU_UPD(RT_HP_CTRL_2,
            RT_HPO_L_PATH_MASK | RT_HPO_R_PATH_MASK | RT_HPO_SEL_IP_EN_SW, 0);
    NAU_UPD(RT_HP_AMP_DET_CTRL_1, RT_CP_SW_SIZE_MASK, RT_CP_SW_SIZE_M);
    NAU_UPD(RT_DEPOP_1,
            RT_LDO_PUMP_EN | RT_PUMP_EN | RT_CAPLESS_L_EN | RT_CAPLESS_R_EN, 0);
    NAU_UPD(RT_DEPOP_1, RT_OUT_HP_L_EN | RT_OUT_HP_R_EN, 0);
    NAU_UPD(RT_HP_CHARGE_PUMP_2,
            RT_PWR_CP | RT_PWR_HP_L | RT_PWR_HP_R, 0);
    NAU_UPD(RT_STO1_DAC_MIXER, 0x2080, 0);
    NAU_UPD(RT_PWR_DIG_1, RT_PWR_DAC_L1 | RT_PWR_DAC_R1, 0);
    NAU_UPD(RT_PWR_DIG_2, RT_PWR_DAC_S1F, 0);
    NAU_UPD(RT_PWR_MIXER, RT_PWR_STO1_DAC_L | RT_PWR_STO1_DAC_R, 0);

    if (!DevCtx->CaptureActive)
        return NauSysclkIdle(DevCtx);
    return STATUS_SUCCESS;
}

/* =========================================================================
 * RTL5682S jack/button helpers.
 *
 * Physical insertion is still polled through AJD1_CTRL because the ACPI GPIO
 * interrupt routing is unreliable on some Windows firmware.  Plug type is
 * classified with the codec's own CBJ state machine, exactly as in Linux
 * rt5682s_headset_detect(): CBJ_CTRL_2[1:0] = 0 -> 3-pole headphones,
 * 1 (OMTP) or 2 (CTIA) -> 4-pole headset.
 * ========================================================================= */

/* CoolStar rt5682s_sar_power_mode(SAR_PWR_NORMAL), also used by the Linux
 * v6.6 headset-detect path.  The combo-jack buffers and both microphone-bias
 * switches must be owned by the SAR state machine before the button detector
 * is enabled; merely setting SAR_IL_CMD_1 is not sufficient. */
static NTSTATUS Rt5682S_SarPowerNormal(_In_ PRT5682S_DEVICE_CONTEXT DevCtx)
{
    NTSTATUS status;
    PAGED_CODE();

    NAU_UPD(RT_CBJ_CTRL_3, RT_CBJ_IN_BUF_MASK, RT_CBJ_IN_BUF_EN);
    NAU_UPD(RT_CBJ_CTRL_1,
            RT_MB1_PATH_MASK | RT_MB2_PATH_MASK,
            RT_CTRL_MB1_FSM | RT_CTRL_MB2_FSM);
    NAU_UPD(RT_SAR_IL_CMD_1,
            RT_SAR_SEL_MB1_2_CTL_MASK,
            RT_SAR_SEL_MB1_2_AUTO);
    NauSleepMs(5);
    NAU_UPD(RT_SAR_IL_CMD_1,
            RT_SAR_BUTDET_MASK | RT_SAR_BUTDET_POW_MASK,
            RT_SAR_BUTDET_EN | RT_SAR_BUTDET_POW_NORM);

    NauLog(L"SarPowerNormal", 1);
    NauLogReg(DevCtx, L"Normal_R10_CBJ_CTRL_1", RT_CBJ_CTRL_1);
    NauLogReg(DevCtx, L"Normal_R210_SAR_IL_CMD_1", RT_SAR_IL_CMD_1);
    return STATUS_SUCCESS;
}

static NTSTATUS Rt5682S_SetButtonDetection(_In_ PRT5682S_DEVICE_CONTEXT DevCtx,
                                          _In_ BOOLEAN Enable)
{
    NTSTATUS status;

    if (Enable) {
        NAU_UPD(RT_SAR_IL_CMD_13, RT_SAR_SOUR_MASK, RT_SAR_SOUR_BTN);
        NAU_UPD(RT_SAR_IL_CMD_1,
                RT_SAR_BUTDET_MASK | RT_SAR_BUTDET_POW_MASK |
                RT_SAR_SEL_MB1_2_CTL_MASK,
                RT_SAR_BUTDET_EN | RT_SAR_BUTDET_POW_NORM |
                RT_SAR_SEL_MB1_2_AUTO);
        NAU_WR(RT_IL_CMD_1, 0x0040);
        NAU_UPD(RT_4BTN_IL_CMD_2,
                RT_4BTN_IL_MASK | RT_4BTN_IL_RST_MASK,
                RT_4BTN_IL_EN | RT_4BTN_IL_NOR);
        NAU_UPD(RT_IRQ_CTRL_3, RT_IL_IRQ_MASK, RT_IL_IRQ_EN);
    } else {
        NAU_UPD(RT_IRQ_CTRL_3, RT_IL_IRQ_MASK, RT_IL_IRQ_DIS);
        NAU_UPD(RT_4BTN_IL_CMD_2, RT_4BTN_IL_MASK, RT_4BTN_IL_DIS);
        NAU_UPD(RT_SAR_IL_CMD_13, RT_SAR_SOUR_MASK, RT_SAR_SOUR_TYPE);
        NAU_UPD(RT_SAR_IL_CMD_1,
                RT_SAR_BUTDET_MASK | RT_SAR_BUTDET_POW_MASK |
                RT_SAR_SEL_MB1_2_CTL_MASK,
                RT_SAR_BUTDET_DIS | RT_SAR_BUTDET_POW_SAV |
                RT_SAR_SEL_MB1_2_MANU);
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS
Rt5682S_EnableJackDetection(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS status;
    PAGED_CODE();
    if (!DevCtx->SpbTarget) return STATUS_INVALID_DEVICE_STATE;

    /* Stable idle configuration is already written by NauInitRegs(); make
     * sure the button detector starts disabled and the JD IRQ is unmasked. */
    status = Rt5682S_SetButtonDetection(DevCtx, FALSE);
    if (!NT_SUCCESS(status)) return status;

    NAU_UPD(RT_IRQ_CTRL_2, RT_JD1_EN_MASK | RT_JD1_POL_MASK,
            RT_JD1_EN | RT_JD1_POL_NOR);
    NauLog(L"JackDetectReady", 1);
    return STATUS_SUCCESS;
}

/* Disable the type/button detector and put VREF/MB back into low power. */
static NTSTATUS NauDisableHeadsetMic(_In_ PRT5682S_DEVICE_CONTEXT DevCtx)
{
    NTSTATUS status;

    status = Rt5682S_SetButtonDetection(DevCtx, FALSE);
    if (!NT_SUCCESS(status)) return status;

    NAU_UPD(RT_SAR_IL_CMD_1, RT_SAR_POW_MASK, 0);
    NAU_UPD(RT_SAR_IL_CMD_2, RT_SAR_ADC_PSV_MASK, 0);
    NAU_UPD(RT_PWR_ANLG_1,
            RT_PWR_VREF1 | RT_PWR_VREF2 | RT_PWR_MB, 0);
    NAU_UPD(RT_PWR_ANLG_3, RT_PWR_CBJ, 0);
    NAU_UPD(RT_CBJ_CTRL_3, RT_CBJ_IN_BUF_MASK, RT_CBJ_IN_BUF_DIS);
    NAU_UPD(RT_CBJ_CTRL_1, RT_FAST_OFF_MASK, RT_FAST_OFF_DIS);
    NAU_UPD(RT_CBJ_CTRL_1, RT_TRIG_JD_MASK, RT_TRIG_JD_LOW);
    NAU_UPD(RT_HP_CHARGE_PUMP_2, RT_OSW_L_MASK | RT_OSW_R_MASK,
            RT_OSW_L_EN | RT_OSW_R_EN);
    return STATUS_SUCCESS;
}

/* Run the RTL5682S combo-jack detector.  Returns TRUE for a 4-pole headset. */
static NTSTATUS NauDetectHeadsetMic(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx,
    _Out_ BOOLEAN* MicPresent
    )
{
    NTSTATUS status;
    UINT16 jackType = 0;

    *MicPresent = FALSE;

    status = Rt5682S_SetButtonDetection(DevCtx, FALSE);
    if (!NT_SUCCESS(status)) return status;

    NAU_UPD(RT_PWR_ANLG_1,
            RT_PWR_VREF1 | RT_PWR_VREF2 | RT_PWR_MB,
            RT_PWR_VREF1 | RT_PWR_VREF2 | RT_PWR_MB);
    NAU_UPD(RT_PWR_ANLG_1, RT_PWR_FV1 | RT_PWR_FV2, 0);
    NauSleepMs(15);
    NAU_UPD(RT_PWR_ANLG_1, RT_PWR_FV1 | RT_PWR_FV2,
            RT_PWR_FV1 | RT_PWR_FV2);
    NAU_UPD(RT_PWR_ANLG_3, RT_PWR_CBJ, RT_PWR_CBJ);
    NAU_WR(RT_SAR_IL_CMD_3, 0x0365);
    NAU_UPD(RT_HP_CHARGE_PUMP_2, RT_OSW_L_MASK | RT_OSW_R_MASK,
            RT_OSW_L_DIS | RT_OSW_R_DIS);
    NAU_UPD(RT_SAR_IL_CMD_13, RT_SAR_SOUR_MASK, RT_SAR_SOUR_TYPE);
    NAU_UPD(RT_CBJ_CTRL_3, RT_CBJ_IN_BUF_MASK, RT_CBJ_IN_BUF_EN);
    NAU_UPD(RT_CBJ_CTRL_1, RT_TRIG_JD_MASK, RT_TRIG_JD_LOW);
    NauSleepMs(45);
    NAU_UPD(RT_CBJ_CTRL_1, RT_TRIG_JD_MASK, RT_TRIG_JD_HIGH);

    /* Poll until the CBJ state machine reports a known 4-pole type.  Linux
     * and CoolStar retry only while the raw value is 0; on this board the
     * first settled value is frequently 3 (an intermediate/invalid state),
     * so retry that as well before deciding the plug has no microphone. */
    for (ULONG i = 0; i < 50; i++) {
        status = I2cReadRegister16(DevCtx->SpbTarget, RT_CBJ_CTRL_2, &jackType);
        if (!NT_SUCCESS(status)) return status;
        jackType &= RT_JACK_TYPE_MASK;
        if (jackType == RT_JACK_TYPE_OMTP || jackType == RT_JACK_TYPE_CTIA) break;
        NauSleepMs(15);
    }
    jackType &= RT_JACK_TYPE_MASK;
    NauLog(L"MicDetectCode", jackType);
    NauLogReg(DevCtx, L"CbjCtrl2", RT_CBJ_CTRL_2);

    if (jackType != RT_JACK_TYPE_HEADPHONE) {
        /* 1 = OMTP, 2 = CTIA, 3 = ambiguous.  Treat every non-zero result as
         * a headset so the microphone endpoint is usable.  For the ambiguous
         * case default to CTIA (MB2) bias; a plain 3-pole plug simply yields
         * silence on that endpoint and playback is unaffected. */
        UINT16 mbSelect = (jackType == RT_JACK_TYPE_OMTP) ? jackType
                                                          : RT_JACK_TYPE_CTIA;
        NAU_WR(RT_SAR_IL_CMD_3, 0x024C);
        NAU_UPD(RT_CBJ_CTRL_1, RT_FAST_OFF_MASK, RT_FAST_OFF_EN);
        NAU_UPD(RT_SAR_IL_CMD_1, RT_SAR_SEL_MB1_2_MASK,
                mbSelect << RT_SAR_SEL_MB1_2_SFT);
        status = Rt5682S_SetButtonDetection(DevCtx, TRUE);
        if (!NT_SUCCESS(status)) return status;
        status = Rt5682S_SarPowerNormal(DevCtx);
        if (!NT_SUCCESS(status)) return status;
        *MicPresent = TRUE;
    } else {
        NAU_WR(RT_SAR_IL_CMD_3, 0x024C);
        NAU_UPD(RT_CBJ_CTRL_1, RT_FAST_OFF_MASK, RT_FAST_OFF_EN);
    }

    NAU_UPD(RT_HP_CHARGE_PUMP_2, RT_OSW_L_MASK | RT_OSW_R_MASK,
            RT_OSW_L_EN | RT_OSW_R_EN);
    return STATUS_SUCCESS;
}

/* =========================================================================
 * Rt5682S_SetPlaybackState — Linux-order headphone path power up / down
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt5682S_SetPlaybackState(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx,
    _In_ BOOLEAN Enable
    )
{
    NTSTATUS status;
    PAGED_CODE();
    if (!DevCtx->SpbTarget) return STATUS_INVALID_DEVICE_STATE;

    if (!DevCtx->HwInitDone) {
        status = Rt5682S_InitializeHardware(DevCtx);
        if (!NT_SUCCESS(status)) return status;
    }

    if (Enable) {
        if (DevCtx->Streaming) return STATUS_SUCCESS;
        status = NauPlaybackOn(DevCtx);
        NauLog(L"PlaybackOnStatus", (ULONG)status);
        NauLog(L"JackInserted", NauJackInserted(DevCtx));
        NauSnapshot(DevCtx);                 /* register state while playing */
        if (NT_SUCCESS(status)) DevCtx->Streaming = TRUE;
    } else {
        if (!DevCtx->Streaming) return STATUS_SUCCESS;
        status = NauPlaybackOff(DevCtx);
        NauLog(L"PlaybackOffStatus", (ULONG)status);
        DevCtx->Streaming = FALSE;
    }

    return status;
}

/* =========================================================================
 * Rt5682S_SetCaptureState — Enable / Disable Headset Mic ADC
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt5682S_SetCaptureState(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx,
    _In_ BOOLEAN Enable
    )
{
    NTSTATUS status;
    PAGED_CODE();
    if (!DevCtx->SpbTarget) return STATUS_INVALID_DEVICE_STATE;

    if (Enable) {
        if (DevCtx->CaptureActive) return STATUS_SUCCESS;
        if (!DevCtx->HwInitDone) {
            status = Rt5682S_InitializeHardware(DevCtx);
            if (!NT_SUCCESS(status)) return status;
        }
        if (!DevCtx->MicPresent) return STATUS_DEVICE_NOT_CONNECTED;

        if (!DevCtx->Streaming) {
            status = NauStreamClockOn(DevCtx);
            if (!NT_SUCCESS(status)) return status;
        }

        /* Keep the SAR-selected CTIA/OMTP microphone bias route active. */
        status = Rt5682S_SetButtonDetection(DevCtx, TRUE);
        if (!NT_SUCCESS(status)) return status;
        status = Rt5682S_SarPowerNormal(DevCtx);
        if (!NT_SUCCESS(status)) return status;

        /* Linux rt5682s_headset_detect() / DAPM supplies: VREF1+2, MB, FV1+2,
         * MICBIAS LDOs and the combo-jack block. */
        NAU_UPD(RT_PWR_ANLG_1,
                RT_PWR_VREF1 | RT_PWR_VREF2 | RT_PWR_MB,
                RT_PWR_VREF1 | RT_PWR_VREF2 | RT_PWR_MB);
        NAU_UPD(RT_PWR_ANLG_1, RT_PWR_FV1 | RT_PWR_FV2, 0);
        NauSleepMs(15);
        NAU_UPD(RT_PWR_ANLG_1, RT_PWR_FV1 | RT_PWR_FV2,
                RT_PWR_FV1 | RT_PWR_FV2);
        NAU_UPD(RT_PWR_ANLG_3,
                RT_PWR_LDO_MB1 | RT_PWR_LDO_MB2 | RT_PWR_CBJ,
                RT_PWR_LDO_MB1 | RT_PWR_LDO_MB2 | RT_PWR_CBJ);
        NAU_UPD(RT_PWR_ANLG_2, RT_PWR_MB1 | RT_PWR_MB2,
                RT_PWR_MB1 | RT_PWR_MB2);
        NauSleepMs(15);

        /* RECMIX1L/R power and the CBJ boost switches into both mixers. */
        NAU_UPD(RT_CAL_REC, RT_RECMIX_POWER_MASK, RT_RECMIX_POWER_MASK);
        NAU_UPD(RT_REC_MIXER, RT_CBJ_REC_MIX_MASK, RT_CBJ_REC_MIX_MASK);

        /* ADC1 clock/filter, ADC1 L/R and the Stereo1 ADC filter. */
        NAU_UPD(RT_CHOP_ADC, RT_CKGEN_ADC1, RT_CKGEN_ADC1);
        NAU_UPD(RT_PWR_DIG_2, RT_PWR_ADC_S1F, RT_PWR_ADC_S1F);
        NAU_UPD(RT_PWR_DIG_1, RT_PWR_ADC_L1 | RT_PWR_ADC_R1,
                RT_PWR_ADC_L1 | RT_PWR_ADC_R1);
        NauSleepMs(40);  /* pdata.amic_delay */

        /* Route ADC1 into Stereo1 ADC MIXL/MIXR and unmute both channels. */
        NAU_UPD(RT_STO1_ADC_MIXER, RT_STO1_ADC_MIX_ROUTE_MASK,
                RT_STO1_ADC_MIX_ROUTE_MASK);
        NAU_UPD(RT_STO1_ADC_DIG_VOL, RT_L_MUTE | RT_R_MUTE, 0);

        DevCtx->CaptureActive = TRUE;
        NauLog(L"CaptureOnStatus", 0);
        NauLogReg(DevCtx, L"Capture_R26_STO1_ADC_MIXER", RT_STO1_ADC_MIXER);
        NauLogReg(DevCtx, L"Capture_R3C_REC_MIXER", RT_REC_MIXER);
        NauLogReg(DevCtx, L"Capture_R44_CAL_REC", RT_CAL_REC);
        NauLogReg(DevCtx, L"Capture_R61_PWR_DIG_1", RT_PWR_DIG_1);
        NauLogReg(DevCtx, L"Capture_R62_PWR_DIG_2", RT_PWR_DIG_2);
        NauLogReg(DevCtx, L"Capture_R64_PWR_ANLG_2", RT_PWR_ANLG_2);
        NauLogReg(DevCtx, L"Capture_R65_PWR_ANLG_3", RT_PWR_ANLG_3);
        NauLogReg(DevCtx, L"Capture_R13B_CHOP_ADC", RT_CHOP_ADC);
        NauLogReg(DevCtx, L"Capture_R1C_ADC_DIG_VOL", RT_STO1_ADC_DIG_VOL);
    } else {
        if (!DevCtx->CaptureActive) return STATUS_SUCCESS;

        /* Mute first, then tear down in reverse DAPM order. */
        NAU_UPD(RT_STO1_ADC_DIG_VOL, RT_L_MUTE | RT_R_MUTE,
                RT_L_MUTE | RT_R_MUTE);
        NAU_UPD(RT_STO1_ADC_MIXER, RT_STO1_ADC_MIX_ROUTE_MASK, 0);
        NAU_UPD(RT_PWR_DIG_1, RT_PWR_ADC_L1 | RT_PWR_ADC_R1, 0);
        NAU_UPD(RT_PWR_DIG_2, RT_PWR_ADC_S1F, 0);
        NAU_UPD(RT_CHOP_ADC, RT_CKGEN_ADC1, 0);
        NAU_UPD(RT_CAL_REC, RT_RECMIX_POWER_MASK, 0);
        NAU_UPD(RT_REC_MIXER, RT_CBJ_REC_MIX_MASK, 0);
        NAU_UPD(RT_PWR_ANLG_2, RT_PWR_MB1 | RT_PWR_MB2, 0);
        NAU_UPD(RT_PWR_ANLG_3, RT_PWR_LDO_MB1 | RT_PWR_LDO_MB2, 0);

        status = Rt5682S_SetButtonDetection(DevCtx, FALSE);
        if (!NT_SUCCESS(status)) return status;

        DevCtx->CaptureActive = FALSE;
        if (!DevCtx->Streaming) {
            status = NauSysclkIdle(DevCtx);
            if (!NT_SUCCESS(status)) return status;
        }
        NauLog(L"CaptureOffStatus", 0);
    }

    return STATUS_SUCCESS;
}

/* =========================================================================
 * Rt5682S_SetVolume — DAC mute/unmute
 *
 * Loudness is owned by the SOF DSP volume pipeline.  The codec DAC is kept at
 * 0 dB (0xFE/0xFE) and only the per-channel mute bits (bit 8 / bit 0 of
 * DAC1_DIG_VOL) are toggled.  The legacy HSVOL register (0x32) does not exist
 * on the RTL5682S and must not be written.
 * ========================================================================= */

_Use_decl_annotations_
NTSTATUS
Rt5682S_SetVolume(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx,
    _In_ UINT8 VolumeStep,
    _In_ BOOLEAN Mute
    )
{
    PAGED_CODE();

    if (!DevCtx->SpbTarget) return STATUS_INVALID_DEVICE_STATE;

    DevCtx->VolumeStep = VolumeStep;
    DevCtx->Muted      = Mute;

    return I2cUpdateBits16(DevCtx->SpbTarget, RT_DAC1_DIG_VOL,
        RT_DAC_L1_VOL_MASK | RT_DAC_R1_VOL_MASK,
        (UINT16)(Mute ? RT_DAC_VOL_MUTE :
                 ((RT_DAC_VOL_0DB << 8) | RT_DAC_VOL_0DB)));
}

/* =========================================================================
 * Rt5682S_ProcessJackStateChange — Read SAR ADC & Update Audio Subsystem
 * ========================================================================= */

_Use_decl_annotations_
VOID
Rt5682S_ProcessJackStateChange(
    _In_ PRT5682S_DEVICE_CONTEXT DevCtx
    )
{
    BOOLEAN inserted = FALSE;
    UINT16 raw = 0;
    BOOLEAN changed = FALSE;
    BOOLEAN micPresent = FALSE;
    CSAUDIO_JACK_STATE newJackState;
    NTSTATUS detectStatus = STATUS_SUCCESS;

    PAGED_CODE();

    if (!DevCtx->SpbTarget || !DevCtx->JackLock) return;

    WdfWaitLockAcquire(DevCtx->JackLock, NULL);

    if (!NauJackRead(DevCtx, &inserted, &raw)) {
        WdfWaitLockRelease(DevCtx->JackLock);
        return;
    }

    if (!DevCtx->JackKnown) {
        /* first reading after power-up: take it as is */
        DevCtx->JackKnown = TRUE;
        DevCtx->JackInserted = inserted;
        DevCtx->JackCandidate = 0xFF;
        changed = TRUE;
    } else if (inserted == DevCtx->JackInserted) {
        DevCtx->JackCandidate = 0xFF;          /* bounce went away */
    } else if (DevCtx->JackCandidate == (UINT8)inserted) {
        /* same new level on two polls in a row -> accept */
        DevCtx->JackInserted = inserted;
        DevCtx->JackCandidate = 0xFF;
        changed = TRUE;
    } else {
        DevCtx->JackCandidate = (UINT8)inserted;
    }

    if (!changed) {
        WdfWaitLockRelease(DevCtx->JackLock);
        return;
    }

    if (inserted) {
        detectStatus = NauDetectHeadsetMic(DevCtx, &micPresent);
        if (!NT_SUCCESS(detectStatus)) {
            micPresent = FALSE;
            NauLog(L"MicDetectStatus", (ULONG)detectStatus);
        }
        newJackState = micPresent ? CSAUDIO_JACK_HEADSET_4POLE
                                  : CSAUDIO_JACK_HEADPHONES_3POLE;
    } else {
        detectStatus = NauDisableHeadsetMic(DevCtx);
        newJackState = CSAUDIO_JACK_UNPLUGGED;
    }
    DevCtx->MicPresent = micPresent;
    DevCtx->CurrentJackState = newJackState;
    DevCtx->JackChanges++;
    WdfWaitLockRelease(DevCtx->JackLock);

    NauLog(L"JackRaw", raw);
    NauLog(L"JackInserted", inserted);
    NauLog(L"JackChanges", DevCtx->JackChanges);
    NauLog(L"JackActiveLow", DevCtx->JackActiveLow);
    NauLog(L"MicPresent", micPresent);
    NauLog(L"JackState", newJackState);
    NauLog(L"MicDetectStatus", (ULONG)detectStatus);

    /* Complete all pending notification requests (CsAudioEndpointSwitcher) */
    if (DevCtx->JackNotificationQueue) {
        WDFREQUEST notifyRequest;
        while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(DevCtx->JackNotificationQueue, &notifyRequest))) {
            PCSAUDIO_JACK_STATE pOutState;
            NTSTATUS status = WdfRequestRetrieveOutputBuffer(notifyRequest, sizeof(CSAUDIO_JACK_STATE), (PVOID*)&pOutState, NULL);
            if (NT_SUCCESS(status)) {
                *pOutState = newJackState;
                WdfRequestCompleteWithInformation(notifyRequest, STATUS_SUCCESS, sizeof(CSAUDIO_JACK_STATE));
            } else {
                WdfRequestComplete(notifyRequest, status);
            }
        }
    }
}

/* Periodic jack poll (passive level). */
VOID
Rt5682S_EvtJackTimer(
    _In_ WDFTIMER Timer
    )
{
    WDFDEVICE device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    PRT5682S_DEVICE_CONTEXT devCtx = GetRt5682SContext(device);
    if (devCtx->JackPollStop) return;
    if (devCtx->PowerActive) {
        Rt5682S_ProcessJackStateChange(devCtx);
    }
    if (!devCtx->JackPollStop) {
        WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_MS(NAU_JACK_POLL_MS));
    }
}

/* =========================================================================
 * Interrupt Callbacks
 * ========================================================================= */

_Use_decl_annotations_
BOOLEAN
Rt5682S_EvtInterruptIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG MessageID
    )
{
    UNREFERENCED_PARAMETER(MessageID);

    /* Queue passive-level work item since reading/clearing RT5682S registers
     * requires I2C bus transactions which must run at PASSIVE_LEVEL */
    WdfInterruptQueueWorkItemForIsr(Interrupt);

    return TRUE;
}

_Use_decl_annotations_
VOID
Rt5682S_EvtInterruptWorkItem(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFOBJECT AssociatedObject
    )
{
    WDFDEVICE device = (WDFDEVICE)AssociatedObject;
    PRT5682S_DEVICE_CONTEXT devCtx = GetRt5682SContext(device);

    UNREFERENCED_PARAMETER(Interrupt);
    PAGED_CODE();

    KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
        "Rt5682SAudio: EvtInterruptWorkItem triggered\n"));

    /* Do not clear codec status here: the polling state machine consumes the
     * headset-completion bit after reading GENERAL_STATUS. Clearing it first
     * races automatic CTIA/OMTP classification. */
    Rt5682S_ProcessJackStateChange(devCtx);
}

/* =========================================================================
 * EvtIoDeviceControl — IOCTL Dispatch
 * ========================================================================= */

_Use_decl_annotations_
VOID
Rt5682S_EvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PRT5682S_DEVICE_CONTEXT devCtx = GetRt5682SContext(device);
    NTSTATUS status = STATUS_SUCCESS;
    size_t bytesReturned = 0;

    UNREFERENCED_PARAMETER(OutputBufferLength);

    switch (IoControlCode) {

    case IOCTL_CSAUDIO_REGISTER_CODEC: {
        PCSAUDIO_REGISTER_PARAMS params;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CSAUDIO_REGISTER_PARAMS), (PVOID*)&params, NULL);
        if (NT_SUCCESS(status)) {
            params->CodecType = CSAUDIO_CODEC_TYPE_HEADSET;
            params->SupportedEndpoints = CSAUDIO_ENDPOINT_HEADPHONE | CSAUDIO_ENDPOINT_HEADSET_MIC;
            RtlStringCbCopyA(params->DeviceName, sizeof(params->DeviceName), "Realtek RTL5682S Audio Codec & Jack Detect");
            bytesReturned = sizeof(CSAUDIO_REGISTER_PARAMS);
        }
        break;
    }

    case IOCTL_CSAUDIO_STREAM_START: {
        PCSAUDIO_STREAM_CONFIG config = NULL;
        BOOLEAN capture = FALSE;
        if (InputBufferLength >= sizeof(CSAUDIO_STREAM_CONFIG) &&
            NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(CSAUDIO_STREAM_CONFIG),
                                                      (PVOID*)&config, NULL))) {
            capture = (config->EndpointType == CSAudioEndpointTypeMicJack);
        }
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
            "Rt5682SAudio: IOCTL_CSAUDIO_STREAM_START received (%s)\n",
            capture ? "capture" : "playback"));
        status = capture ? Rt5682S_SetCaptureState(devCtx, TRUE)
                         : Rt5682S_SetPlaybackState(devCtx, TRUE);
        break;
    }

    case IOCTL_CSAUDIO_STREAM_STOP: {
        PCSAUDIO_STREAM_CONFIG config = NULL;
        BOOLEAN capture = FALSE;
        if (InputBufferLength >= sizeof(CSAUDIO_STREAM_CONFIG) &&
            NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(CSAUDIO_STREAM_CONFIG),
                                                      (PVOID*)&config, NULL))) {
            capture = (config->EndpointType == CSAudioEndpointTypeMicJack);
        }
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
            "Rt5682SAudio: IOCTL_CSAUDIO_STREAM_STOP received (%s)\n",
            capture ? "capture" : "playback"));
        status = capture ? Rt5682S_SetCaptureState(devCtx, FALSE)
                         : Rt5682S_SetPlaybackState(devCtx, FALSE);
        break;
    }

    case IOCTL_CSAUDIO_SET_VOLUME: {
        PCSAUDIO_VOLUME_PARAMS volParams;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CSAUDIO_VOLUME_PARAMS), (PVOID*)&volParams, NULL);
        if (NT_SUCCESS(status)) {
            status = Rt5682S_SetVolume(devCtx, volParams->VolumeDbLevel, volParams->Muted);
        }
        break;
    }

    case IOCTL_CSAUDIO_GET_JACK_STATE: {
        PCSAUDIO_JACK_STATE pJackState;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(CSAUDIO_JACK_STATE), (PVOID*)&pJackState, NULL);
        if (NT_SUCCESS(status)) {
            *pJackState = devCtx->CurrentJackState;
            bytesReturned = sizeof(CSAUDIO_JACK_STATE);
        }
        break;
    }

    case IOCTL_CSAUDIO_REGISTER_JACK_NOTIFICATION: {
        /* Forward request to the manual JackNotificationQueue to be completed when a jack state changes */
        status = WdfRequestForwardToIoQueue(Request, devCtx->JackNotificationQueue);
        if (NT_SUCCESS(status)) {
            return; /* Request is pending in the queue, do not complete yet */
        }
        break;
    }

    case IOCTL_CSAUDIO_READ_REG: {
        PCSAUDIO_REG_IO regIoIn;
        PCSAUDIO_REG_IO regIoOut;
        UINT16 val16 = 0;

        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CSAUDIO_REG_IO), (PVOID*)&regIoIn, NULL);
        if (!NT_SUCCESS(status)) break;

        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(CSAUDIO_REG_IO), (PVOID*)&regIoOut, NULL);
        if (!NT_SUCCESS(status)) break;

        status = I2cReadRegister16(devCtx->SpbTarget, regIoIn->Register, &val16);
        if (NT_SUCCESS(status)) {
            regIoOut->Register = regIoIn->Register;
            regIoOut->Value = val16;
            bytesReturned = sizeof(CSAUDIO_REG_IO);
        }
        break;
    }

    case IOCTL_CSAUDIO_WRITE_REG: {
        PCSAUDIO_REG_IO regIo;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CSAUDIO_REG_IO), (PVOID*)&regIo, NULL);
        if (NT_SUCCESS(status)) {
            status = I2cWriteRegister16(devCtx->SpbTarget, regIo->Register, regIo->Value);
        }
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
