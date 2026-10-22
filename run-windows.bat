@echo off
REM ==========================================================================
REM  Lucid playground for Windows 10/11 - just double-click this file.
REM  Uses the prebuilt windows\lucid-server.exe, so no compiler is needed.
REM ==========================================================================
cd /d "%~dp0"
title Lucid playground

if exist gemini.key goto start
echo.
echo  The AI features need a Gemini API key (free: https://aistudio.google.com/apikey).
echo  Paste it below and press Enter - or just press Enter to skip AI.
echo.
set "KEY="
set /p "KEY=  Gemini API key: "
if not defined KEY goto start
>gemini.key echo %KEY%
echo  Saved to gemini.key. This file is never uploaded to GitHub.

:start
if exist deepseek.key goto run
echo.
echo  Optional backup: a DeepSeek API key, used only if Gemini does not answer.
echo  Paste it below and press Enter - or just press Enter to skip.
echo.
set "BKEY="
set /p "BKEY=  DeepSeek API key: "
if not defined BKEY goto run
>deepseek.key echo %BKEY%
echo  Saved to deepseek.key. This file is never uploaded to GitHub.

:run
echo.
echo  Starting Lucid - your browser will open http://localhost:8099
echo  If Windows Firewall asks, click "Allow". Close this window to stop.
echo.
start "" cmd /c "timeout /t 2 >nul & start http://localhost:8099"
windows\lucid-server.exe 8099
echo.
echo  The server stopped. If it says the port is in use, close the other window running Lucid.
pause
