@echo off
setlocal
call "D:\applications\VisualStudio\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set CMAKE="D:\applications\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA="D:\applications\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set PATH=D:\applications\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
%CMAKE% -S "D:\workspace\ShuoWen\engine" -B "D:\workspace\ShuoWen\engine\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=%NINJA%
if errorlevel 1 exit /b 1
%CMAKE% --build "D:\workspace\ShuoWen\engine\build" --config Release
exit /b %errorlevel%
