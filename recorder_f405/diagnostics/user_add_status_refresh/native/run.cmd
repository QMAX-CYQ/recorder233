@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /utf-8 /W4 /I"..\staged" test_status.c "..\staged\status_log.c" /Fe:test_status.exe
if errorlevel 1 exit /b 1
test_status.exe
