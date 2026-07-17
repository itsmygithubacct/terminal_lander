# Asset provenance and licensing

## Runtime sound effects

The 21 WAV files under `assets/sfx/` form seven cue-specific variation banks:
steady main-engine and attitude-jet loops, a metal lunar-lander crash,
touchdown confirmation, instrument beep, hazard warning, and computer menu
movement. They are deterministic local renders from two
`python_sound_assets` packages. `aircraft_spacecraft_generator` supplies the
thrusters, crash, and landing; `ui_game_state_generator` supplies the beep,
warning, and menu cues. Thrusters and UI cues are fully procedural. Crash and
landing use CC0 1.0/public-domain excerpts from Kenney's Impact Sounds and
rubberduck's metal, impact, breaking, and falling collections. The C
synthesizer remains only as a missing-asset fallback.

The aircraft generator's complete admissible source bank also contains
egomassive's CC0 `Tire.ogg`. Terminal Lander uses the `lander` craft profile,
whose pad-gear touchdown branch never loads that tire recording. It is credited
in `audio-provenance.json` as a bank-only source and is not attributed to crash,
landing, or any runtime artifact, because no shipped WAV contains it.

Runtime files are mono 44.1 kHz signed 16-bit PCM WAV. The game selects a new
variant when a thruster starts, preserves uninterrupted playback while thrust
continues, and avoids immediate repeats. All 21 files passed automated format,
duration, headroom, silence/DC, fade, duplicate, and loop-seam QA.

All recorded inputs are CC0 1.0/public-domain material and the remaining layers
are procedural, so no upstream provider terms restrict the current WAV bank.
The files are included in the repository's MIT grant. The old ElevenLabs bank
and its service-specific restrictions were replaced in full; none of that audio
remains. Exact generator commands, cue options, source pages, source/final
hashes, and per-artifact source lists are in
[`audio-provenance.json`](audio-provenance.json).

The embedded terminal font keeps the public-domain/permissive notice described
in the root `LICENSE` and `third_party/soft-raster/src/font8x16.h`.
