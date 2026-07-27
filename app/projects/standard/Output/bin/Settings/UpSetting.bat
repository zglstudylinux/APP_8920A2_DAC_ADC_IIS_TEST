echo off

set file_dir=SMART

REM Upload File Use Dir
if "%~1"=="" goto :end
if not exist D:\Users\%username%\upload.txt  (goto :end)
:upload_settings
Fileserv.exe -D %file_dir%\\Settings "%~f1"
shift
if not "%~1"=="" goto :upload_settings
