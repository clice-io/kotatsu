@echo off
for /f "delims=" %%i in ('clang-cl --print-resource-dir') do set "CLANG_RES=%%i"
if not defined CLANG_RES (
  echo Failed to resolve clang resource directory from clang-cl. 1>&2
  exit /b 1
)
if not exist "%CLANG_RES%\lib\windows" (
  echo clang resource windows lib path not found: %CLANG_RES%\lib\windows 1>&2
  exit /b 1
)
set "PATH=%CLANG_RES%\lib\windows;%PATH%"
