@echo off
chcp 65001 >nul
setlocal

set VCVARS=D:\Apps\VSstudio\VC\Auxiliary\Build\vcvars64.bat
set OPENCV_DIR_PATH=D:/Apps/opencv/opencv/build/x64/vc16/lib

call "%VCVARS%" >nul
if errorlevel 1 (
  echo [错误] 找不到 vcvars64.bat，改一下脚本里的 VCVARS 路径
  goto :fail
)

cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="%OPENCV_DIR_PATH%"
if errorlevel 1 goto :fail

cmake --build "%~dp0build"
if errorlevel 1 goto :fail

echo.
echo ============================================
echo  编译成功：%~dp0build\aim.exe
echo ============================================
goto :eof

:fail
echo.
echo 编译失败，看上面的报错
exit /b 1
