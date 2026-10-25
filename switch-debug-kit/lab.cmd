@echo off
setlocal
pushd "%~dp0"
if not defined NXDK_PYTHON set "NXDK_PYTHON=python"
if "%~1"=="" (
  "%NXDK_PYTHON%" "host/lab.py" status
) else (
  "%NXDK_PYTHON%" "host/lab.py" %*
)
set "kitExitCode=%ERRORLEVEL%"
if not "%kitExitCode%"=="0" pause
popd
exit /b %kitExitCode%
