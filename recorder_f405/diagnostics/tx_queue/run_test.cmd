@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /Fe:queue_test.exe queue_test.c
if errorlevel 1 exit /b 1
queue_test.exe
