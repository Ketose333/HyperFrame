# MVP Scope

## Included In v0.1

- Framework-neutral DSP core.
- One polyphonic drawable wavetable oscillator.
- Active waveform length from 8 to 512 points.
- Amplitude quantization from 2-bit to 8-bit.
- Nearest and linear playback.
- Normalize and smooth wave operations.
- ADSR envelope.
- Preset model for Draw and Wave roles.
- VST3 wrapper decision documented but not locked until the core is stable.

## Explicitly Deferred

- Full PC-98 sound-chip register emulation.
- Full register-level tone/noise/wavetable architecture.
- Built-in rhythm ROM samples.
- Built-in copyrighted game/sample material.
- MPE.
- Microtuning.
- Advanced modulation matrix.
- Built-in arpeggiator.

## Plugin Target

Primary target: VST3 instrument.

Recommended implementation path:

1. Build and test the DSP core independently.
2. Add a minimal plug-in wrapper.
3. Add UI with drawable waveform editing.
4. Add presets.
5. Expand character controls only after the simple instrument feels good.

## Technical Bias

Keep the core portable C++ first. Avoid tying sound generation to JUCE, VST3 SDK,
or any UI toolkit. This makes it easier to ship VST3, CLAP, standalone, or test
tools later.
