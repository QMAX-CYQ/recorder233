@echo off
call "D:\Application\Visual Studio 2022\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /utf-8 /W4 /DFAULT_LOG_ENABLE_TEST=0 /I"D:\RM\recorder233\recorder_f405\diagnostics\user_add_refresh\fault_tests\native_test" /I"D:\RM\recorder233\recorder_f405\diagnostics\user_add_refresh\staged" test_fault_log.c fault_log_port.c /Fe:fault_0.exe
if errorlevel 1 exit /b 1
fault_0.exe
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /W4 /DFAULT_LOG_ENABLE_TEST=1 /I"D:\RM\recorder233\recorder_f405\diagnostics\user_add_refresh\fault_tests\native_test" /I"D:\RM\recorder233\recorder_f405\diagnostics\user_add_refresh\staged" test_fault_log.c fault_log_port.c /Fe:fault_1.exe
if errorlevel 1 exit /b 1
fault_1.exe
if errorlevel 1 exit /b 1
