@echo off
REM Build the Flat White UI plugin. Requires MSVC Build Tools + Qt 5.15.2 msvc2019_64.
setlocal

set "ROOT=%~dp0.."
if not defined QTDIR set "QTDIR=C:\Qt\5.15.2\msvc2019_64"
if not defined VCVARS set "VCVARS=C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if defined MSVC_ROOT goto standalone_msvc
if not exist "%VCVARS%" (
  echo [error] vcvars64.bat not found at %VCVARS%; set VCVARS or MSVC_ROOT.
  exit /b 1
)
call "%VCVARS%" >nul || exit /b 1
goto toolchain_ready

:standalone_msvc
REM MSVC_ROOT points to the version directory containing bin, include, and lib.
if not defined WINDOWS_SDK_ROOT set "WINDOWS_SDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
if not defined WINDOWS_SDK_VERSION set "WINDOWS_SDK_VERSION=10.0.26100.0"
if not exist "%MSVC_ROOT%\bin\Hostx64\x64\cl.exe" (
  echo [error] x64 compiler not found under %MSVC_ROOT%
  exit /b 1
)
if not exist "%MSVC_ROOT%\include\vector" (
  echo [error] MSVC headers not found under %MSVC_ROOT%
  exit /b 1
)
if not exist "%MSVC_ROOT%\lib\x64\msvcprt.lib" (
  echo [error] MSVC x64 libraries not found under %MSVC_ROOT%
  exit /b 1
)
if not exist "%WINDOWS_SDK_ROOT%\Lib\%WINDOWS_SDK_VERSION%\um\x64\kernel32.lib" (
  echo [error] Windows SDK %WINDOWS_SDK_VERSION% not found under "%WINDOWS_SDK_ROOT%"
  exit /b 1
)
set "PATH=%MSVC_ROOT%\bin\Hostx64\x64;%WINDOWS_SDK_ROOT%\bin\%WINDOWS_SDK_VERSION%\x64;%PATH%"
set "INCLUDE=%MSVC_ROOT%\include;%WINDOWS_SDK_ROOT%\Include\%WINDOWS_SDK_VERSION%\ucrt;%WINDOWS_SDK_ROOT%\Include\%WINDOWS_SDK_VERSION%\shared;%WINDOWS_SDK_ROOT%\Include\%WINDOWS_SDK_VERSION%\um;%WINDOWS_SDK_ROOT%\Include\%WINDOWS_SDK_VERSION%\winrt"
set "LIB=%MSVC_ROOT%\lib\x64;%WINDOWS_SDK_ROOT%\Lib\%WINDOWS_SDK_VERSION%\ucrt\x64;%WINDOWS_SDK_ROOT%\Lib\%WINDOWS_SDK_VERSION%\um\x64"

:toolchain_ready
if not exist "%QTDIR%\lib\Qt5Widgets.lib" (
  echo [error] Qt kit not found at %QTDIR%
  exit /b 1
)

if not exist "%ROOT%\build" mkdir "%ROOT%\build"

cl /nologo /LD /EHsc /O2 /std:c++17 /MD /W3 /utf-8 ^
  /I"%QTDIR%\include" ^
  /I"%QTDIR%\include\QtCore" ^
  /I"%QTDIR%\include\QtGui" ^
  /I"%QTDIR%\include\QtWidgets" ^
  /I"%ROOT%\sdk\include" ^
  /I"%ROOT%\sdk\include\teamspeak" ^
  /Fo"%ROOT%\build\\" ^
  "%ROOT%\src\plugin.cpp" "%ROOT%\src\channelpanel.cpp" ^
  /Fe"%ROOT%\build\flatwhite_ui_win64.dll" ^
  /link /LIBPATH:"%QTDIR%\lib" Qt5Widgets.lib Qt5Gui.lib Qt5Core.lib user32.lib shell32.lib
if errorlevel 1 exit /b 1

echo [ok] %ROOT%\build\flatwhite_ui_win64.dll
endlocal
