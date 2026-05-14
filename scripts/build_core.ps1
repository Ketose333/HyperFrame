$ErrorActionPreference = "Stop"

$cmake = "C:\Program Files\CMake\bin\cmake.exe"
if (!(Test-Path $cmake)) {
    throw "CMake was not found at $cmake"
}

& $cmake -S . -B build-vs2022 -G "Visual Studio 17 2022" -A x64
& $cmake --build build-vs2022 --config Release
& ".\build-vs2022\Release\core_smoke_test.exe"

