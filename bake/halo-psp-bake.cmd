@echo off
rem halo-psp-bake.cmd: the bake's command on Windows (halo-psp-bake on Linux
rem and macOS). Finds a Python 3.9 to 3.14 and hands over to launcher.py, which
rem sets up the bake's own environment on the first run and starts bake.py.
rem   halo-psp-bake.cmd <Halo disc image or maps folder> <output folder> [options]
rem
rem Which Python: the bake's environment's own once it is made, then the
rem newest 3.14 ... 3.9 the py launcher knows, then python or python3.
setlocal
set "HERE=%~dp0"
set "CHECK=import sys; sys.exit(0 if sys.version_info[:2] in [(3, 9), (3, 10), (3, 11), (3, 12), (3, 13), (3, 14)] else 1)"
set "PY="
if exist "%HERE%.venv\Scripts\python.exe" call :try "%HERE%.venv\Scripts\python.exe"
for %%v in (3.14 3.13 3.12 3.11 3.10 3.9) do if not defined PY call :trypy %%v
if not defined PY call :try python
if not defined PY call :try python3
if not defined PY goto nopython
%PY% "%HERE%launcher.py" %*
exit /b %errorlevel%

:try
"%~1" -c "%CHECK%" >nul 2>&1 && set PY="%~1"
exit /b 0

:trypy
py -%1 -c "%CHECK%" >nul 2>&1 && set PY=py -%1
exit /b 0

:nopython
echo halo-psp-bake: the bake needs Python 3.9 to 3.14, and none was found.
echo Install one from https://www.python.org/downloads/windows/ (tick "Add python.exe to PATH"),
echo then run this again.
exit /b 1
