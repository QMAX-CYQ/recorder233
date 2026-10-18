@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /utf-8 /W4 /I. /I"..\staged" test_cache.c "..\staged\dcache_handle.c" /Fe:test_cache.exe
if errorlevel 1 exit /b 1
test_cache.exe
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /W4 /I. /I"..\staged" test_usb_port.c /Fe:test_usb_port.exe
if errorlevel 1 exit /b 1
for %%s in (0 1 2 3) do (
 test_usb_port.exe %%s
 if errorlevel 1 exit /b 1
)
