@echo off
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target coro_roro_tests -j8
timeout 1
start /b .\build\tracy-profiler.exe -a 127.0.0.1
timeout 1
.\build\test\Release\coro_roro_tests.exe
timeout 1
