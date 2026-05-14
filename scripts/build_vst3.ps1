$ErrorActionPreference = "Stop"

$cmake = "C:\Program Files\CMake\bin\cmake.exe"
if (!(Test-Path $cmake)) {
    throw "CMake was not found at $cmake"
}

& $cmake -S . -B build-juce -G "Visual Studio 17 2022" -A x64 -DBUILD_JUCE_PLUGIN=ON
& $cmake --build build-juce --config Release --target HyperFrame_VST3

$plugin = Join-Path $PSScriptRoot "..\build-juce\HyperFrame_artefacts\Release\VST3\HyperFrame.vst3"
if (!(Test-Path $plugin)) {
    throw "Expected VST3 was not found at $plugin"
}

Write-Host "Built VST3: $plugin"
