# Terminal Lander

Kitty-protocol Lunar Lander in C: a software-rendered RGBA framebuffer,
zlib/base64 kitty graphics, exact press/release keyboard input, banked physical
sound effects, and a fixed-timestep game loop.

It follows the Bashed Earth terminal-rendering style while turning the Python
`terminal_lander.py` prototype into a native retro arcade game with lunar
terrain, flat scoring pads, fuel, lives, level progression, altitude warnings,
landing tolerances, and crash particles.

Built for Linux and kitty-protocol terminals such as kitty, ghostty, and
wezterm.

![Terminal Lander: a small lander descends toward a lit pad over lunar terrain](docs/screenshot.png)

## Features

- **Four difficulty presets** - Easy, Medium, Hard, and Extra Hard, with
  Extra Hard preserving the original prototype tuning
- **Assistive flight model** - lower presets add more fuel, wider pads,
  softer limits, stronger damping, and control stabilization
- **Retro software renderer** - pixel-art lander, stars, Earth backdrop,
  layered lunar terrain, HUD panels, pad guides, particles, and screen shake
- **Banked spacecraft sound** - three seamless variants for each thruster plus
  physical crash, touchdown, warning, confirmation, and menu cues; the original
  procedural synthesizer remains as a missing-asset fallback
- **Terminal-native presentation** - no SDL, no X11, no ncurses; frames are
  compressed and streamed through the kitty graphics protocol
- **Independent held controls** - Kitty keyboard press and release events keep
  simultaneous main and side thrust responsive, with a press-only fallback
  for terminals that do not report releases
- **Headless checks** - deterministic selftest and render-test modes for CI

## Build

Linux only. Needs gcc or clang, zlib, libm, pthreads, and a terminal that
supports the kitty graphics protocol:

```sh
make
./terminal-lander
```

Sound plays through the first available CLI sink (`pacat`, `pw-play`,
`aplay`, or sox's `play`). If no sink is found, the game runs silently.

## Controls

| Key | Action |
|-----|--------|
| Up / W | main thrust |
| Left / A | rotate left + side thrust |
| Right / D | rotate right + side thrust |
| Enter / Space | start, advance, restart |
| Esc | return to title during flight |
| Left / Right on title | change difficulty |
| 1-4 on title | Easy / Medium / Hard / Extra Hard |
| C | controls screen |
| Q | quit |

Difficulty presets change fuel, lives, pad width, terrain roughness, landing
tolerance, damping, and control assistance.

## Development

```sh
make test                              # deterministic headless checks
./terminal-lander --selftest 42 3600   # specific seed and tick count
./terminal-lander --render-test 7      # dump render_*.ppm screenshots
./terminal-lander --sound-test
```

`TERMINAL_LANDER_ASSETS=/path/to/assets` overrides runtime asset discovery for
packaging and installed-layout tests.

## Architecture

| File | Role |
|------|------|
| `src/term.c` | Kitty keyboard events, compatibility input, thin adapter over the kitty-framebuffer presenter |
| `src/game.c` | lander physics, terrain generation, pads, particles, difficulty, scoring |
| `src/render.c` | scene, HUD, and menu drawing over the soft-raster primitives |
| `src/sound.c` | strict PCM WAV banks, procedural fallback synthesis, playback through the pcm-mixer voices |
| `src/main.c` | interactive loop, selftest, render-test, sound-test |

Shared infrastructure is vendored under `third_party/`:

| Library | Role |
|---------|------|
| `third_party/kitty-framebuffer` | zlib + base64 kitty graphics presenter thread, raw mode, restore sequences |
| `third_party/soft-raster` | anti-aliased software rasterizer with the embedded 8x16 PSF font |
| `third_party/pcm-mixer` | CLI-sink audio transport, voice mixer, strict WAV loader |
| `third_party/kitty_keyboard` | kitty keyboard protocol decoding with press/release state |

## License

Code is MIT licensed; see [LICENSE](LICENSE). The ElevenLabs-generated WAVs in
`assets/sfx/` have a separate bundled-game asset exception and are not
standalone MIT samples. Full terms and artifact hashes are in [asset
provenance](docs/asset-sources.md). The embedded terminal font comes from
Debian console-setup's public-domain console fonts; details are preserved in
`third_party/soft-raster/src/font8x16.h`.
