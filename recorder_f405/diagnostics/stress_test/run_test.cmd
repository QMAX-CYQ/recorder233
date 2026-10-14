@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /utf-8 /W4 /Fe:stress_format_test.exe stress_format_test.c
if errorlevel 1 exit /b 1
stress_format_test.exe
