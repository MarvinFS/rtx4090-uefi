@echo off
rem Builds tools\mma_bench.exe (used by Test-Gpu4090Unlock.ps1 -Benchmark). Needs the CUDA toolkit (13.1 used)
rem and Visual Studio 2022 Build Tools. The CUDA runtime is linked statically: the exe needs only the NVIDIA driver.
rem Source: gpu-unlock-ada diagnostics/bench/mma_bench.cu (GPL-2.0-only), unmodified.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
"%CUDA_PATH%\bin\nvcc.exe" -O3 -arch=sm_89 -std=c++17 -o "%~dp0mma_bench.exe" "%~dp0mma_bench.cu"
