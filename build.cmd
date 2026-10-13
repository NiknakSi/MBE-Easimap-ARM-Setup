@echo off
setlocal
rem Run from a Visual Studio tools prompt; optionally set MBE_VCVARS on an ARM64 host.
if defined MBE_VCVARS call "%MBE_VCVARS%" arm64
if errorlevel 1 exit /b 1
where msbuild.exe >nul 2>nul
if errorlevel 1 (
 echo Open a Visual Studio Developer Command Prompt first.
 exit /b 1
)
set MBE_CONFIGURATION=Release
if not "%~1"=="" set MBE_CONFIGURATION=%~1
msbuild "%~dp0MBE-Easimap-ARM-Setup.sln" /m /nologo /p:Configuration=%MBE_CONFIGURATION% /p:Platform=ARM64 /verbosity:minimal
exit /b %errorlevel%
