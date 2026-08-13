@echo off
setlocal
cd /d "%~dp0"

where py >nul 2>&1
if %errorlevel%==0 (
  py -3 -m shy_host
  goto :done
)

where python >nul 2>&1
if %errorlevel%==0 (
  python -m shy_host
  goto :done
)

echo 未找到 Python。请安装 Python 3.10+ 并勾选 Add to PATH。
echo 然后执行: py -3 -m pip install -r requirements.txt
pause
exit /b 1

:done
if errorlevel 1 (
  echo.
  echo 若缺依赖，请执行: py -3 -m pip install -r requirements.txt
  pause
)
