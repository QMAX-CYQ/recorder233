@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /utf-8 /W4 /wd4312 /Fe:test_stage.exe test_stage.c
if errorlevel 1 exit /b 1
test_stage.exe
