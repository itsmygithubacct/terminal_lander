/* Banked PCM audio with a procedural fallback.
 *
 * Sound effects are baked procedurally at startup and replaced by the
 * WAV asset bank when it is present (several recorded variants per
 * effect, chosen at random without immediate repeats).  Transport and
 * mixing are delegated to the vendored pcm-mixer library: it probes for
 * a CLI sink (pacat, pw-play, aplay, play), mixes voices on its own
 * thread, and degrades to silence when no sink is available.
 */
#include "terminal_lander.h"
#include "pcm_mixer.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SR 44100
#define MAX_VOICES 20
#define LOOP_COUNT 2
#define MAX_SFX_VARIANTS 8

typedef struct { int16_t *data; int len; } Sample;

static Sample samples[SOUND_COUNT][MAX_SFX_VARIANTS];
static uint8_t sample_counts[SOUND_COUNT];
static uint8_t last_variants[SOUND_COUNT];
static pcmmix mixer;
static bool mixerStarted = false;
static bool soundEnabled = true;
static int loop_handles[LOOP_COUNT] = { -1, -1 };

static uint32_t srng = 0x71d15eedu;

static uint32_t sound_random_u32(void)
{
    srng ^= srng << 13;
    srng ^= srng >> 17;
    srng ^= srng << 5;
    return srng;
}

static float srandf(void)
{
    return (sound_random_u32() >> 8) * (1.0f / 16777216.0f);
}

static float snoise(void) { return srandf() * 2.0f - 1.0f; }

static float lpk(float c)
{
    return 1.0f - expf(-6.2831853f * c / SR);
}

static void clear_sample_bank(int id)
{
    if (id < 0 || id >= SOUND_COUNT) return;
    for (int variant = 0; variant < MAX_SFX_VARIANTS; variant++) {
        free(samples[id][variant].data);
        samples[id][variant] = (Sample){0};
    }
    sample_counts[id] = 0;
}

static void bake(int id, const float *src, int n, float peak, bool fade)
{
    float maxv = 1e-6f;
    for (int i = 0; i < n; i++)
        if (fabsf(src[i]) > maxv) maxv = fabsf(src[i]);

    int16_t *out = malloc((size_t)n * sizeof *out);
    if (!out) return;
    float gain = peak / maxv;
    for (int i = 0; i < n; i++) {
        float v = src[i] * gain;
        if (fade) {
            int fi = 80, fo = 400;
            if (i < fi) v *= (float)i / fi;
            if (n - i < fo) v *= (float)(n - i) / fo;
        }
        v = clampf(v, -1.0f, 1.0f);
        out[i] = (int16_t)(v * 32767.0f);
    }
    clear_sample_bank(id);
    samples[id][0].data = out;
    samples[id][0].len = n;
    sample_counts[id] = 1;
}

static void gen_thrust_main(void)
{
    int n = (int)(0.32f * SR);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    float lp = 0, ph1 = 0, ph2 = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        lp += lpk(950.0f) * (snoise() - lp);
        ph1 += 6.2831853f * 68.0f / SR;
        ph2 += 6.2831853f * 131.0f / SR;
        float trem = 0.78f + 0.22f * sinf(6.2831853f * 12.0f * t);
        s[i] = (lp * 0.85f + sinf(ph1) * 0.5f + sinf(ph2) * 0.18f) * trem;
    }
    bake(SND_THRUST_MAIN, s, n, 0.32f, false);
    free(s);
}

static void gen_thrust_side(void)
{
    int n = (int)(0.22f * SR);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    float lp = 0, ph = 0;
    for (int i = 0; i < n; i++) {
        lp += lpk(1800.0f) * (snoise() - lp);
        ph += 6.2831853f * 210.0f / SR;
        s[i] = lp * 0.55f + sinf(ph) * 0.30f;
    }
    bake(SND_THRUST_SIDE, s, n, 0.22f, false);
    free(s);
}

static void gen_crash(void)
{
    int n = (int)(1.0f * SR);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    float lp = 0, sub = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float cut = 3800.0f * expf(-t * 5.5f) + 90.0f;
        lp += lpk(cut) * (snoise() - lp);
        sub += 6.2831853f * (120.0f * expf(-t * 6.0f) + 35.0f) / SR;
        float env = expf(-t / 0.23f);
        s[i] = tanhf((lp * 1.6f + sinf(sub) * 1.0f) * env * 2.0f);
    }
    bake(SND_CRASH, s, n, 0.62f, true);
    free(s);
}

static void gen_landing(void)
{
    int n = (int)(0.65f * SR);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    static const float notes[4] = { 392.0f, 523.25f, 659.25f, 783.99f };
    for (int k = 0; k < 4; k++) {
        int start = (int)(k * 0.095f * SR);
        for (int i = 0; i < (int)(0.24f * SR) && start + i < n; i++) {
            float t = (float)i / SR;
            float ph = 6.2831853f * notes[k] * t;
            s[start + i] += sinf(ph) * expf(-t / 0.16f);
        }
    }
    bake(SND_LANDING, s, n, 0.32f, true);
    free(s);
}

static void gen_beep(int id, float f0, float f1, float dur, float peak)
{
    int n = (int)(dur * SR);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float f = f0 + (f1 - f0) * (t / dur);
        ph += 6.2831853f * f / SR;
        s[i] = sinf(ph) * expf(-t / (dur * 0.45f));
    }
    bake(id, s, n, peak, true);
    free(s);
}

static void synth_all(void)
{
    gen_thrust_main();
    gen_thrust_side();
    gen_crash();
    gen_landing();
    gen_beep(SND_BEEP, 1050.0f, 1280.0f, 0.10f, 0.23f);
    gen_beep(SND_WARNING, 420.0f, 210.0f, 0.22f, 0.25f);
    gen_beep(SND_MENU, 620.0f, 820.0f, 0.08f, 0.18f);
}

static const char *const sfx_files[SOUND_COUNT] = {
    [SND_THRUST_MAIN] = "sfx/thrust_main.wav",
    [SND_THRUST_SIDE] = "sfx/thrust_side.wav",
    [SND_CRASH] = "sfx/crash.wav",
    [SND_LANDING] = "sfx/landing.wav",
    [SND_BEEP] = "sfx/beep.wav",
    [SND_WARNING] = "sfx/warning.wav",
    [SND_MENU] = "sfx/menu.wav",
};

static char sound_asset_root[512] = "assets";

static void sound_asset_paths_init(void)
{
    const char *override = getenv("TERMINAL_LANDER_ASSETS");
    if (override && *override) {
        snprintf(sound_asset_root, sizeof sound_asset_root, "%s", override);
        return;
    }
    char executable[400];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof executable - 1);
    if (length <= 0) return;
    executable[length] = '\0';
    char *slash = strrchr(executable, '/');
    if (!slash) return;
    *slash = '\0';
    char candidate[512];
    snprintf(candidate, sizeof candidate, "%s/assets", executable);
    if (access(candidate, F_OK) == 0) {
        snprintf(sound_asset_root, sizeof sound_asset_root, "%s", candidate);
        return;
    }
    snprintf(candidate, sizeof candidate,
             "%s/../share/terminal-lander/assets", executable);
    if (access(candidate, F_OK) == 0)
        snprintf(sound_asset_root, sizeof sound_asset_root, "%s", candidate);
}

static bool variant_filename(const char *base, int variant, char *out, size_t size)
{
    if (variant == 0) return snprintf(out, size, "%s", base) < (int)size;
    const char *extension = strrchr(base, '.');
    if (!extension) return false;
    return snprintf(out, size, "%.*s_v%02d%s", (int)(extension - base), base,
                    variant + 1, extension) < (int)size;
}

static void load_external_sounds(void)
{
    sound_asset_paths_init();
    for (int id = 0; id < SOUND_COUNT; id++) {
        Sample loaded[MAX_SFX_VARIANTS] = {{0}};
        int count = 0;
        for (int variant = 0; variant < MAX_SFX_VARIANTS; variant++) {
            char relative[96], full[768];
            if (!variant_filename(sfx_files[id], variant, relative, sizeof relative))
                break;
            if (snprintf(full, sizeof full, "%s/%s", sound_asset_root, relative) >=
                (int)sizeof full)
                break;
            size_t frames = 0;
            char err[128];
            int16_t *data = pcmmix_wav_load(full, &frames, err, sizeof err);
            if (!data) break;
            if (frames == 0 || frames > (size_t)INT_MAX) {
                pcmmix_wav_free(data);
                break;
            }
            loaded[count++] = (Sample){data, (int)frames};
        }
        if (count == 0) continue;
        clear_sample_bank(id);
        for (int variant = 0; variant < count; variant++)
            samples[id][variant] = loaded[variant];
        sample_counts[id] = (uint8_t)count;
    }
}

static int choose_variant(int id)
{
    int count = sample_counts[id];
    if (count <= 1) return 0;
    int variant;
    do variant = (int)(sound_random_u32() % (uint32_t)count);
    while (variant == last_variants[id]);
    last_variants[id] = (uint8_t)variant;
    return variant;
}

bool sound_init(void)
{
    pcmmix_options options;

    synth_all();
    memset(last_variants, 0xff, sizeof last_variants);
    load_external_sounds();

    pcmmix_options_init(&options);
    options.max_voices = MAX_VOICES;
    if (!pcmmix_start(&mixer, &options)) return false;
    mixerStarted = true;
    for (int i = 0; i < LOOP_COUNT; i++) loop_handles[i] = -1;
    pcmmix_set_enabled(&mixer, soundEnabled);
    return true;
}

void sound_shutdown(void)
{
    if (mixerStarted) {
        pcmmix_stop(&mixer);
        mixerStarted = false;
    }
    for (int i = 0; i < LOOP_COUNT; i++) loop_handles[i] = -1;
    /* safe only after pcmmix_stop(): the mixer references sample memory
     * until it has been stopped */
    for (int i = 0; i < SOUND_COUNT; i++) clear_sample_bank(i);
}

void sound_set_enabled(bool on)
{
    soundEnabled = on;
    if (mixerStarted) pcmmix_set_enabled(&mixer, on);
}

bool sound_is_enabled(void)
{
    return mixerStarted ? pcmmix_is_enabled(&mixer) : soundEnabled;
}

void sound_play(int id, float vol, float pitch)
{
    if (id < 0 || id >= SOUND_COUNT || sample_counts[id] == 0) return;
    if (!mixerStarted) return;
    Sample *sample = &samples[id][choose_variant(id)];
    pcmmix_sample clip = { sample->data, (size_t)sample->len };
    (void)pcmmix_play(&mixer, &clip, clampf(vol, 0, 1.5f), pitch);
}

void sound_loop(int id, bool on, float vol, float pitch)
{
    if (id < 0 || id >= LOOP_COUNT || sample_counts[id] == 0) return;
    if (!mixerStarted) return;
    if (!on) {
        if (loop_handles[id] > 0)
            (void)pcmmix_voice_stop(&mixer, loop_handles[id]);
        loop_handles[id] = -1;
        return;
    }
    vol = clampf(vol, 0, 1.2f);
    if (pitch <= 0) pitch = 1.0f;
    /* an already-running loop is retuned in place, matching the old
     * mixer's keep-position semantics */
    if (loop_handles[id] > 0 &&
        pcmmix_voice_set(&mixer, loop_handles[id], vol, pitch))
        return;
    Sample *sample = &samples[id][choose_variant(id)];
    pcmmix_sample clip = { sample->data, (size_t)sample->len };
    loop_handles[id] = pcmmix_loop(&mixer, &clip, vol, pitch);
}
