@echo off

set prevcd=%cd%
cd %~dp0..\..\..\..\

set PROJECT=CCR\CCR.uproject
set TESTS=VoxelPlugin
set OUTPUT=%cd%\CCR\Reports\VoxelPlugin
set LOG=%OUTPUT%\UnitTests.log
set ERRORS=%OUTPUT%\UnitTests.err

rem Plugins that may have stale build IDs (built against a different engine).
rem VoxelPlugin tests have no dependency on any of these.
set DISABLE=-DisablePlugins=CommonLoadingScreen,CommonGame,CommonUser,DebugMenu,Developer,EasyOnline,ExtendedCommonUI,JoltPhysics,Recall,RecallGameplay,UnrealEnginePatch,VariableCollection

if exist "%OUTPUT%" (
	echo Deleting "%OUTPUT%" ...
	rmdir /S /Q "%OUTPUT%"
)

mkdir "%OUTPUT%"

echo Running %TESTS% ...
call "Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -ExecCmds="Automation RunTests %TESTS%; Quit" -ReportExportPath="%OUTPUT%" -unattended -nopause -NullRHI -NoLoadingScreen -nosplash -STDOUT -TestExit="Automation Test Queue Empty" %DISABLE% > "%LOG%" 2>&1
set ec=%errorlevel%

type "%LOG%"
cd /d "%prevcd%"

findstr /C:" Error:" "%LOG%" > "%ERRORS%" 2>&1
if "%errorlevel%" equ "0" (
	echo.
	echo =[ ERRORS DETECTED ]============================================================ >&2
	type "%ERRORS%" >&2
	if "%ec%" equ "0" set ec=1
)

exit /b %ec%
