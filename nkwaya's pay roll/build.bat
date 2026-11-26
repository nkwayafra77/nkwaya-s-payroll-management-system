@echo off
REM Build the payroll app. Needs gcc (MinGW) in PATH, e.g. the one inside Dev-C++.
REM If gcc is not found, uncomment the next line and fix the path to your Dev-C++ folder:
REM set PATH=C:\Program Files (x86)\Dev-Cpp\MinGW64\bin;%PATH%
gcc -O2 -o PayrollApp.exe main.c payroll.c -mwindows -static -lcomctl32 -lgdi32
if errorlevel 1 (echo BUILD FAILED) else (echo BUILD OK - run PayrollApp.exe)
pause
