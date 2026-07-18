@ECHO OFF
SETLOCAL
CD /D "%~dp0"

NMAKE /NOLOGO /F "tools\Makefile.Windows"
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%

"tools\conftype\conftype.exe" "package.conf" "src\dewpoint_define.h.template" > "src\dewpoint_define.h.tmp"
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%
MOVE /Y "src\dewpoint_define.h.tmp" "src\dewpoint_define.h" > NUL
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%

"tools\conftype\conftype.exe" "package.conf" "vdf\action_manifest.vdf.template" > "vdf\action_manifest.vdf.tmp"
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%
MOVE /Y "vdf\action_manifest.vdf.tmp" "vdf\action_manifest.vdf" > NUL
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%

"tools\conftype\conftype.exe" "package.conf" "Makefile.Windows.template" > "Makefile.Windows.tmp"
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%
MOVE /Y "Makefile.Windows.tmp" "Makefile.Windows" > NUL
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%

"tools\makerom\makerom.exe" "package.conf" "src\game_rom.c"
IF ERRORLEVEL 1 EXIT /B %ERRORLEVEL%

NMAKE /NOLOGO /F "Makefile.Windows" %*
EXIT /B %ERRORLEVEL%
