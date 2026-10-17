@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /utf-8 /W4 /I"D:\RM\recorder233\recorder_f405\diagnostics\user_add_refresh\staged" ready_link_test.c /Fe:ready_link_test.exe
if errorlevel 1 exit /b 1
ready_link_test.exe
