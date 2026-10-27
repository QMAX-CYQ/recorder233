@echo off
setlocal
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
cl /nologo /utf-8 /W4 /WX /DCOMM_INTERFACE=1 /I ../../User/Comm /I ../../User/Recorder test_comm.c ../../User/Comm/comm.c /Fe:comm_usb_test.exe
if errorlevel 1 exit /b 1
comm_usb_test.exe
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /W4 /WX /DCOMM_INTERFACE=0 /I ../../User/Comm /I ../../User/Recorder test_comm.c ../../User/Comm/comm.c /Fe:comm_none_test.exe
if errorlevel 1 exit /b 1
comm_none_test.exe
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /W4 /WX /DCOMM_INTERFACE=2 /I ../../User/Comm /I ../../User/Recorder test_comm.c ../../User/Comm/comm.c /Fe:comm_spi_test.exe
if errorlevel 1 exit /b 1
comm_spi_test.exe
if errorlevel 1 exit /b 1
for %%m in (3 99) do (
    cl /nologo /utf-8 /W4 /DCOMM_INTERFACE=%%m /I ../../User/Comm /I ../../User/Recorder /c ../../User/Comm/comm.c >selection_error.log 2>&1
    if not errorlevel 1 exit /b 1
)
echo PASS: UART and invalid interface selections rejected
popd
exit /b 0
