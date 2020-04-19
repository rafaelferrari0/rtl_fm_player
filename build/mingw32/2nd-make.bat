@ECHO OFF


PUSHD %~dp0

FOR /D %%i IN ("..\","..\","..\","..\","..\","..\") DO (
  CD %%i
  IF EXIST bin\mingw32-make.exe SET MINGW_HOME=%%~fi
)

IF "%MINGW_HOME%"=="" (
ECHO MinGW directory not found
  GOTO fim
)

ECHO MINGW_HOME=%MINGW_HOME%

POPD

SET PATH=%MINGW_HOME%\i686-w64-mingw32\bin;%MINGW_HOME%\bin;%MINGW_HOME%\cmake\bin;%PATH%

mingw32-make.exe

pause
