@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /utf-8 /W4 /Fe:metadata_test.exe metadata_test.c
if errorlevel 1 exit /b 1
metadata_test.exe
