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

set PATH=%MINGW_HOME%\mingw32\bin;%MINGW_HOME%\bin;%MINGW_HOME%\cmake\bin;%PATH%

FOR /D %%i IN ("%MINGW_HOME%cmake\share\cmake*") DO (
  set CMAKE_ROOT=%%i
)

ECHO CMAKE_ROOT=%CMAKE_ROOT%

REM change the back-slashes to forward-slashes without drive
SET MINGW_HOMEFW=%MINGW_HOME:~3%
SET MINGW_HOMEFW=%MINGW_HOMEFW:\=/%


cmake ..\..\ -G "MinGW Makefiles" -D C_INCLUDE_PATH="/%MINGW_HOMEFW%include" -D LIBRARY_PATH="/%MINGW_HOMEFW%lib"

:fim
pause
