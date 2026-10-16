@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

rem ============================================================
rem  ReplayScope one-click packaging script
rem
rem  Pipeline: read VERSION -> env check -> Qt build -> windeployqt
rem            -> PyInstaller x3 -> web build -> go build
rem            -> assemble deploy dir -> Inno Setup
rem
rem  Version single source of truth: VERSION.txt in this folder.
rem  (must keep the .txt suffix: the project root is on the compiler include
rem   path, and a file named VERSION would be picked up by #include <version>)
rem  Player3D.pro reads it via $$cat(); this script reads it and passes
rem  it to Inno Setup with /DAppVersion. No manual sync needed.
rem
rem  NOTE: this file must stay UTF-8 WITH BOM. Without the BOM, cmd.exe
rem  misparses multi-byte characters and the script breaks at random lines.
rem ============================================================

set "PROJ_DIR=%~dp0"
if "%PROJ_DIR:~-1%"=="\" set "PROJ_DIR=%PROJ_DIR:~0,-1%"

rem ---- environment paths; pre-set variables override these defaults ----
if not defined QTDIR set "QTDIR=C:\Qt\6.8.3\mingw_64"
if not defined MINGW set "MINGW=C:\Qt\Tools\mingw1310_64"
if not defined ISCC set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not defined NPM set "NPM=npm.cmd"
if not defined GO set "GO=go"
rem leave PYTHON empty to auto-detect an interpreter that has all deps

set "DEPLOY_DIR=%PROJ_DIR%\deploy"
set "BUILD_DIR=%PROJ_DIR%\build\Release"
set "WEB_DIR=%PROJ_DIR%\roboscope-web\web"
set "GO_DIR=%PROJ_DIR%\roboscope-web\server"
set "OUT_DIR=%PROJ_DIR%\installer_output"
rem Keep the Go build cache inside the project so packaging does not depend on
rem a machine-specific cache directory or permissions outside the workspace.
set "GOCACHE=%PROJ_DIR%\build\go-cache"
set "PATH=%QTDIR%\bin;%MINGW%\bin;%PATH%"
set "SETUP_EXE="

rem ---------- step 0: read version ----------
if not exist "%PROJ_DIR%\VERSION.txt" (
    echo [ERROR] VERSION.txt not found: %PROJ_DIR%\VERSION.txt
    goto fail
)
set "APP_VERSION="
for /f "usebackq tokens=1 delims= " %%v in ("%PROJ_DIR%\VERSION.txt") do (
    if not defined APP_VERSION set "APP_VERSION=%%v"
)
if "%APP_VERSION%"=="" (
    echo [ERROR] VERSION.txt is empty
    goto fail
)

echo.
echo ============================================================
echo   ReplayScope packaging   version v%APP_VERSION%
echo ============================================================
echo.

rem ---------- step 0.5: environment check, fail fast ----------
echo [0/9] checking build environment ...

if not exist "%QTDIR%\bin\qmake.exe" (
    echo [ERROR] qmake not found: %QTDIR%\bin\qmake.exe
    goto fail
)
if not exist "%MINGW%\bin\g++.exe" (
    echo [ERROR] g++ not found: %MINGW%\bin\g++.exe
    goto fail
)
where %GO% >nul 2>&1
if errorlevel 1 (
    echo [ERROR] go not found in PATH
    goto fail
)
where %NPM% >nul 2>&1
if errorlevel 1 (
    echo [ERROR] npm not found in PATH, install Node.js first
    goto fail
)

rem Pick a Python that really has the deps. Using a bare "python" is risky:
rem any leftover empty venv earlier in PATH will be picked and PyInstaller
rem then fails with a confusing missing-module error.
if not "%PYTHON%"=="" goto py_given

for %%P in ("python.exe" "D:\python312\python.exe" "python3.exe") do (
    if not defined PYTHON_OK call :probe_python %%P
)
if not defined PYTHON_OK (
    echo [ERROR] no usable Python found.
    echo         needs: numpy, pandas, pybullet, PyInstaller
    echo         set PYTHON=full\path\to\python.exe at the top of this script
    goto fail
)
goto py_done

:py_given
call :probe_python "%PYTHON%"
if not defined PYTHON_OK (
    echo [ERROR] the given Python lacks required modules: %PYTHON%
    goto fail
)

:py_done
echo        Python : %PYTHON%
echo        Qt     : %QTDIR%
echo        Output : %OUT_DIR%
echo.

rem ---------- step 1: clean ----------
echo [1/9] cleaning deploy dir ...
if exist "%DEPLOY_DIR%" rd /s /q "%DEPLOY_DIR%"
mkdir "%DEPLOY_DIR%"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

rem ---------- step 2: Qt release build in deploy mode ----------
echo [2/9] building Qt release ...
if exist "%BUILD_DIR%" rd /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%"
qmake "%PROJ_DIR%\Player3D.pro" -spec win32-g++ "CONFIG+=release" "CONFIG+=deploy"
if errorlevel 1 (
    popd
    echo [ERROR] qmake failed
    goto fail
)
mingw32-make -j%NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    popd
    echo [ERROR] compile failed
    goto fail
)
popd

set "QT_EXE=%BUILD_DIR%\release\Player3D.exe"
if not exist "%QT_EXE%" (
    echo [ERROR] Player3D.exe not produced
    goto fail
)

rem ---------- step 3: windeployqt ----------
echo [3/9] collecting Qt runtime ...
copy /y "%QT_EXE%" "%DEPLOY_DIR%\" >nul
windeployqt --release --no-translations --no-opengl-sw "%DEPLOY_DIR%\Player3D.exe"
if errorlevel 1 (
    echo [ERROR] windeployqt failed
    goto fail
)
rem screenshot / video recording needs the JPEG image plugin
if not exist "%DEPLOY_DIR%\imageformats\qjpeg.dll" (
    echo [ERROR] imageformats\qjpeg.dll missing, recording would break at runtime
    goto fail
)

rem ---------- step 4-6: PyInstaller ----------
echo [4/9] packaging X30 PyBullet service ...
call :pyinstall "%PROJ_DIR%\x30PybusSimlu" x30_server.spec x30_server "%DEPLOY_DIR%\x30PybusSimlu"
if errorlevel 1 goto fail

echo [5/9] packaging M20 PyBullet service ...
call :pyinstall "%PROJ_DIR%\M20pybusSimlu" m20_server.spec m20_server "%DEPLOY_DIR%\M20pybusSimlu"
if errorlevel 1 goto fail

echo [5/9] packaging DR02 humanoid PyBullet service ...
call :pyinstall "%PROJ_DIR%\DR02pybusSimlu" dr02_server.spec dr02_server "%DEPLOY_DIR%\DR02pybusSimlu"
if errorlevel 1 goto fail

echo [6/9] packaging SSH log helper ...
call :pyinstall "%PROJ_DIR%" log_ssh_helper.spec log_ssh_helper "%DEPLOY_DIR%"
if errorlevel 1 goto fail

rem ---------- step 7: web frontend ----------
echo [7/9] building web frontend ...
pushd "%WEB_DIR%"
if not exist "node_modules" (
    echo        first build, running npm install ...
    call %NPM% install
    if errorlevel 1 (
        popd
        echo [ERROR] npm install failed
        goto fail
    )
)
call %NPM% run build
if errorlevel 1 (
    popd
    echo [ERROR] frontend build failed
    goto fail
)
popd
if not exist "%WEB_DIR%\dist\index.html" (
    echo [ERROR] frontend dist missing
    goto fail
)

rem ---------- step 8: go backend + assemble web assets ----------
echo [8/9] building Go backend ...
pushd "%GO_DIR%"
%GO% build -ldflags="-s -w" -o roboscope.exe .
if errorlevel 1 (
    popd
    echo [ERROR] go build failed
    goto fail
)
popd
if not exist "%GO_DIR%\roboscope.exe" (
    echo [ERROR] roboscope.exe not produced
    goto fail
)

mkdir "%DEPLOY_DIR%\roboscope-web\server" 2>nul
mkdir "%DEPLOY_DIR%\roboscope-web\web\dist" 2>nul
copy /y "%GO_DIR%\roboscope.exe" "%DEPLOY_DIR%\roboscope-web\server\" >nul
xcopy /e /i /y /q "%WEB_DIR%\dist" "%DEPLOY_DIR%\roboscope-web\web\dist" >nul

rem ---------- artifact self-check ----------
echo.
echo        verifying artifacts ...
call :must_exist "%DEPLOY_DIR%\Player3D.exe"
if errorlevel 1 goto fail
call :must_exist "%DEPLOY_DIR%\x30PybusSimlu\pybullet_server.exe"
if errorlevel 1 goto fail
call :must_exist "%DEPLOY_DIR%\x30PybusSimlu\_internal\X30\urdf\X30.urdf"
if errorlevel 1 goto fail
call :must_exist "%DEPLOY_DIR%\M20pybusSimlu\pybullet_server.exe"
if errorlevel 1 goto fail
call :must_exist "%DEPLOY_DIR%\DR02pybusSimlu\pybullet_server.exe"
if errorlevel 1 goto fail
call :must_exist "%DEPLOY_DIR%\DR02pybusSimlu\_internal\DR02_model\DR02_pro.urdf"
if errorlevel 1 goto fail
call :must_exist "%DEPLOY_DIR%\log_ssh_helper.exe"
if errorlevel 1 goto fail
call :must_exist "%DEPLOY_DIR%\roboscope-web\server\roboscope.exe"
if errorlevel 1 goto fail
call :must_exist "%DEPLOY_DIR%\roboscope-web\web\dist\index.html"
if errorlevel 1 goto fail

rem ---------- step 9: Inno Setup ----------
if not exist "%ISCC%" (
    echo [9/9] skipping installer, ISCC.exe not found: %ISCC%
    echo        deploy dir is ready, you can compile Player3D.iss manually
    goto done
)

echo [9/9] building installer ...
"%ISCC%" /Q "/DAppVersion=%APP_VERSION%" "%PROJ_DIR%\Player3D.iss"
if errorlevel 1 (
    echo [ERROR] Inno Setup failed
    goto fail
)

set "SETUP_EXE=%OUT_DIR%\ReplayScope-%APP_VERSION%-x64-Setup.exe"
if not exist "%SETUP_EXE%" (
    echo [ERROR] installer not produced: %SETUP_EXE%
    goto fail
)

:done
echo.
echo ============================================================
echo   BUILD OK    v%APP_VERSION%
echo ============================================================
if not "%SETUP_EXE%"=="" (
    for %%F in ("%SETUP_EXE%") do set "SETUP_SIZE=%%~zF"
    set /a SETUP_MB=!SETUP_SIZE!/1048576
    echo   Installer : %SETUP_EXE%
    echo   Size      : !SETUP_MB! MB
)
echo   Portable  : %DEPLOY_DIR%
echo.
endlocal
exit /b 0

rem ============================================================
rem  subroutines
rem ============================================================

:probe_python
set "_PY=%~1"
"%_PY%" -c "import numpy,pandas,pybullet,PyInstaller" >nul 2>&1
if errorlevel 1 exit /b 1
set "PYTHON=%_PY%"
set "PYTHON_OK=1"
exit /b 0

:pyinstall
rem %1=workdir %2=spec %3=dist subdir %4=destination
pushd "%~1"
"%PYTHON%" -m PyInstaller --clean --noconfirm "%~2"
if errorlevel 1 (
    popd
    echo [ERROR] PyInstaller failed: %~2
    exit /b 1
)
if not exist "dist\%~3" (
    popd
    echo [ERROR] dist not found: %~1\dist\%~3
    exit /b 1
)
if not exist "%~4" mkdir "%~4"
xcopy /e /i /y /q "dist\%~3" "%~4" >nul
popd
exit /b 0

:must_exist
if not exist "%~1" (
    echo [ERROR] missing artifact: %~1
    exit /b 1
)
echo          OK  %~nx1
exit /b 0

:fail
echo.
echo ============================================================
echo   BUILD FAILED - see the error above
echo ============================================================
endlocal
exit /b 1
