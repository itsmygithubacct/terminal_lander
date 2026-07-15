# Asset provenance and licensing

## Runtime sound effects

The 21 WAV files under `assets/sfx/` form seven cue-specific variation banks:
steady main-engine and attitude-jet loops, a metal lunar-lander crash,
touchdown confirmation, instrument beep, hazard warning, and computer menu
movement. They were generated specifically for Terminal Lander with
ElevenLabs Text to Sound Effects v2 (`eleven_text_to_sound_v2`) during the
account owner's paid Starter subscription.

The production field contained 35 candidates. A pinned LAION CLAP model
provided semantic triage, and candidate score arrays were identical across two
independent runs. Signal-shape checks covered onset, event count, stationary
output, clipping, and tails. One-shots were onset-aligned, downmixed with equal
power, trimmed or padded to exact runtime length, DC/high-pass cleaned,
edge-faded, and reconstructed-peak gain-staged. Thrusters instead use stable
window selection and smooth equal-value loop-boundary preparation, without
fading the engine to silence. No procedural layer or library sample was mixed
into the masters; the C synthesizer remains only as a missing-asset fallback.

Runtime files are mono 44.1 kHz signed 16-bit PCM WAV. The game selects a new
variant when a thruster starts, preserves uninterrupted playback while thrust
continues, and avoids immediate repeats. All 21 files passed automated format,
duration, headroom, silence/DC, fade, duplicate, and loop-seam QA.

ElevenLabs states that qualifying paid-plan output may be used commercially
and indefinitely. Its service-specific restrictions still apply, including a
prohibition on standalone commercial distribution or licensing of Sound
Effects output. The WAVs are therefore excluded from this repository's MIT
grant and included only as bundled Terminal Lander content, not as a sample
pack or sound library.

Terms were checked on 2026-07-14: [paid-plan commercial
use](https://help.elevenlabs.io/hc/en-us/articles/13313564601361-Can-I-publish-the-content-I-generate-on-the-platform),
[Terms of Service](https://elevenlabs.io/terms-of-use), [Sound Effects
Terms](https://elevenlabs.io/sound-effects-terms), and [Prohibited Use
Policy](https://elevenlabs.io/use-policy). Exact prompts, source/final hashes,
selection metrics, loop settings, and mastering details are in
[`audio-provenance.json`](audio-provenance.json).

The embedded terminal font keeps the public-domain/permissive notice described
in the root `LICENSE` and `src/font8x16.h`.
