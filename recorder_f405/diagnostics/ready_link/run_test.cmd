@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /utf-8 /W4 /Fe:ready_link_test.exe ready_link_test.c
if errorlevel 1 exit /b 1
ready_link_test.exe
