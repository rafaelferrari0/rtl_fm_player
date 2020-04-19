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

REM change the back-slashes to forward-slashes without drive
SET MINGW_HOMEFW=%MINGW_HOME:~3%
SET MINGW_HOMEFW=%MINGW_HOMEFW:\=/%

FOR /D %%i IN ("%MINGW_HOME%cmake\share\cmake*") DO (
  set CMAKE_ROOT=%%i
)

ECHO CMAKE_ROOT=%CMAKE_ROOT%


cmake ..\..\ -G "Unix Makefiles" -D C_INCLUDE_PATH="/%MINGW_HOMEFW%i686-w64-mingw32/include" -D LIBRARY_PATH="/%MINGW_HOMEFW%i686-w64-mingw32/lib" -D CMAKE_MAKE_PROGRAM="/%MINGW_HOMEFW%bin/mingw32-make.exe" -D CMAKE_C_COMPILER="/%MINGW_HOMEFW%bin/i686-w64-mingw32-gcc.exe"


:fim
pause
