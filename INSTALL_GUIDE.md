# Chromebook Audio Driver Installation & Deployment Guide
## Windows 11 SOF AudioDSP + RT1019P + RTL5682S

> **Credits:** Built upon foundational reverse-engineering and source code by **CoolStar** ([@coolstarorg](https://github.com/coolstar)), creator of `sklhdaudbus` and the `csaudio` architecture.

This guide applies to package `cros-sofaudio-v1.3.7.zip`. Installation takes a few minutes and requires **two reboots**.

---

## 1. Prerequisites

1. **Supported hardware:** Google Marasov / ASUS Chromebook Plus CX34 (Intel Core i3-1215U, Alder Lake-P, DSP `8086:51C8`) with RTL5682S and RT1019P. The installer verifies the hardware IDs and stops on a mismatch.
2. **Custom UEFI firmware:** full UEFI/coreboot firmware must be flashed (for example, MrChromebox) with ChromeOS write-protect disabled.
3. **Windows 11 x64** with Secure Boot disabled. Windows may refuse to enable Test Mode while Secure Boot is active.
4. **Test Mode:** the drivers use a self-issued test certificate, so Windows must run with `testsigning on`.
5. **Vulnerable driver blocklist:** `Install.cmd` disables the Microsoft vulnerable-driver blocklist after verifying every package signature. The previous value is saved and restored by `Uninstall.cmd`. A reboot is required for Test Mode and the blocklist change.

These drivers are **not WHQL-certified by Microsoft**. Official WHQL signing requires an EV code-signing certificate, a Microsoft Partner Center account, and paid validation/attestation. Those recurring costs are outside the scope of this free project, so Test Mode is mandatory. The installer imports `ChromebookAudio.cer` into `Root` and `TrustedPublisher` and trusts the test signature.

### Enable Windows Test Mode manually

`Install.cmd` can enable Test Mode when you answer **Y**. If you need to do it yourself, or if the setting was not applied:

1. Disable **Secure Boot** in the UEFI firmware setup. On Marasov/CX34 with MrChromebox or coreboot, enter the firmware setup and turn Secure Boot off. Keep it disabled while these drivers are installed.
2. Open **Start**, type `cmd`, right-click **Command Prompt**, and choose **Run as administrator**.
3. Run:

   ```cmd
   bcdedit /set testsigning on
   ```

4. Reboot the Chromebook.
5. Verify the setting in an administrator Command Prompt:

   ```cmd
   bcdedit /enum {current} | findstr testsigning
   ```

   It must report `testsigning Yes`. The desktop should also show a **Test Mode Windows** watermark.
6. Run `Install.cmd` from the extracted package as administrator.

To restore a normal Windows boot, run `bcdedit /set testsigning off` as administrator, re-enable Secure Boot if desired, and reboot. Run `Uninstall.cmd` first if the drivers are no longer needed.

---

## 2. Installation steps

1. Copy the ZIP to the Chromebook and **extract the entire archive**. Do not run `Install.cmd` from inside the ZIP.
2. Double-click **`Install.cmd`** and approve the administrator prompt.
3. If asked to enable Test Mode, answer **Y** and **reboot** when requested.
4. After the reboot, run **`Install.cmd` a second time**.
5. **Reboot once more.** The *Speakers* and *Headphones* endpoints should now be available.

The installer validates signatures, trusts the certificate, configures the driver policies, installs the four drivers with `pnputil`, and registers the `csaudioswitcher` service.

Manual installation is also possible with `pnputil`:

```cmd
pnputil /add-driver ".\Drivers\SklHdaudBus\SklHdaudBus.inf" /install
pnputil /add-driver ".\Drivers\CsAudioIntcsof\csaudiointcsof.inf" /install
pnputil /add-driver ".\Drivers\Rt1019Audio\Rt1019Audio.inf" /install
pnputil /add-driver ".\Drivers\Rt5682SAudio\Rt5682SAudio.inf" /install
```

---

## 3. Verification

Open **Device Manager** (`devmgmt.msc`) and confirm:

- `Realtek RTL5682S Headset Audio Codec & Jack Detect` starts without error code 10.
- `Realtek Integrated RT1019 Smart Audio Amplifier` starts without error code 10.
- `Chromebook Intel SOF Audio (WaveRT)` endpoints appear under Sound, video and game controllers.

Check the installed driver versions:

```powershell
Get-CimInstance Win32_PnPSignedDriver |
  Where-Object { $_.DeviceName -match 'RTL5682S|SOF Audio|RT1019' } |
  Select-Object DeviceName,DriverVersion,InfName
```

Expected with v1.3.7: `rt5682saudio 18.5.2.0`, `csaudiointcsof 18.4.5.0`, `rt1019audio 18.4.0.0`.

### Capture default

Since v1.3.4, the **internal microphone array** is always the default capture device. Confirm it in **Settings → System → Sound → Input** (it may be labelled *Microphone array* or *Multiple microphones*, depending on the Windows language). If an application such as Discord or Teams was previously set to use the headset *Microphone* explicitly, change it to the internal array.

Since v1.3.5, the switcher also caps the internal microphone recording level at `0.70` (`+5.49 dB`) when the endpoint is above `0.81`. This avoids `+30 dB` clipping. v1.3.7 restores the missing DSP high-pass stage so the capped signal contains usable speech. Do not raise the slider back to 100%; the switcher will restore the safe level on its next pass.

The headset microphone endpoint remains visible for diagnostics but is silent on this hardware. The internal microphone array is the intended capture source and passed live speech validation in v1.3.7.

### Diagnostic utility

```cmd
Tools\cros_audio_test.exe
```

For a source build, the same tool is at `x64\Release\CrosAudioTest.exe`. It reports codec IDs, jack state, DSP boot status, IPC timeouts, and stream state.

`Collect-Logs.cmd` produces `audio-log.txt`. Healthy values include:

- `DspBoot\Stage = 10`, `Status = 0`, `RomStatus = 5`, `RomError = 0`
- `DspIpc\IpcTimeouts = 0`
- `DspIpc\SpkStreamDaiBclkHz = 3072000`
- `Amp0\SdbWriteValue = 1` while a tone is playing
- `Codec\VendorIdReg = 0x10EC`, `Codec\DeviceIdReg = 0x6749`

---

## 4. Settings

Registry path: `HKLM\SOFTWARE\CrosSofAudio` (DWORD values, reboot to apply).

| Value | Default | Meaning |
|---|---|---|
| `SpeakerDspAttenDb` | 2 | speaker attenuation in dB |
| `SpeakerLimiterThresholdDb` | 2 | peak limiter headroom |
| `JackActiveLow` | 1 | jack GPIO polarity |
| `MicArrayDspAttenDb` | 0 | internal microphone DSP attenuation in dB |
| `HeadsetMicDspAttenDb` | 0 | headset microphone DSP attenuation in dB |

Example to lower speaker level by 6 dB:

```cmd
reg add HKLM\SOFTWARE\CrosSofAudio /v SpeakerDspAttenDb /t REG_DWORD /d 6 /f
```

The RT1019P has no I²C volume registers. All gain and attenuation is applied in the SOF DSP pipeline (`PCM0P → volume → SSP1`); no unmeasured EQ is applied.

---

## 5. Troubleshooting

- If the jack switches in reverse, set `JackActiveLow = 0` and reboot.
- If the capture level sounds distorted, keep the Windows recording slider near `70%`. The underlying endpoint reports `+30 dB` at 100%; v1.3.5 caps it automatically, but manually raising it above `81%` can reintroduce clipping. If there is no speech signal at all, check the DSP IPC telemetry and live capture instead of changing the slider.
- To disable DSP boot: `reg add HKLM\SOFTWARE\CrosSofAudio /v EnableDspBoot /t REG_DWORD /d 0 /f`
- If the system fails to boot, enter Safe Mode and run `Uninstall.cmd`.
- `switcher.log` is written to `C:\ProgramData\CrosSofAudio\switcher.log`.

---

## 6. Uninstallation

1. Double-click **`Uninstall.cmd`** from the extracted package and approve elevation.
2. The script stops and removes the `csaudioswitcher` service, deletes all four driver packages from the Driver Store, removes `sof-*.ri` files from `C:\Windows\System32\drivers\`, restores the previous vulnerable-driver blocklist value, and rescans hardware.
3. Reboot the system.
