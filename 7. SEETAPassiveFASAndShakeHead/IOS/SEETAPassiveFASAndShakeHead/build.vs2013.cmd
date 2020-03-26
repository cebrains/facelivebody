@echo off

set "ProjectName=SeetaPassiveFaceAntiSpoofing"
set "SolutionName=%ProjectName%"

set "VSHome=%VS120COMNTOOLS%..\IDE\"

echo.
echo Building Release^|x64 ...
"%VSHome%devenv.com" %SolutionName%.sln /Build "Release|x64" /Project %ProjectName%
echo.
echo Building Release^|Win32 ...
"%VSHome%devenv.com" %SolutionName%.sln /Build "Release|Win32" /Project %ProjectName%
echo.
echo Building Debug^|x64 ...
"%VSHome%devenv.com" %SolutionName%.sln /Build "Debug|x64" /Project %ProjectName%
echo.
echo Building Debug^|Win32 ...
"%VSHome%devenv.com" %SolutionName%.sln /Build "Debug|Win32" /Project %ProjectName%

echo.
echo Copy files into install
if not exist "%~dp0"install mkdir "%~dp0"install
xcopy /Y /S "%~dp0"build\*.h "%~dp0"install
xcopy /Y /S "%~dp0"build\*.dll "%~dp0"install
xcopy /Y /S "%~dp0"build\*.lib "%~dp0"install
if not exist "%~dp0"install\example mkdir "%~dp0"install\example
xcopy "%~dp0"testit\*.cpp "%~dp0"install\example

pause
