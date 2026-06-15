@echo off
setlocal
where gcc >nul 2>nul || set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;%PATH%"
if not exist build cmake -S . -B build -G Ninja
cmake --build build || exit /b 1
echo.
build\bin\spikot.exe %*
endlocal
