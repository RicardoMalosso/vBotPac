@echo off
setlocal
where py >nul 2>&1
if errorlevel 1 goto python
py -3 "%~dp0external\window.py" %*
goto done
:python
python "%~dp0external\window.py" %*
:done
if errorlevel 1 pause
