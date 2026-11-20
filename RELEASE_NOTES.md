# Release notes

## Current release

**Tag:** `v1.3.4`  
**Title:** `v1.3.4 — functional default capture`

---

## v1.3.4 — functional default capture (20/11/2026)

Live measurement with v1.3.3 installed (`rt5682saudio 18.5.2.0`) showed that the report "no microphone works" was a **default-routing** problem. The internal DMIC0 array produced real signal (`rms≈70591`, peak `241905`), but the switcher set the silent TRRS jack endpoint (`-94.7 dBFS`) as the default capture device. Applications therefore opened the dead endpoint.

### Changes from v1.3.3

- The switcher now keeps *Multiple microphones* as the default capture device for the Multimedia, Communications, and Console roles, with or without a jack inserted.
- With a TRRS headset inserted, render still goes to *Headphones*; the headset microphone remains visible for manual selection/diagnosis but is no longer the default. With a 3-pole TRS plug it is hidden as before.
- No kernel binaries changed: `rt5682saudio` remains at `18.5.2.0` and the SSP1/RT1019P speaker pipeline is untouched.
- Added `tests\Test-AudioDefault.cpp` to verify default render/capture endpoints and roles. It also showed the capture endpoint at `VolumenScalar=1.0` / `VolumenDb=+30.00`, which explains why float samples from the array reach `7.38` (out of range). This behavior was not changed in v1.3.4; lower the Windows recording level if distortion is audible.

### Status

- **Speakers:** verified on Google Marasov / ASUS Chromebook Plus CX34.
- **Headphone jack:** verified on the same machine.
- **Internal microphone (DMIC0 array):** working and now the capture default.
- **TRRS headset microphone:** under investigation; still at noise level and no longer the default.

### Requirements

- Windows 11 x64, Secure Boot disabled, Test Mode enabled (`testsigning on`).
- Compatible UEFI/coreboot firmware (MrChromebox recommended).
- Intel Alder Lake-P with DSP `8086:51C8`, RTL5682S, and RT1019P.

### Important

Test-signed drivers, not WHQL. The ZIP contains only the public `.cer` certificate; the private key is never published. If upgrading from v1.3.3, reboot after installation. Do not publish to GitHub until the headset microphone is validated.

### Verification

- 0 IPC timeouts, DSP `Stage=10`, `Status=0`.
- `SpkStreamDaiBclkHz=3072000`, `Amp0\SdbWriteValue=1` during playback.
- I²C registers: `0xFE=10EC`, `0xFF=6749`.
- `switcher.log` shows *Multiple microphones* as the capture default.

**SHA-256:** see the attached `SHA256SUMS.txt`.

---

## Earlier releases

### v1.3.3 — full SAR sequence (internal validation)

Added `Rt5682S_SarPowerNormal()`, equivalent to `rt5682s_sar_power_mode(SAR_PWR_NORMAL)` in Linux/CoolStar: `CBJ_CTRL_3` enables the input buffer, `CBJ_CTRL_1` puts MB1/MB2 under FSM control, `SAR_IL_CMD_1` switches to automatic control, a 5 ms wait follows, and `BUTDET` is enabled at normal power. It runs after headset detection and when capture opens. `Rt5682SAudio` moved to `18.5.2.0`. Speaker pipeline unchanged. Measurements: internal array real signal, jack microphone noise; manual OMTP/CTIA bias and live I²S word-width changes did not restore the jack microphone.

### v1.3.2 — TRRS detection fix

`NauDetectHeadsetMic()` now retries `CBJ_CTRL_2[1:0]` until it reads OMTP (`1`) or CTIA (`2`); any nonzero value is treated as a headset, with CTIA bias for the ambiguous `3` case. `Rt5682SAudio` moved to `18.5.1.0`. Internal microphone confirmed working.

### v1.3.1 — real RTL5682S register map

Replaced the inherited NAU88L25 audio routing with the real RTL5682S map validated against Linux v6.6 and CoolStar. Rewrote capture power-up, jack event reporting, and endpoint filtering by `DeviceDesc`/`InstanceId`. `Rt5682SAudio` moved to `18.5.0.0`. Speakers and jack verified; microphone still pending.
