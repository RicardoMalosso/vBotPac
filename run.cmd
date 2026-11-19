@echo off
setlocal
where py >nul 2>&1
if errorlevel 1 goto python
py -3.12 "%~dp0inclient\launch.py" %*
goto done
:python
python "%~dp0inclient\launch.py" %*
:done
if errorlevel 1 pause
