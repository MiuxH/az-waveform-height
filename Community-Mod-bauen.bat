@echo off
setlocal
cd /d "%~dp0"
py -3 "%~dp0Build-Community.py"
if errorlevel 1 echo Vorgang abgebrochen. Meldung oben lesen.
pause
