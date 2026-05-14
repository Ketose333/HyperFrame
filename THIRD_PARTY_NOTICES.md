# Third-Party Notices

This file summarizes third-party components used by HyperFrame v0.1.0.

HyperFrame itself is distributed under `AGPL-3.0-only`; see `LICENSE`.

## JUCE 8.0.12

- Source: https://github.com/juce-framework/JUCE
- Version: 8.0.12
- Use in HyperFrame: VST3 wrapper, plug-in parameters, MIDI/audio processing glue,
  and editor UI.
- Licence: JUCE modules are dual-licensed under AGPLv3 and the commercial JUCE
  licence. HyperFrame uses JUCE under the AGPLv3 option.
- Local licence file after configure:
  `build-juce/_deps/juce-src/LICENSE.md`

JUCE's licence file also lists bundled dependencies used by JUCE modules,
including the VST3 SDK under MIT terms.

## liblsdj

- Source: https://github.com/stijnfrishert/liblsdj
- Commit: 6023c4e48ad8280abacfddba60f2689e2442d79c
- Use in HyperFrame: parsing user-provided LSDJ song/save/instrument data for
  Wave bank import.
- Licence: MIT
- Local licence file after configure:
  `build-juce/_deps/liblsdj-src/LICENSE`

MIT License

Copyright (c) 2018 Stijn Frishert

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
