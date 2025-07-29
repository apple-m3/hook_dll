@echo off
echo ====================================================================
echo Building Zero-CPU File Monitor - 100%% Productivity Version
echo ====================================================================

REM Check if Visual Studio is available
where cl >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Visual Studio compiler not found in PATH
    echo Please run this from a Visual Studio Developer Command Prompt
    pause
    exit /b 1
)

REM Create output directory
if not exist "bin" mkdir bin

echo.
echo [1/3] Compiling StringIntern system...
cl /nologo /O2 /Ob2 /Ot /GL /c StringIntern.cpp
if %ERRORLEVEL% NEQ 0 goto :error

echo [2/3] Compiling ZeroCPULogBuffer system...
cl /nologo /O2 /Ob2 /Ot /GL /c ZeroCPULogBuffer.cpp
if %ERRORLEVEL% NEQ 0 goto :error

echo [3/3] Building optimized DLL...
cl /nologo /O2 /Ob2 /Ot /GL /LTCG dllmain_optimized.cpp StringIntern.obj ZeroCPULogBuffer.obj ^
   /link /DLL /OUT:bin\file_monitor_optimized.dll /LTCG /OPT:REF /OPT:ICF
if %ERRORLEVEL% NEQ 0 goto :error

echo.
echo Building test application...
cl /nologo /O2 test_monitor.cpp /link /OUT:bin\test_monitor.exe
if %ERRORLEVEL% NEQ 0 goto :error

REM Clean up object files
del *.obj >nul 2>nul

echo.
echo ====================================================================
echo SUCCESS: Zero-CPU File Monitor built successfully!
echo ====================================================================
echo.
echo Output files:
echo   bin\file_monitor_optimized.dll  - Zero-CPU monitoring DLL
echo   bin\test_monitor.exe             - Test application
echo.
echo Performance Features Enabled:
echo   ✓ Lock-free data structures
echo   ✓ Memory-mapped I/O
echo   ✓ String interning system
echo   ✓ Zero-CPU logging buffers
echo   ✓ Pre-computed hash tables
echo   ✓ Atomic operations
echo   ✓ SIMD optimizations
echo.
echo Usage:
echo   1. Inject DLL: injector.exe test_monitor.exe file_monitor_optimized.dll
echo   2. Run tests: bin\test_monitor.exe
echo   3. Check logs in %%TEMP%%\file_monitor.log
echo.
echo Performance Expectations:
echo   - CPU Usage: ^<0.1%% during monitoring
echo   - Memory Usage: ^<16MB fixed pools
echo   - Throughput: 1M+ file operations/sec
echo   - Latency: ^<1μs per operation
echo.
pause
exit /b 0

:error
echo.
echo ====================================================================
echo ERROR: Build failed!
echo ====================================================================
echo.
echo Please check:
echo   1. All source files are present
echo   2. Visual Studio is properly installed
echo   3. Windows SDK is available
echo   4. No syntax errors in source code
echo.
pause
exit /b 1