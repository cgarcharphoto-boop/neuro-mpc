# Dependency Licenses

| Dependency | Version (commit) | License | How it's included |
|---|---|---|---|
| JUCE | 9.0.4 (3fb5cbc) | AGPLv3 / commercial | `Scripts/fetch-juce.sh`, not committed |
| Signalsmith Stretch | 1.4.0 (a670068) | MIT | vendored in `ThirdParty/signalsmith-stretch` |
| Signalsmith Linear | 0.6.4 (de55e6a) | MIT | vendored in `ThirdParty/signalsmith-linear` |
| VST3 SDK | bundled with JUCE | MIT (since 3.8) | through JUCE |
| demucs.cpp | f1206e9 | MIT | vendored in `ThirdParty/demucs.cpp` (`src/`, LICENSE) |
| Eigen | dd8c71e | MPL-2.0 | fetched by CMake (FetchContent, SHA-256 pinned), header-only, unmodified |
| Demucs model `htdemucs_ft` drums (ggml f16, converted for demucs.cpp) | HF `Retrobear/demucs.cpp` @ 8f58ac0 | MIT (Meta, Demucs) | **not distributed**: downloaded by the user on first use, SHA-256 verified |

NEURO MPC itself is AGPL-3.0, because it is built against JUCE's open-source licence. MIT dependencies are compatible with AGPL. Their license files are kept in their directories.
