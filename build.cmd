@echo off
setlocal EnableDelayedExpansion

set "ROOT_DIR=%~dp0"
set "VERSION=v1.3.7"

:: Some WDK installations do not register the WindowsKernelModeDriver10.0
:: platform toolset with Visual Studio. Use the user-writable shim shipped in
:: build-tools\wdk-msbuild-shim so MSBuild can find it without touching Program
:: Files or requiring elevation. The old ..\tools location is kept as fallback
:: for existing development checkouts.
set "WDK_SHIM=%ROOT_DIR%build-tools\wdk-msbuild-shim\"
if not exist "%WDK_SHIM%Platforms\x64\PlatformToolsets\WindowsKernelModeDriver10.0\Toolset.props" (
    set "WDK_SHIM=%ROOT_DIR%..\tools\wdk-msbuild-shim\"
)
if exist "%WDK_SHIM%Platforms\x64\PlatformToolsets\WindowsKernelModeDriver10.0\Toolset.props" (
    if defined AdditionalVCTargetsPath (
        set "AdditionalVCTargetsPath=%AdditionalVCTargetsPath%;!WDK_SHIM!"
    ) else (
        set "AdditionalVCTargetsPath=!WDK_SHIM!"
    )
)

set "ERROR_LOG=%ROOT_DIR%build_errors.log"
set "FULL_LOG=%ROOT_DIR%build_full.log"
set "BUILD_RESULT=0"

echo ====================================================================
echo  Chromebook SOF Audio Drivers (RT1019 + RTL5682S) - Build Script
echo ====================================================================
echo  Version: !VERSION!
echo.

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" (
    echo [ERROR] vswhere.exe not found at '!VSWHERE!'. > "!ERROR_LOG!"
    echo Please install Visual Studio 2022/2026 or Build Tools. >> "!ERROR_LOG!"
    type "!ERROR_LOG!"
    set "BUILD_RESULT=1"
    goto :BuildDone
)

for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo [ERROR] Visual Studio with MSBuild not detected! > "!ERROR_LOG!"
    type "!ERROR_LOG!"
    set "BUILD_RESULT=1"
    goto :BuildDone
)

echo [INFO] Found Visual Studio at: "!VS_PATH!"
set "MSBUILD=!VS_PATH!\MSBuild\Current\Bin\MSBuild.exe"

if not exist "!MSBUILD!" (
    echo [ERROR] MSBuild.exe not found at '!MSBUILD!'. > "!ERROR_LOG!"
    type "!ERROR_LOG!"
    set "BUILD_RESULT=1"
    goto :BuildDone
)

echo [INFO] Building Solution (Release ^| x64)...
echo [INFO] Logging errors to: !ERROR_LOG!
echo.

:: Pin the SDK/WDK version explicitly. Without this, MSBuild resolves the
:: placeholder "10.0" from the vcxproj and fails with MSB8036 even though
:: 10.0.26100.0 is installed.
if not defined WINDOWS_TARGET_PLATFORM_VERSION (
    set "WINDOWS_TARGET_PLATFORM_VERSION=10.0.26100.0"
)
set "TARGET_PLATFORM_ARG=/p:WindowsTargetPlatformVersion=!WINDOWS_TARGET_PLATFORM_VERSION!"
echo [INFO] Pinning Windows SDK/WDK target to !WINDOWS_TARGET_PLATFORM_VERSION!.

:: Clear previous logs
if exist "!ERROR_LOG!" del /f /q "!ERROR_LOG!"
if exist "!FULL_LOG!" del /f /q "!FULL_LOG!"

:: Never allow an incremental build to reuse a catalog generated for older
:: INF or binary contents. Inf2Cat regenerates each one during MSBuild.
for %%c in (
    "%ROOT_DIR%x64\Release\Rt1019Audio\rt1019audio.cat"
    "%ROOT_DIR%x64\Release\Rt5682SAudio\rt5682saudio.cat"
    "%ROOT_DIR%x64\Release\SklHdaudBus\sklhdaudbus.cat"
    "%ROOT_DIR%x64\Release\CsAudioIntcsof\csaudiointcsof.cat"
) do if exist "%%~c" del /f /q "%%~c"

:: Ensure firmware files are present in driver package directory for Inf2Cat
if not exist "%ROOT_DIR%x64\Release\CsAudioIntcsof" mkdir "%ROOT_DIR%x64\Release\CsAudioIntcsof"
copy /y "%ROOT_DIR%CsAudioIntcsof\sof-*.ri" "%ROOT_DIR%x64\Release\CsAudioIntcsof\" >nul

:: Execute MSBuild with dedicated lightweight file loggers
"!MSBUILD!" "%ROOT_DIR%ChromebookAudioDrivers.sln" ^
    /p:Configuration=Release ^
    /p:Platform=x64 !TARGET_PLATFORM_ARG! ^
    /m ^
    /v:normal ^
    /flp1:logfile="!ERROR_LOG!";errorsonly ^
    /flp2:logfile="!FULL_LOG!";verbosity=normal

set BUILD_EXIT=%ERRORLEVEL%

if %BUILD_EXIT% equ 0 (
    for %%c in (
        "%ROOT_DIR%x64\Release\Rt1019Audio\rt1019audio.cat"
        "%ROOT_DIR%x64\Release\Rt5682SAudio\rt5682saudio.cat"
        "%ROOT_DIR%x64\Release\SklHdaudBus\sklhdaudbus.cat"
        "%ROOT_DIR%x64\Release\CsAudioIntcsof\csaudiointcsof.cat"
    ) do if not exist "%%~c" (
        echo [ERROR] Inf2Cat did not generate %%~c.
        set "BUILD_RESULT=1"
        goto :BuildDone
    )

    set "SIGNTOOL="
    for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT_DIR%build-tools\Find-SignTool.ps1"`) do set "SIGNTOOL=%%i"
    if not defined SIGNTOOL (
        echo [ERROR] signtool.exe was not found in the installed Windows Driver Kit.
        set "BUILD_RESULT=1"
        goto :BuildDone
    )

    set "SIGN_CERT_STORE="
    set "RESOLVED_SIGN_CERT_SHA1="
    if defined SIGN_CERT_PFX_PATH (
        for /f "usebackq delims=" %%a in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT_DIR%build-tools\Resolve-PfxSigningCertificate.ps1" -PfxPath "!SIGN_CERT_PFX_PATH!" -ExpectedCertificatePath "%ROOT_DIR%ChromebookAudio.cer" -OutputPath "%ROOT_DIR%x64\Release\ChromebookAudio.cer"`) do set "RESOLVED_SIGN_CERT_SHA1=%%a"
        set "SIGN_CERT_STORE=PFX"
    ) else (
        for /f "tokens=1,2 delims=|" %%a in ('powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT_DIR%build-tools\Resolve-SigningCertificate.ps1" -CertificatePath "%ROOT_DIR%ChromebookAudio.cer" -OutputPath "%ROOT_DIR%x64\Release\ChromebookAudio.cer"') do (
            set "SIGN_CERT_STORE=%%a"
            set "RESOLVED_SIGN_CERT_SHA1=%%b"
        )
    )
    if not defined RESOLVED_SIGN_CERT_SHA1 (
        echo [ERROR] Could not resolve the driver signing certificate and private key.
        set "BUILD_RESULT=1"
        goto :BuildDone
    )

    echo [INFO] Signing driver packages with certificate !RESOLVED_SIGN_CERT_SHA1!...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT_DIR%build-tools\Sign-BuildOutputs.ps1" ^
        -SignToolPath "!SIGNTOOL!" ^
        -Thumbprint "!RESOLVED_SIGN_CERT_SHA1!" ^
        -StoreLocation "!SIGN_CERT_STORE!" ^
        -Paths "%ROOT_DIR%x64\Release\Rt1019Audio\Rt1019Audio.sys;%ROOT_DIR%x64\Release\Rt1019Audio\rt1019audio.cat;%ROOT_DIR%x64\Release\Rt5682SAudio\Rt5682SAudio.sys;%ROOT_DIR%x64\Release\Rt5682SAudio\rt5682saudio.cat;%ROOT_DIR%x64\Release\SklHdaudBus\SklHdaudBus.sys;%ROOT_DIR%x64\Release\SklHdaudBus\sklhdaudbus.cat;%ROOT_DIR%x64\Release\CsAudioIntcsof\CsAudioIntcsof.sys;%ROOT_DIR%x64\Release\CsAudioIntcsof\csaudiointcsof.cat;%ROOT_DIR%x64\Release\CrosAudioTest.exe;%ROOT_DIR%x64\Release\csaudioendpointswitcher.exe"
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] Driver package signing failed.
        set "BUILD_RESULT=1"
        goto :BuildDone
    )

    set "SIGNATURE_VERIFY_POWERSHELL=powershell.exe"
    if defined CSAUDIO_SIGNATURE_POWERSHELL set "SIGNATURE_VERIFY_POWERSHELL=!CSAUDIO_SIGNATURE_POWERSHELL!"
    "!SIGNATURE_VERIFY_POWERSHELL!" -NoProfile -ExecutionPolicy Bypass -File "%ROOT_DIR%build-tools\Test-DriverSignatures.ps1" ^
        -CertificatePath "%ROOT_DIR%x64\Release\ChromebookAudio.cer" ^
        -Paths "%ROOT_DIR%x64\Release\Rt1019Audio\Rt1019Audio.sys;%ROOT_DIR%x64\Release\Rt1019Audio\rt1019audio.cat;%ROOT_DIR%x64\Release\Rt5682SAudio\Rt5682SAudio.sys;%ROOT_DIR%x64\Release\Rt5682SAudio\rt5682saudio.cat;%ROOT_DIR%x64\Release\SklHdaudBus\SklHdaudBus.sys;%ROOT_DIR%x64\Release\SklHdaudBus\sklhdaudbus.cat;%ROOT_DIR%x64\Release\CsAudioIntcsof\CsAudioIntcsof.sys;%ROOT_DIR%x64\Release\CsAudioIntcsof\csaudiointcsof.cat;%ROOT_DIR%x64\Release\CrosAudioTest.exe;%ROOT_DIR%x64\Release\csaudioendpointswitcher.exe"
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] One or more build outputs were signed by a different certificate.
        set "BUILD_RESULT=1"
        goto :BuildDone
    )

    for %%d in (Rt1019Audio Rt5682SAudio SklHdaudBus CsAudioIntcsof) do (
        copy /y "%ROOT_DIR%x64\Release\ChromebookAudio.cer" "%ROOT_DIR%x64\Release\%%d\ChromebookAudio.cer" >nul
    )
    echo [INFO] Driver packages signed and verified successfully.

    echo -------------------------------------------------------------------- > "!ERROR_LOG!"
    echo [BUILD STATUS] SUCCESS >> "!ERROR_LOG!"
    echo [TIMESTAMP]    %DATE% %TIME% >> "!ERROR_LOG!"
    echo [SOLUTION]     ChromebookAudioDrivers.sln >> "!ERROR_LOG!"
    echo [RESULT]       0 Errors, 0 Warnings >> "!ERROR_LOG!"
    echo. >> "!ERROR_LOG!"
    echo Generated Targets: >> "!ERROR_LOG!"
    echo   - x64\Release\Rt1019Audio\Rt1019Audio.sys >> "!ERROR_LOG!"
    echo   - x64\Release\Rt1019Audio\Rt1019Audio.inf >> "!ERROR_LOG!"
    echo   - x64\Release\Rt1019Audio\rt1019audio.cat >> "!ERROR_LOG!"
    echo   - x64\Release\Rt5682SAudio\Rt5682SAudio.sys >> "!ERROR_LOG!"
    echo   - x64\Release\Rt5682SAudio\Rt5682SAudio.inf >> "!ERROR_LOG!"
    echo   - x64\Release\Rt5682SAudio\rt5682saudio.cat >> "!ERROR_LOG!"
    echo   - x64\Release\SklHdaudBus\SklHdaudBus.sys >> "!ERROR_LOG!"
    echo   - x64\Release\SklHdaudBus\sklhdaudbus.inf >> "!ERROR_LOG!"
    echo   - x64\Release\SklHdaudBus\sklhdaudbus.cat >> "!ERROR_LOG!"
    echo   - x64\Release\CsAudioIntcsof\CsAudioIntcsof.sys >> "!ERROR_LOG!"
    echo   - x64\Release\CsAudioIntcsof\csaudiointcsof.inf >> "!ERROR_LOG!"
    echo   - x64\Release\CsAudioIntcsof\csaudiointcsof.cat >> "!ERROR_LOG!"
    echo   - x64\Release\CrosAudioTest.exe >> "!ERROR_LOG!"
    echo   - x64\Release\csaudioendpointswitcher.exe >> "!ERROR_LOG!"
    echo -------------------------------------------------------------------- >> "!ERROR_LOG!"

    echo.
    echo ====================================================================
    echo  [SUCCESS] All drivers and tools built successfully!
    echo  Lightweight build log written to: build_errors.log
    echo  Output binaries located at: %ROOT_DIR%x64\Release\
    echo ====================================================================
) else (
    echo.
    echo ====================================================================
    echo  [ERROR] Build failed with error code %BUILD_EXIT%.
    echo  Lightweight Error Log:
    echo --------------------------------------------------------------------
    if exist "!ERROR_LOG!" (
        type "!ERROR_LOG!"
    ) else (
        echo No specific errors captured in !ERROR_LOG!. See !FULL_LOG!.
    )
    echo --------------------------------------------------------------------
    echo ====================================================================
    set "BUILD_RESULT=%BUILD_EXIT%"
)

:BuildDone
endlocal & exit /b %BUILD_RESULT%
