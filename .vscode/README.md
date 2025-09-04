# VS Code Configuration

This directory contains VS Code configuration files for debugging and building the Coro-Roro project.

## Launch Configurations

The `launch.json` file provides several debugging configurations:

### Test Executables
- **Debug Tests (Windows/macOS/Linux)** - Debug the main test suite (`corororo_tests`)
- **Debug Performance Tests (Windows/macOS/Linux)** - Debug performance tests (`coro_roro_perf_tests`)
- **Debug Simple Test (Windows/macOS/Linux)** - Debug the simple test executable (`single_file_test`)

### Platform Support
- **Windows**: Uses `cppvsdbg` debugger with Visual Studio debugger
- **macOS/Linux**: Uses `cppdbg` debugger with LLDB/GDB

## Tasks

The `tasks.json` file provides build and test tasks:

- **build** - Build the project using CMake
- **configure** - Configure the project with CMake
- **clean** - Clean the build directory
- **test** - Run the test suite

## Usage

1. Open the project in VS Code
2. Press `F5` or go to Run > Start Debugging
3. Select the appropriate configuration for your platform and target
4. The debugger will build the project and start debugging

## Requirements

- CMake 3.18+
- C++20 compiler
- VS Code with C/C++ extension
- Platform-specific debugger (Visual Studio debugger on Windows, LLDB on macOS, GDB on Linux)
