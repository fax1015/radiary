@echo off
setlocal

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build"
set "OBJROOT=%BUILD%\CMakeFiles\Radiary.dir"
set "PDB=%OBJROOT%\Radiary-build.pdb"

call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

if not exist "%BUILD%" mkdir "%BUILD%"
if not exist "%OBJROOT%\src\core" mkdir "%OBJROOT%\src\core"
if not exist "%OBJROOT%\src\engine\flame" mkdir "%OBJROOT%\src\engine\flame"
if not exist "%OBJROOT%\src\engine\path" mkdir "%OBJROOT%\src\engine\path"
if not exist "%OBJROOT%\src\renderer" mkdir "%OBJROOT%\src\renderer"
if not exist "%OBJROOT%\src\io" mkdir "%OBJROOT%\src\io"
if not exist "%OBJROOT%\src\ui" mkdir "%OBJROOT%\src\ui"

set "CLFLAGS=/nologo /TP -DNOMINMAX -DUNICODE -DWIN32_LEAN_AND_MEAN -D_UNICODE /I%ROOT%\src /DWIN32 /D_WINDOWS /EHsc /Ob0 /Od /RTC1 -std:c++20 -MDd -Zi /W4 /permissive- /EHsc /FS"

cl %CLFLAGS% /c "%ROOT%\src\main.cpp" /Fo"%OBJROOT%\src\main.cpp.obj" /Fd"%PDB%"
if errorlevel 1 exit /b %errorlevel%
cl %CLFLAGS% /c "%ROOT%\src\core\Scene.cpp" /Fo"%OBJROOT%\src\core\Scene.cpp.obj" /Fd"%PDB%"
if errorlevel 1 exit /b %errorlevel%
cl %CLFLAGS% /c "%ROOT%\src\engine\flame\Variation.cpp" /Fo"%OBJROOT%\src\engine\flame\Variation.cpp.obj" /Fd"%PDB%"
if errorlevel 1 exit /b %errorlevel%
cl %CLFLAGS% /c "%ROOT%\src\engine\flame\IFSEngine.cpp" /Fo"%OBJROOT%\src\engine\flame\IFSEngine.cpp.obj" /Fd"%PDB%"
if errorlevel 1 exit /b %errorlevel%
cl %CLFLAGS% /c "%ROOT%\src\engine\path\SplinePath.cpp" /Fo"%OBJROOT%\src\engine\path\SplinePath.cpp.obj" /Fd"%PDB%"
if errorlevel 1 exit /b %errorlevel%
cl %CLFLAGS% /c "%ROOT%\src\renderer\SoftwareRenderer.cpp" /Fo"%OBJROOT%\src\renderer\SoftwareRenderer.cpp.obj" /Fd"%PDB%"
if errorlevel 1 exit /b %errorlevel%
cl %CLFLAGS% /c "%ROOT%\src\io\SceneSerializer.cpp" /Fo"%OBJROOT%\src\io\SceneSerializer.cpp.obj" /Fd"%PDB%"
if errorlevel 1 exit /b %errorlevel%
cl %CLFLAGS% /c "%ROOT%\src\io\PresetLibrary.cpp" /Fo"%OBJROOT%\src\io\PresetLibrary.cpp.obj" /Fd"%PDB%"
if errorlevel 1 exit /b %errorlevel%
cl %CLFLAGS% /c "%ROOT%\src\ui\MainWindow.cpp" /Fo"%OBJROOT%\src\ui\MainWindow.cpp.obj" /Fd"%PDB%"
if errorlevel 1 exit /b %errorlevel%

link /nologo ^
  "%OBJROOT%\src\main.cpp.obj" ^
  "%OBJROOT%\src\core\Scene.cpp.obj" ^
  "%OBJROOT%\src\engine\flame\Variation.cpp.obj" ^
  "%OBJROOT%\src\engine\flame\IFSEngine.cpp.obj" ^
  "%OBJROOT%\src\engine\path\SplinePath.cpp.obj" ^
  "%OBJROOT%\src\renderer\SoftwareRenderer.cpp.obj" ^
  "%OBJROOT%\src\io\SceneSerializer.cpp.obj" ^
  "%OBJROOT%\src\io\PresetLibrary.cpp.obj" ^
  "%OBJROOT%\src\ui\MainWindow.cpp.obj" ^
  /OUT:"%BUILD%\Radiary.exe" ^
  /PDB:"%BUILD%\Radiary.pdb" ^
  /SUBSYSTEM:WINDOWS ^
  /DEBUG ^
  windowscodecs.lib comdlg32.lib ole32.lib dwmapi.lib kernel32.lib user32.lib gdi32.lib winspool.lib shell32.lib oleaut32.lib uuid.lib advapi32.lib

exit /b %errorlevel%
