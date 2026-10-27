@echo off
setlocal
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
cl /nologo /utf-8 /W4 /WX /I ../user_add_spi_split/stubs /I D:/RM/wheel_leg_2026/wheel_leg_2025--4/rm_main/User/Log /I D:/RM/wheel_leg_2026/wheel_leg_2025--4/rm_main/User/Support test_sender_polling.c /Fe:sender_polling_test.exe
if errorlevel 1 exit /b 1
sender_polling_test.exe
if not %errorlevel%==0 exit /b 1
popd
exit /b 0
