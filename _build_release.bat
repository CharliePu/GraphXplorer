@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo >nul 2>&1
cd /d D:\GraphXplorer
cmake --build build-release --target %1
exit /b %ERRORLEVEL%
