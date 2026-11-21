# Release notes

## Current release

**Tag:** `v1.3.7`  
**Title:** `v1.3.7 — canonical DMIC high-pass stage`

> [!IMPORTANT]
> v1.3.7 passed live speech validation on Google Marasov / ASUS Chromebook Plus CX34.
> It is signed with a self-issued test certificate, not WHQL-certified by Microsoft.
> Official WHQL signing requires an EV certificate, a Partner Center account, and paid
> validation, so Windows loads these drivers only in Test Mode with Secure Boot
> disabled and the public `ChromebookAudio.cer` trusted. See `INSTALL_GUIDE.md` for the
> step-by-step Test Mode procedure.

---

## v1.3.7 — canonical DMIC high-pass stage (21/11/2026)

v1.3.6 proved that changing PDM0 to PDM1 alone does not restore speech. A field-by-field
comparison with `sof-hda-generic-2ch-pdm1.tplg` then showed the remaining structural
difference: the canonical capture pipeline contains an EQIIR DF2T high-pass component
between the DAI buffer and the volume component. Earlier generated pipelines omitted it.

### Changes from v1.3.6

- `tools-gen/gen_capture_pipelines.c` now emits the canonical `EQIIR10.0` component in
  pipeline 4, using the 88-byte DF2T coefficient block from the SOF v2.2.1 topology.
- The DMIC graph is now `DMIC0.IN -> buffer -> EQIIR -> buffer -> volume -> buffer ->
  PCM99C`, matching the canonical three-buffer pipeline shape.
- `CsAudioIntcsof/sof_ipc.cpp` now logs DMIC-specific `DAI_CONFIG` fields (version,
  PDM clocks, FIFO rate/width, duty cycle, active PDM count, ramp, and PDM0 controls)
  instead of skipping all non-SSP DAI payloads.
- `CsAudioIntcsof` moved from `18.4.4.0` to `18.4.5.0`; `DriverVer`, `.vcxproj`
  `TimeStamp`, `build.cmd`, and `package.cmd` were bumped together.
- The SSP1/RT1019P speaker pipeline, RTL5682S jack codec, and endpoint switcher are
  unchanged.

### Live validation — passed (21/11/2026)

The package was installed elevated. Windows loaded `csaudiointcsof 18.4.5.0`; DSP boot
reached `Stage=10`, `Status=0`, `Result=SUCCESS: SOF firmware booted and sent FW_READY`.
All 17 DMIC setup IPCs returned `fw err 0` / `status 0x00000000`, including
`COMP_NEW eq-iir highpass`. DMIC telemetry confirmed PDM1, 2.4–4.8 MHz PDM clock,
48 kHz/32-bit FIFO, duty 40/60, one active PDM, 200 ms unmute ramp, and both microphones
enabled. `Disable_SysFx = 0` and the default capture was *Multiple microphones* at `0.70`.

A 20-second WASAPI capture while the user spoke continuously produced:

- `mean=0.000001`, `ac_rms=0.018311`, peak range `-0.287180` to `0.263140`
- `zero_crossings=108556` (about `2713.9 Hz`)
- `19/19` post-startup blocks above the `0.001` AC-RMS threshold
- no out-of-range samples

This is sustained speech-like alternating signal, not the fixed DC/stale output seen
with v1.3.5 and v1.3.6.

### Status

- **Speakers and headphone jack:** previously verified; no code path changed.
- **Internal microphone (DMIC0):** validated with live speech capture in v1.3.7.
- **TRRS headset microphone:** out of scope.

### Important

This is a test-signed release, not WHQL-certified. Official Microsoft certification
was not pursued because of its EV certificate and paid validation costs. Windows must
run in Test Mode with Secure Boot disabled. The ZIP contains only the public `.cer`
certificate; the private key is never published.

---

## v1.3.6 — PDM1 configuration (failed live validation; not published)

v1.3.6 changed the generated DMIC `DAI_CONFIG` from PDM0 to PDM1 to match Marasov/CX34
coreboot `mic-tplg="_2ch_pdm1"`. It was installed and tested locally, but live captures
still showed only a startup transient and fixed DC/stale samples while the user spoke.
The DSP booted successfully, all DMIC IPCs returned `fw err 0` / `status 0x00000000`,
and no IPC timeouts occurred. Therefore the PDM controller ID alone was not the root
cause. v1.3.6 was never published as a GitHub release.

---

## v1.3.5 — endpoint level cap (21/11/2026; published)

v1.3.5 added the internal microphone level cap after the endpoint was observed at
`+30 dB` maximum. The original note claimed clean capture from an `rms≈1998.5`
measurement. That measurement was invalid: the WAV samples are 32-bit float and were
parsed as integers. Correct float32 analysis showed fixed DC or stale samples with no
speech. Only the routing and level-cap changes in this release are still considered valid.

### Changes from v1.3.4

- The endpoint switcher caps the internal microphone capture level to `0.70` (about `+5.49 dB`) whenever it finds the endpoint above `0.81`; lower user-selected levels are preserved.
- The cap is applied on service startup and on every jack-state routing pass, with or without a headset inserted.
- **Correction:** the earlier RMS/peak/out-of-range claim came from incorrect WAV parsing and must not be used as evidence that the microphone worked.
- No kernel binaries changed. `rt5682saudio` remains at `18.5.2.0`; the SSP1/RT1019P speaker pipeline is untouched.

### Status

- **Speakers:** verified on Google Marasov / ASUS Chromebook Plus CX34.
- **Headphone jack:** verified on the same machine.
- **Internal microphone (DMIC0 array):** routing and level cap applied, but speech capture was never proven; later float32 analysis found DC/stale samples.
- **TRRS headset microphone:** still silent on this hardware and outside the current fix scope.

### Important

Test-signed drivers, not WHQL. The ZIP contains only the public `.cer` certificate; the private key is never published. This release changes only the endpoint switcher service, so no kernel-driver reboot is required when upgrading from v1.3.4; restarting the service or signing out is sufficient. A full reboot is still recommended.

### Verification

- `switcher.log` records `Internal microphone volume capped: 1.00 (30.00 dB) -> 0.70 (5.49 dB)`.
- Default capture remains *Multiple microphones* for Multimedia, Communications, and Console.
- **Correction:** the original sample-level verification was not reliable because some WAV analysis parsed float32 data incorrectly. Correct float32 analysis found DC/stale samples, not speech. Only the routing and level-cap observations are valid.

**SHA-256:** see the attached `SHA256SUMS.txt`.

---

## v1.3.4 — functional default capture (20/11/2026)

> [!NOTE]
> The original claim that the internal array produced `rms≈70591` was based on parsing 32-bit float WAV samples as integers. The routing conclusion below is still useful, but the "real signal" claim was a false positive. Correct float32 analysis later showed DC/stale samples.

Live measurement with v1.3.3 installed (`rt5682saudio 18.5.2.0`) showed that the switcher had selected the silent TRRS jack endpoint instead of *Multiple microphones*. That default-routing problem was real, and v1.3.4 fixed it. It did not prove that the internal DMIC0 array carried speech.

### Changes from v1.3.3

- The switcher now keeps *Multiple microphones* as the default capture device for the Multimedia, Communications, and Console roles, with or without a jack inserted.
- With a TRRS headset inserted, render still goes to *Headphones*; the headset microphone remains visible for manual selection/diagnosis but is no longer the default. With a 3-pole TRS plug it is hidden as before.
- No kernel binaries changed: `rt5682saudio` remains at `18.5.2.0` and the SSP1/RT1019P speaker pipeline is untouched.
- Added `tests\Test-AudioDefault.cpp` to verify default render/capture endpoints and roles. It also showed the capture endpoint at `VolumenScalar=1.0` / `VolumenDb=+30.00`, which explains why float samples from the array reach `7.38` (out of range). This behavior was not changed in v1.3.4; lower the Windows recording level if distortion is audible.

### Status

- **Speakers:** verified on Google Marasov / ASUS Chromebook Plus CX34.
- **Headphone jack:** verified on the same machine.
- **Internal microphone (DMIC0 array):** capture default, but speech capture was not proven and later float32 analysis found DC/stale samples.
- **TRRS headset microphone:** under investigation; still at noise level and no longer the default.

### Requirements

- Windows 11 x64, Secure Boot disabled, Test Mode enabled (`testsigning on`).
- Compatible UEFI/coreboot firmware (MrChromebox recommended).
- Intel Alder Lake-P with DSP `8086:51C8`, RTL5682S, and RT1019P.

### Important

Test-signed drivers, not WHQL. The ZIP contains only the public `.cer` certificate; the private key is never published. If upgrading from v1.3.3, reboot after installation. This release was published before the DMIC speech fix; v1.3.5 keeps the active fix scoped to routing and endpoint level, and the TRRS headset microphone remains out of scope.

### Verification

- 0 IPC timeouts, DSP `Stage=10`, `Status=0`.
- `SpkStreamDaiBclkHz=3072000`, `Amp0\SdbWriteValue=1` during playback.
- I²C registers: `0xFE=10EC`, `0xFF=6749`.
- `switcher.log` shows *Multiple microphones* as the capture default.

**SHA-256:** see the attached `SHA256SUMS.txt`.

---

## Earlier releases

### v1.3.3 — full SAR sequence (internal validation)

Added `Rt5682S_SarPowerNormal()`, equivalent to `rt5682s_sar_power_mode(SAR_PWR_NORMAL)` in Linux/CoolStar: `CBJ_CTRL_3` enables the input buffer, `CBJ_CTRL_1` puts MB1/MB2 under FSM control, `SAR_IL_CMD_1` switches to automatic control, a 5 ms wait follows, and `BUTDET` is enabled at normal power. It runs after headset detection and when capture opens. `Rt5682SAudio` moved to `18.5.2.0`. Speaker pipeline unchanged. The original "internal array real signal" measurement was later invalidated by incorrect float32 WAV parsing.

### v1.3.2 — TRRS detection fix

`NauDetectHeadsetMic()` now retries `CBJ_CTRL_2[1:0]` until it reads OMTP (`1`) or CTIA (`2`); any nonzero value is treated as a headset, with CTIA bias for the ambiguous `3` case. `Rt5682SAudio` moved to `18.5.1.0`. The original claim that the internal microphone was confirmed working was later invalidated by incorrect float32 WAV parsing.

### v1.3.1 — real RTL5682S register map

Replaced the inherited NAU88L25 audio routing with the real RTL5682S map validated against Linux v6.6 and CoolStar. Rewrote capture power-up, jack event reporting, and endpoint filtering by `DeviceDesc`/`InstanceId`. `Rt5682SAudio` moved to `18.5.0.0`. Speakers and jack verified; microphone still pending.
