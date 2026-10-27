@echo off
setlocal
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
cl /nologo /utf-8 /W4 /WX /I stubs /I package/SPI test_spi_log.c /Fe:spi_log_test.exe
if errorlevel 1 exit /b 1
spi_log_test.exe
if errorlevel 1 exit /b 1
popd
exit /b 0
