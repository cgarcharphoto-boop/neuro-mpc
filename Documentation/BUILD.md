# Building NEURO MPC

## Requirements (tested)
- macOS 26 on Apple Silicon (arm64); deployment target macOS 11
- Xcode 26.1.1 / Apple clang 17, CMake ≥ 3.22 (tested with 4.3.2), git

## Build
```sh
Scripts/build.sh                 # = fetch-juce.sh + configure + build + ctest + auval
```
or by hand:
```sh
Scripts/fetch-juce.sh            # clones JUCE 9.0.4 (3fb5cbc) into ./JUCE
cmake -S . -B build -G Xcode     # optional: -DJUCE_PATH=/path/to/JUCE-9.0.4
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
**Outputs** (also copied to `~/Library/Audio/Plug-Ins/{VST3,Components}` after the build):
- `build/NEURO_MPC_artefacts/Release/VST3/NEURO MPC.vst3`
- `build/NEURO_MPC_artefacts/Release/AU/NEURO MPC.component`
- `build/NEURO_MPC_artefacts/Release/Standalone/NEURO MPC.app`

**Validate the AU:** `killall -9 AudioComponentRegistrar; auval -v aumu Nm26 Nrmp`

## Dependencies
| Dependency | How it's obtained |
|---|---|
| JUCE 9.0.4 | `Scripts/fetch-juce.sh` (pinned tag) |
| Signalsmith Stretch 1.4.0, Signalsmith Linear 0.6.4 | vendored in `ThirdParty/` (MIT) |

No private credentials or services are needed. See `LICENSES.md`.

## Release builds
Builds are arm64 only; for Intel support, set `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. To distribute outside your own machine:
1. Sign with a Developer ID certificate: `codesign --deep --timestamp --options runtime -s "Developer ID Application: …"` on each bundle.
2. Notarise with `xcrun notarytool submit … --wait` and staple with `xcrun stapler staple`.
3. Ship the source with the binaries, or a written offer of it (AGPL-3.0).
