@echo off
rem Build Blockcraft on Windows with MSYS2 UCRT64.
rem One-time setup in the MSYS2 UCRT64 shell:
rem   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib make
rem Then run this file from a UCRT64 shell or with C:\msys64\ucrt64\bin on PATH.
setlocal
if exist C:\msys64\ucrt64\bin set PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%
cd /d "%~dp0"
make -j8 CXX=g++ WINDRES=windres
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
strip build\Blockcraft.exe
echo Built build\Blockcraft.exe
