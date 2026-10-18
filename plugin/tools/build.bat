@echo off
REM Build the Flat White UI plugin. Requires MSVC Build Tools + Qt 5.15.2 msvc2019_64.
setlocal

set "ROOT=%~dp0.."
set "QTDIR=C:\Qt\5.15.2\msvc2019_64"
set "VCVARS=C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VCVARS%" (
  echo [error] vcvars64.bat not found at %VCVARS%
  exit /b 1
)
if not exist "%QTDIR%\lib\Qt5Widgets.lib" (
  echo [error] Qt kit not found at %QTDIR%
  exit /b 1
)

call "%VCVARS%" >nul || exit /b 1

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
