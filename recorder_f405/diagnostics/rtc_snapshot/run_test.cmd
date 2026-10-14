@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /utf-8 /W4 /Fe:rtc_snapshot_test.exe rtc_snapshot_test.c
if errorlevel 1 exit /b 1
rtc_snapshot_test.exe
