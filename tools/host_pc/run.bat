@echo off
setlocal
cd /d "%~dp0"

where pyw >nul 2>&1
if %errorlevel%==0 (
  start "" pyw -3 -m shy_host
  exit /b 0
)

where pythonw >nul 2>&1
if %errorlevel%==0 (
  start "" pythonw -m shy_host
  exit /b 0
)

echo 未找到 pythonw/pyw。请安装 Python 3.10+ 并勾选 Add to PATH。
echo 然后执行: py -3 -m pip install -r requirements.txt
pause
exit /b 1
