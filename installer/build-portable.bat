@echo off
setlocal enabledelayedexpansion

rem Portable release packager for lvdterm: builds the Release preset and
rem zips up its dist\ folder (the exe plus everything windeployqt placed
rem next to it - Qt runtime DLLs, platform/style/plugin subfolders) into
rem a single, extract-and-run .zip. No installer, no admin rights, no
rem registry or Start Menu changes - the Inno Setup script
rem (installer\lvdterm.iss) packages the exact same dist\ folder, just
rem as a proper installer instead.
rem
rem Run from anywhere; paths below are relative to this script's own
rem location, not the current directory.

set "ROOT=%~dp0.."
set "DIST=%ROOT%\build\llvm-mingw-release\dist"
set "OUTDIR=%ROOT%\dist"

rem Read the version straight from CMakeLists.txt's own project() call
rem instead of hardcoding a third copy to keep in sync by hand (see
rem lvdterm.iss's own comment about doing exactly that for its copy).
set "VERSION="
for /f "tokens=3 delims= " %%V in ('findstr /c:"project(lvdterm VERSION" "%ROOT%\CMakeLists.txt"') do (
    if not defined VERSION set "VERSION=%%V"
)
if not defined VERSION (
    echo Could not read the version from CMakeLists.txt's project^(^) line.
    exit /b 1
)

echo Building Release preset...
cmake --build --preset llvm-mingw-release
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

if not exist "%DIST%\lvdterm.exe" (
    echo %DIST%\lvdterm.exe not found after building - check the build output above.
    exit /b 1
)

if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set "ZIPNAME=lvdterm-%VERSION%-portable.zip"
set "ZIPPATH=%OUTDIR%\%ZIPNAME%"
if exist "%ZIPPATH%" del /f "%ZIPPATH%"

rem Stage a clean copy first rather than zipping dist\ directly: *.log
rem files sometimes end up in there from ad hoc test runs (redirected
rem stdout/stderr while smoke-testing) and have no business in a release
rem zip. robocopy (not a plain xcopy/Compress-Archive-on-a-file-list)
rem is what keeps the platforms\/styles\/etc. plugin subfolders intact -
rem Compress-Archive flattens paths when fed a filtered file list instead
rem of a directory.
set "STAGE=%TEMP%\lvdterm-portable-stage"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
robocopy "%DIST%" "%STAGE%" /E /XF *.log /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo robocopy failed staging %DIST%.
    exit /b 1
)

echo Packaging %ZIPNAME% ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ZIPPATH%' -Force"
if errorlevel 1 (
    echo Packaging failed.
    rmdir /s /q "%STAGE%"
    exit /b 1
)

rmdir /s /q "%STAGE%"

echo.
echo Done: %ZIPPATH%
endlocal
