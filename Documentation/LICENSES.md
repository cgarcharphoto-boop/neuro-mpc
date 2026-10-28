# Dependency Licenses

| Dependency | Version (commit) | License | How it's included |
|---|---|---|---|
| JUCE | 9.0.4 (3fb5cbc) | AGPLv3 / commercial | `Scripts/fetch-juce.sh`, not committed |
| Signalsmith Stretch | 1.4.0 (a670068) | MIT | vendored in `ThirdParty/signalsmith-stretch` |
| Signalsmith Linear | 0.6.4 (de55e6a) | MIT | vendored in `ThirdParty/signalsmith-linear` |
| VST3 SDK | bundled with JUCE | MIT (since 3.8) | through JUCE |

NEURO MPC itself is AGPL-3.0, because it is built against JUCE's open-source licence. MIT dependencies are compatible with AGPL. Their license files are kept in their directories.
