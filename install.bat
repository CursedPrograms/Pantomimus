@echo off
setlocal
cd /d "%~dp0"
rem Installs Pantomimus for this user: %LOCALAPPDATA%\Programs\Pantomimus, with Start menu and Desktop shortcuts
rem named "Atelier - Pantomimus". Build it first (cmake --build build --config Release), or unzip a release and run this beside it.
set "SRC=build\Release"
if exist "Pantomimus.exe" set "SRC=."
if not exist "%SRC%\Pantomimus.exe" ( echo Build it first: cmake -S . -B build ^&^& cmake --build build --config Release & exit /b 1 )
tasklist /fi "imagename eq Pantomimus.exe" | find /i "Pantomimus.exe" >nul
if not errorlevel 1 ( echo Pantomimus is open: close it and run this again. & exit /b 1 )
set "INST=%LOCALAPPDATA%\Programs\Pantomimus"
if not exist "%INST%" mkdir "%INST%"
rem the program and what it reads beside it (no build leftovers, no test files)
for %%F in (Pantomimus.exe SDL2.dll onnxruntime.dll) do (
    if exist "%SRC%\%%F" copy /y "%SRC%\%%F" "%INST%\" >nul || ( echo Couldn't copy %%F to %INST% & exit /b 1 )
)
for %%D in (assets data lang models blender) do (
    if exist "%SRC%\%%D" xcopy /e /i /q /y "%SRC%\%%D" "%INST%\%%D\" >nul
)
powershell -NoProfile -Command "$s=New-Object -ComObject WScript.Shell; foreach($d in @([Environment]::GetFolderPath('Programs'),[Environment]::GetFolderPath('Desktop'))){ $l=$s.CreateShortcut((Join-Path $d 'Atelier - Pantomimus.lnk')); $l.TargetPath='%INST%\Pantomimus.exe'; $l.WorkingDirectory='%INST%'; $l.IconLocation='%INST%\Pantomimus.exe,0'; $l.Description='Pantomimus: motion capture from your webcam'; $l.Save() }"
echo Installed: %INST%
if /i not "%~1"=="--quiet" echo Start it from the Start menu or the Desktop. The pose models download on first start.
exit /b 0
