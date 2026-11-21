# RTL5682S + RT1019P port — working notes

Original repository: `cros-sofaudio-main` (MAX98373 + NAU88L25).  
Working copy: this directory (`cros-sofaudio-rtl`).

All documentation in this repository is maintained in English only.

---

## Changes already applied

- `Max98373Audio\` → `Rt1019Audio\`, `Nau8825Audio\` → `Rt5682SAudio\`.
- GUIDs, ProjectGuids, file names, and INF files renamed systematically (`MAX98373` → `RT1019`, `NAU8825` → `RT5682S`).
- INF files use the real Marasov hardware IDs:
  - `ACPI\VEN_RTL&DEV_5682`, `ACPI\RTL5682`, `*RTL5682`
  - `ACPI\VEN_RTL&DEV_1019`, `ACPI\RTL1019`, `*RTL1019`
- `installer\install.ps1` detects these IDs and no longer expects two amplifiers.
- SOF IPC3 topologies were regenerated from the v2.2.1 headers for the `sof-adl-rt1019-rt5682` topology (Marasov/CX34):
  - `sof_spk_pipeline.h`: SSP1.I2S, 19.2 MHz MCLK / 3.072 MHz BCLK / 32-bit slots.
  - `sof_hp_pipeline.h` and `sof_capture_pipelines.h`: matching SSP0 and DMIC0 settings.
- The real downstream blob `research\sof-adl-rt1019-rt5682-downstream.tplg` (WeirdTreeThing, ChromeOS/WTT) was downloaded and parsed. The `SSP1-Codec` link (BE id 7) confirms for SSP1: `mclk=19200000`, `bclk=3072000`, `fsync=48000`, `tdm_slots=2`, `tdm_slot_width=32`, `tx/rx=0x3`, `mclk_direction=1`, with tokens `SAMPLE_BITS=32`, `CLKS_CONTROL=0`, `QUIRKS=0`, `BCLK_DELAY=0`, `MCLK_ID=0`, `FRAME_PULSE_WIDTH=0`, `TDM_PADDING=0`. The earlier port used 1.536 MHz / 16 bits and was corrected.
- **`Rt1019Audio` was rewritten as a GPIO-only driver for RTL1019P**:
  - SPB/I²C support removed (`spb.h`, `acpiioct.h`, `i2c_helper.h` deleted);
  - obtains the connection ID from `CmResourceTypeConnection` with `CM_RESOURCE_CONNECTION_CLASS_GPIO` / `TYPE_GPIO_IO`;
  - opens the target with `RESOURCE_HUB_CREATE_PATH_FROM_ID`;
  - asserts the SDB pin with `IOCTL_GPIO_WRITE_PINS`;
  - waits 300 ms on first power-up (matching `rt1015p.c`);
  - `rt1019_reg.h` remains as a documented stub (there is no register map).

---

## Build and signing

**Completed.** There is no longer a dependency on reinstalling the WDK:

- WDK MSBuild integration was repaired with a user-writable shim in `build-tools\wdk-msbuild-shim\`; it does not touch Program Files or require elevation. `build.cmd` adds it to `AdditionalVCTargetsPath` automatically.
- `build.cmd` also pins `WindowsTargetPlatformVersion=10.0.26100.0`, avoiding `MSB8036` from the generic SDK version `10.0`.
- Real compile errors were fixed:
  - `Rt1019Audio.c` and `Rt5682SAudio.c`: unavailable `DECLARE_UNICODE_STRING_SIZE` macro replaced with `WCHAR[] + RtlInitEmptyUnicodeString`.
  - `CsAudioIntcsof\*.cpp` and `SklHdaudBus\nhlt.cpp`: an unfortunate global replacement had changed `Maximum` into `Realtekum` (`MaximumLength`, `SignedMaximum`, `MaximumChannels`, …).
  - `CrosAudioTest` adapted to RT1019P being GPIO-only (no I²C registers).
- `build-tools\15-crear-certificado-usuario.ps1` creates the test certificate in `CurrentUser\My` and updates the repository `.cer` files so signing can be done without administrator rights.
- `build.cmd` result: **0 errors, 0 warnings**, 10 files signed and verified (4 `.sys`, 4 `.cat`, 2 tools).
- The Local Machine certificate requires elevation for `signtool`; the elevated build is launched with `Start-Process -Verb RunAs`.
- The private `.pfx` key is never included in the repository or the distribution package. Only the public `.cer` certificate is published.

Verification commands:

- `build.cmd` — builds, generates catalogs, signs, and verifies.

---

## Silent output diagnosis (resolved)

- DSP, DMA, and all four pipelines start without IPC timeouts. All messages, including `DAI_CONFIG SSP1 HW_PARAMS` and `STREAM_TRIG_START`, return `fw err 0, status 0x00000000`, and `SD_LPIB` advances.
- Comparing the generated `DAI_CONFIG` with the real downstream blob revealed an important SSP1 mismatch: 1.536 MHz / 16-bit slots versus 3.072 MHz / 32-bit slots in the blob. This was corrected and regenerated.
- Instrumentation added:
  - `Rt1019Audio`: logs `GpioIdLow/High`, `SdbWriteValue`, `SdbReadStatus`, and `SdbReadLevel` under `...\Amp0` via `IOCTL_GPIO_READ_PINS`.
  - `CsAudioIntcsof`: logs all fields of the `DAI_CONFIG` payload sent during setup and in `HW_PARAMS` under `...\DspIpc`.
- **Confirmed on hardware (20/11/2026):** after rebooting to load the `18.4` drivers, the 1 kHz tone is heard correctly. The reboot was necessary because Windows had reused old `oem73`–`oem76` packages with `18.3` binaries; only after loading `oem77`–`oem80` (`csaudiointcsof 18.4.3.0`, `rt1019audio`/`rt5682saudio 18.4.0.0`) was the corrected `DAI_CONFIG` applied. Verified: `Amp0\GpioIdLow=3`, `SdbWriteValue=1` during the tone, `DspIpc\SpkSetup07DaiBclkHz=3072000`, `SpkStreamDaiBclkHz=3072000`, `IpcTimeouts=0`.

---

## Known risk

The remaining risk is Windows support for RTL5682S (jack detection and I²C) and whether the ChromeOS downstream topology matches the installed SOF IPC3 firmware (`10.35.0.9915`). The amplifier is no longer a blocker: RT1019P only needs its SDB GPIO, which the real DSDT declares.

---

## Packaging and distribution (20/11/2026)

- Documentation is maintained in English only: `README.md`, `INSTALL_GUIDE.md`, `installer\README.txt`, and `RELEASE_NOTES.md`.
- The WDK shim was copied to `build-tools\wdk-msbuild-shim\` and `build.cmd` uses it before the older `..\tools\...` path, so the repository is self-contained.
- Installer texts that still mentioned "volmar"/"2x RT1019" were corrected to Marasov/CX34 and RT1019P GPIO-only.
- `package.cmd v1.3.5` generates `dist\cros-sofaudio-v1.3.5\` and `cros-sofaudio-v1.3.5.zip` with `rt5682saudio 18.5.2.0` and the remaining 18.4.x binaries, plus `SHA256SUMS.txt`.
- Verified ZIP: 38 entries, 0 private keys, 10 signatures verified with `ChromebookAudio.cer`, `VERSION.txt=v1.3.5`.
- The private `.pfx` key stays outside the repository; only the public `.cer` certificate is published.

---

## Changes in v1.3.1 (20/11/2026)

- `rt5682s_seq.h` and `Rt5682SAudio.c` stopped using the NAU88L25 register map for audio routing. The map was validated against Linux v6.6 (`rt5682s.c` / `rt5682s.h`) and CoolStar: reset by writing `0`, mute in `DAC1_DIG_VOL` bits 8/0, neutral gain `0x8080` in `HP_CTRL_1`, `TDM_ADDA_CTRL_1=0x8000`, `CTRL_2=0x0080`.
- Capture: VREF/MB/CBJ power, `RECMIX1L/R`, ADC1 clock/filter, ADC L/R, and `STO1_ADC_MIXER` with `STO1_ADC_DIG_VOL` unmute; the SAR-detected CTIA/OMTP selection is preserved.
- Jack: reads `RT_AJD1_CTRL` / `RT_JDH_RS_MASK` and sends the `CSAUDIO_JACK` event. The switcher now filters by `PKEY_Device_DeviceDesc` and instance properties in addition to `FriendlyName`.
- `Rt5682SAudio` moved to `18.5.0.0` so Windows replaces the `18.4.0.0` package in the Driver Store.
- Confirmed on hardware: speakers and jack work. Microphone still pending. The speaker pipeline (SSP1/RT1019P) was not touched.

---

## Changes in v1.3.2 (20/11/2026)

- `NauDetectHeadsetMic()` retries reading `CBJ_CTRL_2[1:0]` while the result is neither OMTP (`1`) nor CTIA (`2`). Previously it only retried while the value was `0`, so an intermediate `3` made the connector be classified as 3-pole headphones and the switcher hid the headset "Microphone" endpoint.
- Any nonzero value is now treated as a headset; the ambiguous `3` case applies CTIA bias (MB2). A 3-pole TRS plug only produces silence in that endpoint, without affecting playback.
- `Rt5682SAudio` moved to `18.5.1.0` to force Driver Store replacement.
- The internal microphone (DMIC0 array) delivers real audio; the TRRS jack microphone remains under investigation.

---

## Changes in v1.3.3 (20/11/2026)

- Added `Rt5682S_SarPowerNormal()`, equivalent to `rt5682s_sar_power_mode(SAR_PWR_NORMAL)` in CoolStar/Linux: `CBJ_CTRL_3` enables the input buffer, `CBJ_CTRL_1` puts MB1/MB2 under FSM control, `SAR_IL_CMD_1` switches to automatic control, waits 5 ms, and enables `BUTDET` at normal power. Called after headset detection and when capture opens.
- `Rt5682SAudio` moved to `18.5.2.0` to replace the installed `18.5.1.0`. The SSP1/RT1019P speaker pipeline was not touched.
- Measurement with TRRS inserted: internal array real signal (`rms≈79571`, `7.7 dBFS`), but jack "Microphone" at noise level (`rms≈18`, `-65 dBFS`). Manual OMTP/CTIA bias and live I²S word widths (`0x3330`, `0x2220`, `0x1110`, `0x0000`) did not restore the signal, so the cause is not just bias or codec format.
- Pending at that point: validate on hardware. If silence persisted, compare the SSP0 `DAI_CONFIG` in `gen_capture_pipelines.c` with the real downstream topology and try a different TRRS headset to rule out cable/mic/board.

---

## Changes in v1.3.4 (20/11/2026)

- Live measurement with v1.3.3 installed confirmed that the "no microphone works" report was a **default-routing** problem: the internal array produced real signal (`rms≈70591`, peak `241905`), but the switcher set the silent jack endpoint as the capture default (`-94.7 dBFS`).
- `csaudioendpointswitcher.cpp`: with a TRRS jack inserted, render still goes to *Headphones*, but the capture default stays on *Multiple microphones* for Multimedia, Communications, and Console. The jack "Microphone" endpoint remains visible for manual selection and further diagnosis; with a 3-pole TRS plug it is hidden as before.
- No `.sys` files were modified: `Rt5682SAudio` remains at `18.5.2.0` and the SSP1/RT1019P speaker pipeline is intact.
- Added `tests\Test-AudioDefault.cpp` to verify default render/capture endpoints and roles. It also revealed `VolumenScalar=1.0` / `VolumenDb=+30.00` on the capture endpoint, explaining why internal-array floats reach `7.38` (out of range). This behavior was not changed in v1.3.4; lower the Windows recording level if distortion is audible.
- Documentation is now English-only; the Spanish files and the duplicate `README.en.md` were removed.

---

## Changes in v1.3.5 (21/11/2026)

- Live capture with v1.3.4 installed proved that the DMIC0 stream itself is healthy, but the Windows capture endpoint was at `1.0` / `+30.00 dB`. At that level the float samples had a large positive DC offset (`mean≈2.14`) and peaks above `7.0`; `~89%` of samples were out of range, so applications effectively received a clipped flat line.
- A sweep confirmed the clean operating point: `0.50` gave `ac_rms≈0.031`, `0.70` gave `ac_rms≈0.066` with no clipping, `0.80` was still clean but had more DC, and `0.90+` clipped again. `0.70` was chosen as the safe automatic level.
- `csaudioendpointswitcher.cpp`: added `NormalizeMicArrayVolume()` using `IAudioEndpointVolume` on the cached internal-array endpoint ID. It runs after routing on startup and on jack events. If the scalar is above `0.81`, it sets `0.70` and logs the old/new scalar and dB values. Lower user-selected levels are not overridden.
- Added `endpointvolume.h` and `uuid.lib` to the switcher project/build. No `.sys`, topology, INF, or DriverVer changes were needed; the SSP1/RT1019P speaker pipeline is untouched.
- Live verification after replacing the service: default capture remained *Multiple microphones* at `0.7000` / `+5.49 dB`; WASAPI capture reported `rms≈1998.5` (`-24.3 dBFS`), peak `14360` (`-7.2 dBFS`), and `0` out-of-range samples.
- Added local diagnostic helpers under `tests\`: `Set-CaptureVolume.cpp` and `Analyze-Mic.ps1`. They are development tools only and are not copied into the release ZIP.
