@echo off

REM Build for Visual Studio compiler. Run your copy of vcvarsall.bat to setup command-line compiler.

REM Check if build tool is on path
REM >nul, 2>nul will remove the output text from the where command
where /q cl.exe
if %errorlevel%==1 (
	echo MSVC CL not on path, please add it to path to build by command line.
	goto end
)

REM Build tags file
ctags -R

set ProjectName=drenderer
ctime -begin ..\src\%ProjectName%.ctm

IF NOT EXIST ..\bin mkdir ..\bin
pushd ..\bin

REM ////////////////////////////////////////////////////////////////////////////
REM Compile Switches
REM ////////////////////////////////////////////////////////////////////////////
REM EHa-   disable exception handling (we don't use)
REM GR-    disable c runtime type information (we don't use)
REM MD     use dynamic runtime library
REM MT     use static runtime library, so build and link it into exe
REM Od     disables optimisations
REM Oi     enable intrinsics optimisation, let us use CPU intrinsics if there is one
REM        instead of generating a call to external library (i.e. CRT).
REM Zi     enables debug data, Z7 combines the debug files into one.
REM W4     warning level 4
REM WX     treat warnings as errors
REM wd4100 unused argument parameters
REM wd4201 nonstandard extension used: nameless struct/union
REM wd4189 local variable is initialised but not referenced
REM wd4505 unreferenced local function not used will be removed
set CompileFlags=-EHa- -GR- -Oi -MT -Z7 -W4 -wd4100 -wd4201 -wd4189 -wd4505 -Od -FAsc
set Defines=

REM ////////////////////////////////////////////////////////////////////////////
REM Include Directories/Link Libraries
REM ////////////////////////////////////////////////////////////////////////////
set IncludeFiles=

REM Link libraries
set LinkLibraries=user32.lib kernel32.lib gdi32.lib

REM incremental:no, turn incremental builds off
REM opt:ref,        try to remove functions from libs that are not referenced at all
set LinkFlags=-incremental:no -opt:ref -subsystem:WINDOWS -machine:x64 -nologo

REM ////////////////////////////////////////////////////////////////////////////
REM Compile
REM ////////////////////////////////////////////////////////////////////////////
cl %CompileFlags% %Defines% ..\src\UnityBuild\UnityBuild.cpp %IncludeFiles% /link %LinkLibraries% %LinkFlags% /out:%ProjectName%.exe
REM cl %CompileFlags% /P %Defines% ..\src\UnityBuild\UnityBuild.cpp %IncludeFiles%

popd
set LastError=%ERRORLEVEL%
ctime -end %ProjectName%.ctm %LastError%

:end
exit /b %LastError%
