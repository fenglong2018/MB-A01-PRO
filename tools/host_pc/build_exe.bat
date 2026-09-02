@echo off
setlocal
cd /d "%~dp0"

where py >nul 2>&1
if %errorlevel%==0 (
  set PY=py -3
) else (
  where python >nul 2>&1
  if %errorlevel%==0 (
    set PY=python
  ) else (
    echo 未找到 Python。请安装 3.10+ 并勾选 Add to PATH。
    pause
    exit /b 1
  )
)

%PY% -m pip install -r requirements.txt pyinstaller
if errorlevel 1 (
  echo pip 安装失败
  pause
  exit /b 1
)

%PY% -m PyInstaller --noconfirm --clean --windowed --name shySOFT_Host --collect-all PySide6 --hidden-import serial --hidden-import shy_host.main_window --hidden-import shy_host.serial_link --hidden-import shy_host.protocol --hidden-import shy_host.radio_parse --paths . shy_host\main.py
if errorlevel 1 (
  echo 打包失败
  pause
  exit /b 1
)

echo.
echo EXE: %cd%\dist\shySOFT_Host\shySOFT_Host.exe
explorer dist\shySOFT_Host
pause
