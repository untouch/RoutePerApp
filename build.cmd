@echo off
rem ==========================================================================
rem  RoutePerApp - Durchbau in einem Schritt (MSVC, Visual Studio 2019/2022)
rem
rem  build.cmd            alles bauen (Tests + Programm)
rem  build.cmd exe        nur Programm und beide Hook-Bibliotheken
rem  build.cmd tests      nur die Tests
rem  build.cmd probe      zusaetzlich eine Datei ohne Erhoehungsvermerk - nur fuer
rem                       automatische Ablauf-Tests, weil ein erhohtes Programm von
rem                       einem normalen Testfenster nicht gesteuert werden kann
rem  build.cmd shot       kleines Werkzeug fuer das Bildschirmbild der README
rem
rem  Ergebnis:
rem    build\routeperapp.exe   Oberflaeche (64 Bit)  <- die ausfuehrbare Datei
rem    build\hook.dll         Hook 64 Bit (fuer 64-Bit-Programme)
rem    build\hook32.dll       Hook 32 Bit (fuer 32-Bit-Programme)
rem    build\tests.exe, gui_tests.exe, inject_tests.exe, srtarget.exe
rem ==========================================================================
setlocal
cd /d "%~dp0"

set VCVARS=
for %%Y in ("C:\Program Files\Microsoft Visual Studio\2022" "C:\Program Files (x86)\Microsoft Visual Studio\2022" "C:\Program Files\Microsoft Visual Studio\2019" "C:\Program Files (x86)\Microsoft Visual Studio\2019") do (
  for %%E in (BuildTools Enterprise Professional Community) do (
    if not defined VCVARS if exist "%%~Y\%%E\VC\Auxiliary\Build\vcvars64.bat" set VCVARS="%%~Y\%%E\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if not defined VCVARS (
  echo Visual Studio Build Tools nicht gefunden.
  echo Noetig ist die Arbeitslast "C++-Desktop-Entwicklung": https://visualstudio.microsoft.com/downloads/
  exit /b 2
)
set VCVARS32=%VCVARS:vcvars64=vcvars32%

if not exist build mkdir build

rem /MT = C-Laufzeit eingebettet, die exe laeuft ohne VC-Redistributable
set FLAGS=/nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Zi /Fd:build\ /Od /MT
set LIBS=ws2_32.lib iphlpapi.lib psapi.lib advapi32.lib user32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib
set GUILIBS=comctl32.lib uxtheme.lib dwmapi.lib gdi32.lib shlwapi.lib shcore.lib

set MODE=%1
if "%MODE%"=="" set MODE=all

call %VCVARS% >nul 2>&1
if errorlevel 1 exit /b 2

rem Datei-Eigenschaften und Symbol in eine Resourcedatei (rc.exe kommt mit dem SDK)
rc /nologo /fo build\app.res app.rc || exit /b 1

if "%MODE%"=="tests" goto :tests
if "%MODE%"=="exe"   goto :exe
if "%MODE%"=="probe" goto :probe
if "%MODE%"=="shot"  goto :shot
if "%MODE%"=="all"   goto :all
echo unbekanntes Ziel: %MODE%   (moeglich: all, exe, tests, probe, shot)
exit /b 2

:tests
echo [1] Tests des Kerns
cl %FLAGS% /Fe:build\tests.exe tests\test_core.cpp src\lang.cpp /Fo:build\ /link %LIBS% || exit /b 1
echo [2] Tests der Oberflaeche
cl %FLAGS% /Fe:build\gui_tests.exe tests\test_gui.cpp src\gui_logic.cpp src\core.cpp src\lang.cpp /Fo:build\ /link %LIBS% || exit /b 1
goto :done

:exe
echo [1] Hook-Bibliothek 64 Bit
cl %FLAGS% /LD /Fe:build\hook.dll src\hook.cpp src\core.cpp src\lang.cpp /Fo:build\ /link %LIBS% || exit /b 1
echo [2] Oberflaeche
cl %FLAGS% /Fe:build\routeperapp.exe src\gui.cpp src\gui_drag.cpp src\gui_logic.cpp src\core.cpp src\lang.cpp /Fo:build\ /link /subsystem:windows /MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:app.manifest build\app.res %GUILIBS% %LIBS% || exit /b 1
goto :hook32

:all
echo [1] Tests des Kerns
cl %FLAGS% /Fe:build\tests.exe tests\test_core.cpp src\lang.cpp /Fo:build\ /link %LIBS% || exit /b 1
echo [2] Tests der Oberflaeche
cl %FLAGS% /Fe:build\gui_tests.exe tests\test_gui.cpp src\gui_logic.cpp src\core.cpp src\lang.cpp /Fo:build\ /link %LIBS% || exit /b 1
echo [3] Zielprogramm fuer Integrationstests
cl %FLAGS% /Fe:build\srtarget.exe src\srtarget.cpp /Fo:build\ /link ws2_32.lib || exit /b 1
echo [4] Injektionstests
cl %FLAGS% /Fe:build\inject_tests.exe tests\test_inject.cpp src\core.cpp src\lang.cpp /Fo:build\ /link %LIBS% || exit /b 1
echo [5] Bildschirmbild-Werkzeug
cl %FLAGS% /Fe:build\shot.exe tools\shot.cpp /Fo:build\ /link %GUILIBS% %LIBS% gdiplus.lib || exit /b 1
echo [6] Hook-Bibliothek 64 Bit
cl %FLAGS% /LD /Fe:build\hook.dll src\hook.cpp src\core.cpp src\lang.cpp /Fo:build\ /link %LIBS% || exit /b 1
echo [7] Oberflaeche
cl %FLAGS% /Fe:build\routeperapp.exe src\gui.cpp src\gui_drag.cpp src\gui_logic.cpp src\core.cpp src\lang.cpp /Fo:build\ /link /subsystem:windows /MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:app.manifest build\app.res %GUILIBS% %LIBS% || exit /b 1
rem fällt nach unten durch zum 32-Bit-Hook

:hook32
rem 32-Bit-Hook: eigene Builder-Umgebung und eigene Objektdateien.
if not exist build\x86 mkdir build\x86
call %VCVARS32% >nul 2>&1
if errorlevel 1 exit /b 2
echo [8] Hook-Bibliothek 32 Bit
cl %FLAGS% /LD /Fe:build\hook32.dll src\hook.cpp src\core.cpp src\lang.cpp /Fo:build\x86\ /link %LIBS% || exit /b 1
goto :done

:shot
rem Bild fuer die README: malt das eigene Fenster in eine PNG-Datei.
cl %FLAGS% /Fe:build\shot.exe tools\shot.cpp /Fo:build\ /link %GUILIBS% %LIBS% gdiplus.lib || exit /b 1
goto :done

:probe
rem Wie oben, aber ohne das Erhoehungsvermerk im eingebetteten Merkblatt - nur fuer Tests.
cl %FLAGS% /Fe:build\routeperapp_probe.exe src\gui.cpp src\gui_drag.cpp src\gui_logic.cpp src\core.cpp src\lang.cpp /Fo:build\ /link /subsystem:windows "/MANIFESTUAC:level='asInvoker' uiAccess='false'" build\app.res %GUILIBS% %LIBS% || exit /b 1

:done
echo.
echo Fertig.
if not "%MODE%"=="tests" (
  echo   Programm: %~dp0build\routeperapp.exe
  echo   Hooks:    %~dp0build\hook.dll   und   %~dp0build\hook32.dll
  echo   Beide Hooks neben routeperapp.exe legen - das Programm prueft sie und legt sie im
  echo   geschuetzten Unterordner hooks seiner Datenablage ab.
)
exit /b 0
