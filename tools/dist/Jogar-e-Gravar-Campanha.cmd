@echo off
rem Monitor de Campanha (N8.8, D-N8-4): abre a janela "Jogar e Gravar Campanha" (tools\dev\campanha.py).
rem Precisa do Python 3 com Tk. Nao altera a instalacao do jogo; os dados ficam em %LOCALAPPDATA%\BlueStingerRecomp\campanhas.
setlocal
set "MONITOR=%~dp0..\dev\campanha.py"
where pythonw >nul 2>nul
if not errorlevel 1 (
    start "" pythonw "%MONITOR%" gui
    exit /b 0
)
where python >nul 2>nul
if not errorlevel 1 (
    python "%MONITOR%" gui
    exit /b %errorlevel%
)
echo Python 3 nao encontrado. Instale o Python 3 (com Tk) para usar o Monitor de Campanha.
pause
exit /b 1
