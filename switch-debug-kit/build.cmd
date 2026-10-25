@echo off
setlocal
pushd "%~dp0"
if not defined NXDK_PYTHON set "NXDK_PYTHON=python"
"%NXDK_PYTHON%" "build.py" %*
set "kitExitCode=%ERRORLEVEL%"
if not "%kitExitCode%"=="0" pause
popd
exit /b %kitExitCode%
