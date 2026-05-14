# Wrapper Decision

## Decision

Use JUCE as the first plug-in wrapper stack and build VST3 first.

Status: Accepted.

## Why JUCE First

- The instrument needs a drawable waveform editor early.
- JUCE provides VST3 wrapping, host parameter handling, MIDI handling, and UI
  primitives in one stack.
- The DSP core remains framework-neutral, so this decision does not trap the
  sound engine inside JUCE.
- CLAP, standalone, or a direct VST3 SDK wrapper can be added later if needed.

## Tradeoffs

JUCE is not as license-neutral as the current VST3 SDK. For internal
development this is acceptable, but release planning must decide between a JUCE
commercial licence and an open-source licence compatible with JUCE terms.

Direct VST3 SDK remains attractive for a smaller binary and simpler licensing,
but it would slow down the waveform editor and preset UI work.

## Current Build Decision

- Core DSP target: always built.
- JUCE plug-in target: optional behind `BUILD_JUCE_PLUGIN`.
- JUCE version: pinned to `8.0.12`.
- First plug-in format: VST3 only.

## Build Commands

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' -S . -B build-vs2022 -G 'Visual Studio 17 2022' -A x64
& 'C:\Program Files\CMake\bin\cmake.exe' --build build-vs2022 --config Release
```

With the JUCE wrapper:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' -S . -B build-juce -G 'Visual Studio 17 2022' -A x64 -DBUILD_JUCE_PLUGIN=ON
& 'C:\Program Files\CMake\bin\cmake.exe' --build build-juce --config Release --target HyperFrame_VST3
```

## Sources Checked

- JUCE supports CMake integration and plug-in formats including VST3.
- JUCE currently lists Visual Studio 2019 or newer as the Windows build
  requirement.
- JUCE licensing requires a release-time decision for closed-source commercial
  distribution.
