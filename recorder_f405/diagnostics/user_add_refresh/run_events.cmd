@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /utf-8 /W4 /I"D:\RM\recorder233\recorder_f405\diagnostics\user_add_refresh\staged" events_test.c /Fe:events_test.exe
if errorlevel 1 exit /b 1
events_test.exe
