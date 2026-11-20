# cros-sofaudio-rtl — Internal Audio for Chromebooks on Windows 11 (SOF / RTL5682S + RT1019P)

[![Platform](https://img.shields.io/badge/Platform-Windows%2011%20x64-0078D6.svg)](https://www.microsoft.com/windows)
[![Hardware](https://img.shields.io/badge/Hardware-Marasov%20%7C%20CX34-orange.svg)](#supported-hardware)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Open-source Windows drivers that enable real internal audio on Intel Chromebooks through the **Intel Sound Open Firmware (SOF) DSP**. This working copy (`cros-sofaudio-rtl`) is ported to the **Google Marasov / ASUS Chromebook Plus CX34**.

**Status: speakers and headphone jack verified; headset microphone still under investigation.** Speaker output works with SOF firmware `10.35.0.9915` and drivers `18.4.x`/`18.5.x`. The decisive fix was aligning the `SSP1` `DAI_CONFIG` with the real downstream blob: **19.2 MHz MCLK / 3.072 MHz BCLK / 32-bit slots / 48 kHz**. `v1.3.2` corrected TRRS detection and the RTL5682S register map; `v1.3.3` added the full `SAR_PWR_NORMAL` sequence from Linux v6.6 / CoolStar; `v1.3.4` fixes the default capture routing so applications use the internal microphone array, which does produce audio, instead of the silent headset jack microphone. Live tests ruled out microphone bias polarity and I²S word width as the sole cause of the jack silence.

> [!IMPORTANT]
> These drivers are **test-signed** with a test certificate. They are not WHQL-certified. Windows requires **Test Mode**, **Secure Boot disabled**, and trust in `ChromebookAudio.cer`. The installer automates everything except disabling Secure Boot in the UEFI firmware.

---

## Supported hardware

| Component | ID / detail |
|---|---|
| Device | Google Marasov, ASUS Chromebook Plus CX34 |
| CPU / DSP | Intel Core i3-1215U (Alder Lake-P), DSP `PCI\VEN_8086&DEV_51C8` |
| Headset codec | `ACPI\RTL5682S\0` — RTL5682S, I²C + jack GPIO |
| Speaker amplifier | `ACPI\RTL1019\0` — **RT1019P**, SDB GPIO only (no I²C) |
| Internal microphones | PDM microphones on DSP `DMIC0` (not routed through RTL5682S) |
| Firmware | coreboot/UEFI from MrChromebox; SOF `10.35.0.9915` (IPC3) |

The installer checks these IDs and stops if the hardware does not match. Other models with RTL5682S + RT1019 with I²C require adapting the INF files and the amplifier sidecar.

---

## Quick installation

1. Download `cros-sofaudio-v1.3.4.zip` from **Releases**.
2. Extract the entire archive to a local folder (for example `Desktop\CrosSofAudio`).
3. Run **`Install.cmd`** as administrator (it elevates itself).
4. If prompted for Test Mode, answer **Y** and **reboot**. After the reboot, run `Install.cmd` again.
5. **Reboot** once more. The *Speakers* and *Headphones* endpoints should appear.

To uninstall, run **`Uninstall.cmd`** as administrator and reboot.

Detailed guide: [`INSTALL_GUIDE.md`](INSTALL_GUIDE.md) · [`README.txt`](installer/README.txt) included in the package.

---

## Package contents

```
Install.cmd / install.ps1          installer (verifies signatures, trusts the certificate, installs)
Uninstall.cmd / uninstall.ps1      uninstaller (restores the vulnerable-driver blocklist)
Collect-Logs.cmd                   collects audio-log.txt for diagnostics
ChromebookAudio.cer                public test certificate (contains no private key)
Drivers\SklHdaudBus                Intel AudioDSP / HD Audio bus (CoolStar)
Drivers\CsAudioIntcsof             WaveRT, SOF boot, sof-*.ri firmware
Drivers\Rt1019Audio                RT1019P speaker amplifier (SDB GPIO only)
Drivers\Rt5682SAudio               RTL5682S codec, jack detect, TRRS microphone
Switcher\csaudioendpointswitcher   automatic speaker/headphone and capture-default switching
Tools\cros_audio_test.exe          codec, jack, and stream diagnostic utility
```

### Architecture

```
WASAPI / PortCls (Windows Audio)
        |
CsAudioIntcsof.sys  (WaveRT, 4 pipelines: speakers, headphones, DMIC, mic jack)
        |  ADSP_BUS_INTERFACE / DMA
SklHdaudBus.sys     (PCI 8086:51C8, NHLT, DMA)
        |
  +-----+------------------+
  | SSP1.OUT -> RT1019P    |  SDB GPIO (no I²C)
  | SSP0     -> RTL5682S   |  I²C + jack IRQ
  | DMIC0    -> microphones|  internal PDM array
```

The sidecar drivers `Rt1019Audio.sys` and `Rt5682SAudio.sys` receive power, volume, and jack-detection commands through CoolStar's `csaudio` interface. The amplifier is disabled (`SDB = 0`) when no stream is active.

---

## Audio endpoints and default routing

Four capture/render endpoints are exposed:

| Endpoint | Flow | Source | Status |
|---|---|---|---|
| Speakers (*Altavoces*) | Render | SSP1 → RT1019P | Verified |
| Headphones (*Auriculares*) | Render | SSP0 → RTL5682S | Verified |
| Microphone array | Capture | DMIC0 PDM array | Working; capture default |
| Headset microphone | Capture | SSP0 → RTL5682S TRRS jack | Silent; visible for diagnosis |

Since `v1.3.4`, the switcher keeps *Multiple microphones* as the default capture device for the Multimedia, Communications, and Console roles in every jack state. The headset microphone remains visible so it can be selected manually, but it is no longer the default. This prevents ordinary applications from silently opening the dead endpoint.

---

## Building from source

Requirements: Windows 11 x64, Visual Studio 2022 (v143), WDK/SDK `10.0.26100.0`, KMDF 1.33, and a test-signing key.

```cmd
build.cmd
```

The script builds `Release|x64`, generates catalogs with `Inf2Cat`, signs all 10 outputs, and verifies the signatures. The WDK shim is included in `build-tools\wdk-msbuild-shim\`, so no changes under `Program Files` or elevation are needed to compile. Signing with the Local Machine certificate store does require elevation; the installer/build scripts request it when needed.

To create the installable package:

```cmd
package.cmd v1.3.4
```

The GitHub Actions workflow (`.github/workflows/build.yml`) builds and signs with a PFX stored as repository secrets (`DRIVER_SIGNING_PFX_BASE64` / `DRIVER_SIGNING_PFX_PASSWORD`). The private key is never committed to the repository. The artifacts are still test-signed and require Test Mode.

---

## Tuning and diagnostics

Values are stored in `HKLM\SOFTWARE\CrosSofAudio` (DWORD, reboot to apply):

| Value | Default | Meaning |
|---|---|---|
| `SpeakerDspAttenDb` | 2 | speaker attenuation in dB |
| `SpeakerLimiterThresholdDb` | 2 | peak limiter headroom |
| `JackActiveLow` | 1 | jack GPIO polarity |
| `MicArrayDspAttenDb` | 0 | internal microphone DSP attenuation in dB |
| `HeadsetMicDspAttenDb` | 0 | headset microphone DSP attenuation in dB |

Run `Collect-Logs.cmd` and review `audio-log.txt`. Indicators of a healthy boot:

- `DspBoot\Stage = 10`, `Status = 0`, `RomStatus = 5`, `RomError = 0`
- `DspIpc\IpcTimeouts = 0`, `DspIpc\SpkStreamDaiBclkHz = 3072000`
- `Amp0\SdbWriteValue = 1` while audio is playing
- `Codec\VendorIdReg = 0x10EC` and `Codec\DeviceIdReg = 0x6749` (real RTL5682S)

Historical diagnosis notes: [`PORT_NOTES.md`](PORT_NOTES.md) and [`REVERSE_ENGINEERING_SPEC.md`](REVERSE_ENGINEERING_SPEC.md).

**Known limitations:** this port uses SOF IPC3 with generic ADL firmware. `v1.3.4` keeps the RTL5682S register map, endpoint filtering by `DeviceDesc`/`InstanceId`, and `SAR_PWR_NORMAL`, and makes the internal microphone the capture default. The TRRS headset microphone still produces no signal and remains under investigation. The capture endpoint is reported at `+30 dB`, so captured floats can exceed 1.0; lower the recording level in Windows if distortion is audible. Jack detection may need adjustment on boards other than Marasov/CX34. No Linux SOF stack has been ported, and no paid CoolStar components are used.

---

## Credits

- **CoolStar** — `sklhdaudbus`, the `csaudio` architecture, and ADSP/NHLT reverse engineering. See [`CREDITS.md`](CREDITS.md).
- **isaiahscape** — original `cros-sofaudio` project.
- **Kier (@3kierz)** and **Claude** — diagnosis and correction of the audio output.
- **MrChromebox** — coreboot/UEFI firmware.
- **SOF Project / Linux ASoC / Intel** — topologies, sequences, and firmware.

## License

See [`LICENSE`](LICENSE) and [`LICENCE.sof-firmware`](LICENCE.sof-firmware).
