CHROMEBOOK AUDIO FOR WINDOWS 11 - Marasov / CX34 (RTL5682S + RT1019P)
=======================================================================

Real internal audio through the Intel Sound Open Firmware (SOF) DSP, without
porting the Linux SOF stack and without paid driver dependencies.

SUPPORTED DEVICES
-----------------
Google Marasov / ASUS Chromebook Plus CX34 (Intel Core i3-1215U, Alder Lake-P,
DSP PCI 8086:51C8), with:
  - Realtek RTL5682S at ACPI\RTL5682  (3.5 mm headphones and TRRS microphone)
  - Realtek RT1019P at ACPI\RTL1019   (internal speakers; SDB GPIO only)
  - internal PDM microphones connected to DSP DMIC0

You must have UEFI/coreboot firmware (for example MrChromebox) and Windows 11 x64.
The installer checks the hardware and stops if it does not match.

IMPORTANT REQUIREMENTS
----------------------
These drivers are signed with a self-issued TEST certificate. They are NOT
WHQL-certified by Microsoft. Official WHQL signing needs an EV code-signing
certificate, a Microsoft Partner Center account, and paid validation; those
recurring costs are outside the scope of this free project. Windows therefore
loads the drivers only in Test Mode.

Windows requires:
  1. Secure Boot disabled in UEFI (Windows may reject testsigning otherwise).
  2. Windows Test Mode enabled (testsigning=on).
  3. Trust in ChromebookAudio.cer (the installer adds it).

ENABLE TEST MODE MANUALLY
-------------------------
Install.cmd can do this when you answer Y. To do it yourself:
  1. Disable Secure Boot in the UEFI firmware setup and keep it disabled.
  2. Open Start, type cmd, right-click Command Prompt, and choose
     "Run as administrator".
  3. Run:  bcdedit /set testsigning on
  4. Reboot Windows. A "Test Mode" watermark should appear on the desktop.
  5. Verify with:  bcdedit /enum {current} | findstr testsigning
     The result must be "testsigning Yes".

To return to normal, run "bcdedit /set testsigning off" as administrator,
re-enable Secure Boot if desired, and reboot. Use Uninstall.cmd first if the
drivers are no longer needed.

INSTALLATION
------------
  1. Extract the entire ZIP to a local folder (for example Desktop\CrosSofAudio).
  2. Double-click Install.cmd and accept the administrator prompt.
  3. If asked about Test Mode, answer Y. Reboot when requested.
  4. After the reboot, run Install.cmd a second time.
  5. Reboot once more. "Speakers" and "Headphones" should appear.

The installer verifies every signature before trusting the certificate, disables
the Microsoft vulnerable-driver blocklist (saving the previous value), installs
the four drivers, and registers the jack/default-endpoint switcher service.

CAPTURE IN v1.3.7
-----------------
"Multiple microphones" (the internal DMIC0 array) is the default capture device.
The "Microphone" endpoint for the TRRS jack remains visible for diagnosis but is
silent on this hardware and is no longer the default. If an application was
explicitly set to "Microphone", select "Multiple microphones" instead.

The endpoint reports +30 dB at 100%, so v1.3.5 caps the internal microphone
recording level at 70% (+5.49 dB) on startup and on routing changes. Do not raise
the slider back to 100%; the switcher restores the safe level on its next pass.
v1.3.6 configured the array on PDM1, but live speech capture still failed. v1.3.7
adds the canonical EQIIR high-pass stage and passed a 20-second live speech capture
with sustained AC signal.

PACKAGE CONTENTS
----------------
  Drivers\SklHdaudBus     Intel AudioDSP / HD Audio bus driver
  Drivers\CsAudioIntcsof  SOF boot, WaveRT, SSP1/SSP0/DMIC0 pipelines
  Drivers\Rt1019Audio     RT1019P speaker amplifier (SDB GPIO only)
  Drivers\Rt5682SAudio    RTL5682S codec and jack detection
  Switcher\               csaudioswitcher service (render + capture routing)
  Tools\cros_audio_test.exe  signed diagnostic utility

SETTINGS (HKLM\SOFTWARE\CrosSofAudio, DWORD; reboot to apply)
-------------------------------------------------------------
  SpeakerDspAttenDb = 2        speaker attenuation in dB (default)
  SpeakerLimiterThresholdDb=2  limiter headroom (0-15; higher = more protection)
  JackActiveLow = 1            jack-detect GPIO polarity
  MicArrayDspAttenDb = 0       internal microphone DSP attenuation in dB
  HeadsetMicDspAttenDb = 0     headset microphone DSP attenuation in dB

Example to lower the speakers by 6 dB:
  reg add HKLM\SOFTWARE\CrosSofAudio /v SpeakerDspAttenDb /t REG_DWORD /d 6 /f

The RT1019P has no I2C volume registers: all gain is applied in the DSP
(PCM0P -> volume -> SSP1). No unmeasured EQ is applied.

TROUBLESHOOTING
---------------
  - Collect-Logs.cmd generates audio-log.txt with DSP, IPC, codec, amplifier,
    jack, and switcher state.
  - If there is no sound, check audio-log.txt for:
      DspBoot\BootCount > 0 and a successful last result
      DspIpc\IpcTimeouts = 0
      Amp0\SdbWriteValue = 1 while a tone is playing
      DspIpc\SpkStreamDaiBclkHz = 3072000
  - If the jack switches in reverse, set JackActiveLow to 0 and reboot.
  - To disable DSP boot:
      reg add HKLM\SOFTWARE\CrosSofAudio /v EnableDspBoot /t REG_DWORD /d 0 /f
  - If the system will not boot, enter Safe Mode and run Uninstall.cmd.
  - Uninstall.cmd removes the drivers, restores the blocklist, and lets Windows
    reload its fallback drivers.
  - switcher.log is at C:\ProgramData\CrosSofAudio\switcher.log.

CREDITS AND LICENSES
--------------------
CoolStar (sklhdaudbus and the csaudio architecture), isaiahscape (original
cros-sofaudio), Kier (@3kierz) and Claude (audio diagnosis), the Linux SOF/ASoC
project and Intel (SOF firmware, LICENCE.sof-firmware, redistributable
BSD-3-Clause). See LICENSE, LICENCE.sof-firmware, and CREDITS.md.
