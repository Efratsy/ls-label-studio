@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem ===================================================================
rem  LS Label Studio - Windows-Build (64-bit)
rem  Erzeugt:  dist\LS-Label-Studio.exe  (Programm mit Oberflaeche)
rem            build\ls64.exe            (Brennprogramm, Kommandozeile)
rem  Voraussetzung: Lokales Python unter .\python313\python.exe
rem  Alles andere laedt das Skript selbst.
rem ===================================================================
setlocal
cd /d "%~dp0.."
set "PYTHON=%~dp0..\python313\python.exe"
if not exist "%PYTHON%" (
  echo Python fehlt: "%PYTHON%"
  echo Erwartete Struktur:
  echo   build_windows.bat
  echo   python313\python.exe
  pause & exit /b 1
)
echo [1/3] Werkzeuge installieren (PyQt6, PyInstaller, Zig-C-Compiler) ...
"%PYTHON%" -m pip install --upgrade --quiet pip
"%PYTHON%" -m pip install --upgrade --quiet pyqt6 pyinstaller ziglang
if errorlevel 1 ( echo pip-Installation fehlgeschlagen & pause & exit /b 1 )

echo [2/3] Brennprogramm ls64.exe bauen ...
if not exist build mkdir build
"%PYTHON%" -m ziglang cc -O2 -target x86_64-windows-gnu -DOHNE_PNG -DOHNE_ZLIB -o build\ls64.exe ^
  src\ls64.c src\bild.c src\spuren.c src\brennen.c src\daten.c src\plattform.c -lshell32
if errorlevel 1 ( echo FEHLER beim Bauen von ls64.exe - bitte die Ausgabe an Claude schicken & pause & exit /b 1 )
build\ls64.exe version

echo [3/3] Programm LS-Label-Studio.exe bauen ...
"%PYTHON%" -m PyInstaller --noconfirm --clean --onefile --windowed --uac-admin --name "LS-Label-Studio" ^
  --icon gui\ls-label-studio.ico ^
  --add-binary "build\ls64.exe;." --add-data "gui\ls-label-studio.svg;." --add-data "gui\testbild.png;." ^
  gui\ls_label_studio.py > build\pyinstaller.log 2>&1
if errorlevel 1 ( echo FEHLER bei PyInstaller - siehe build\pyinstaller.log & pause & exit /b 1 )

echo.
echo Selbsttest ...
dist\LS-Label-Studio.exe --selbsttest > build\selbsttest.txt 2>&1
type build\selbsttest.txt
echo.
echo FERTIG:  dist\LS-Label-Studio.exe
pause
