@echo off
rem Builds Aleks.DualSense.SA.asi with llvm-mingw (i686). Put its bin folder on PATH first, e.g.
rem   set PATH=C:\llvm-mingw-ucrt-i686\bin;%PATH%
setlocal
cd /d "%~dp0"
i686-w64-mingw32-clang++ -std=c++17 -O2 -Wall -Wextra -Werror -static src\test_ds.cpp -o test_ds.exe || exit /b 1
"%~dp0test_ds.exe" || exit /b 1
i686-w64-mingw32-windres src\version.rc -O coff -o version.res || exit /b 1
i686-w64-mingw32-clang++ -std=c++17 -O2 -Wall -Wextra -Werror -static -shared -s src\dualsense.cpp version.res -o Aleks.DualSense.SA.asi ^
  -lhid -lsetupapi -lole32 -lgdi32 -lshell32 || exit /b 1
echo Built Aleks.DualSense.SA.asi
