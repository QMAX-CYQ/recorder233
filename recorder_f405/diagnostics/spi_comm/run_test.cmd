@echo off
setlocal
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
cl /nologo /utf-8 /W4 /WX /DCOMM_INTERFACE=2 /I stubs /I ../../User/Comm /I ../../User/Recorder test_spi.c ../../User/Comm/spi_comm.c ../../User/Comm/log_rx_timestamp.c /Fe:spi_test.exe
if errorlevel 1 exit /b 1
spi_test.exe
if not %errorlevel%==0 exit /b 1
cl /nologo /utf-8 /W4 /WX /DCOMM_INTERFACE=1 /I stubs /I ../../User/Comm /I ../../User/Recorder test_timestamp.c ../../User/Comm/log_rx_timestamp.c /Fe:timestamp_rtc_test.exe
if errorlevel 1 exit /b 1
timestamp_rtc_test.exe
if not %errorlevel%==0 exit /b 1
cl /nologo /utf-8 /W4 /WX /DCOMM_INTERFACE=2 /DUSE_RTC=0 /I stubs /I ../../User/Comm /I ../../User/Recorder test_timestamp.c ../../User/Comm/log_rx_timestamp.c /Fe:timestamp_tick_test.exe
if errorlevel 1 exit /b 1
timestamp_tick_test.exe
if not %errorlevel%==0 exit /b 1
popd
exit /b 0
