@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Publish.ps1"
if errorlevel 1 echo Vorgang abgebrochen. Meldung oben lesen.
pause
