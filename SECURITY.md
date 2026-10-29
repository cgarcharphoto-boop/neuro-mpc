# Security Policy

NEURO MPC is an audio plugin that parses user-supplied audio files, project state and preset files, all inside host processes.

**Report vulnerabilities privately** through a GitHub private security advisory on this repository, not as public issues. Include the version or commit, the host, and a minimal file that reproduces the problem.

Current hardening:
- **State and presets:** malformed or truncated input is rejected without changing the current state (fuzz-tested in `EngineTests.cpp`).
- **Audio files:** decoded through JUCE's readers with a per-sample memory limit.
- **Network:** none; the plugin makes no network requests.
