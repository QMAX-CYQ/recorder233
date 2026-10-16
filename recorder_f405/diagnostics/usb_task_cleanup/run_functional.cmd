@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /utf-8 /W4 /I"D:\RM\recorder233\recorder_f405\User\Task" test_functional.c /Fe:test_functional.exe
if errorlevel 1 exit /b 1
test_functional.exe
