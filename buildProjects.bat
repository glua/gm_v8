@echo off
If NOT exist config.lua (
	copy config.sample.lua config.lua
	echo Created config.lua. Edit it if necessary, then run again to generate projects.
	pause
	exit
)
echo Generating Windows project...
premake4 --os=windows --platform=x32 --file=buildProjects.lua vs2010
echo.
echo Generating Mac project...
premake4 --os=macosx --platform=universal32 --file=buildProjects.lua gmake
echo.
echo Generating Linux project...
premake4 --os=linux --platform=x32 --file=buildProjects.lua gmake
echo.
pause