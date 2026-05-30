@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

cl.exe /LD /EHsc /std:c++17 /O2 ^
    /I "C:\N8RO\dev\samples\sim\student-char-anim\include" ^
    /I "C:\N8RO\include\astlib" ^
    /I "C:\N8RO\include\astsim" ^
    /I "C:\N8RO\include\astdb" ^
    /I "C:\N8RO\include\n8ro-db" ^
    "C:\N8RO\dev\samples\sim\student-char-anim\src\SimCharAnimStudentPlugin.cpp" ^
    /link /LIBPATH:"C:\N8RO\lib" arkheon-astlib.lib arkheon-astdb.lib n8ro-db.lib arkheon-astsim.lib user32.lib ^
    /OUT:"C:\N8RO\dev\samples\sim\student-char-anim\student-char-anim.dll"

if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo BUILD SUCCESS
