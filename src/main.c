/* Entry point: terminal setup, fixed timestep, and headless checks. */
#include "terminal_lander.h"
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void on_signal(int sig)
{
    (void)sig;
    term_emergency_restore();
    _exit(1);
}

static bool event_matches_letter(const kittykb_event *event, char lower)
{
    char upper = (char)(lower - 'a' + 'A');
    return kittykb_event_matches_key(event, (uint32_t)(unsigned char)lower) ||
           kittykb_event_matches_key(event, (uint32_t)(unsigned char)upper);
}

static int game_key_from_event(const kittykb_event *event)
{
    static const char letters[] = "acdqw";
    for (size_t i = 0; i < sizeof letters - 1; i++)
        if (event_matches_letter(event, letters[i])) return letters[i];

    switch (event->key) {
    case KITTYKB_KEY_ENTER: return KEY_ENTER;
    case KITTYKB_KEY_BACKSPACE: return KEY_BACKSPACE;
    case KITTYKB_KEY_TAB: return KEY_TAB;
    case KITTYKB_KEY_ESCAPE: return KEY_ESC;
    case KITTYKB_KEY_UP: return KEY_UP;
    case KITTYKB_KEY_DOWN: return KEY_DOWN;
    case KITTYKB_KEY_RIGHT: return KEY_RIGHT;
    case KITTYKB_KEY_LEFT: return KEY_LEFT;
    default:
        return event->key <= (uint32_t)INT_MAX ? (int)event->key : -1;
    }
}

static bool gameplay_control_key(int key)
{
    return key == KEY_UP || key == KEY_LEFT || key == KEY_RIGHT ||
           key == 'w' || key == 'a' || key == 'd';
}

static bool interrupt_event(const kittykb_event *event)
{
    return event->key == 3u ||
           (event_matches_letter(event, 'c') &&
            (event->modifiers & KITTYKB_MOD_CTRL) != 0u);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void sleep_ms(double ms)
{
    if (ms <= 0) return;
    struct timespec ts = { (time_t)(ms / 1000), (long)(fmod(ms, 1000.0) * 1e6) };
    nanosleep(&ts, NULL);
}

static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", G.W, G.H);
    const uint8_t *p = render_fb();
    for (int i = 0; i < G.W * G.H; i++)
        fwrite(p + i * 4, 1, 3, f);
    fclose(f);
    printf("wrote %s\n", path);
}

static int selftest(unsigned seed, int ticks)
{
    if (ticks <= 0) ticks = 3600;
    game_init(1000, 640, seed);
    int stabilityState = GS_TITLE;
    for (int i = 0; i < ticks; i++) {
        game_autopilot_tick();
        game_tick();
        Lander *l = &G.lander;
        if (isnan(l->x) || isnan(l->y) || isnan(l->vx) || isnan(l->vy) ||
            isnan(l->angle)) {
            printf("FAIL: NaN at tick %d\n", i);
            game_shutdown();
            return 1;
        }
        if (G.state == GS_GAMEOVER) break;
    }
    stabilityState = G.state;

    game_start_run();
    G.lander.x = G.pad.x + G.pad.width / 2.0f - G.lander.w / 2.0f;
    G.lander.y = G.pad.y - G.lander.h - 5 * G.scale;
    G.lander.vx = 0;
    G.lander.vy = 18 * G.scale;
    G.lander.angle = 0;
    G.lander.angularVelocity = 0;
    for (int i = 0; i < 20 && G.state == GS_PLAYING; i++)
        game_tick();
    if (G.state != GS_LEVEL_COMPLETE || G.score <= 0) {
        printf("FAIL: safe landing did not score (state=%d score=%d)\n", G.state, G.score);
        game_shutdown();
        return 1;
    }
    int landingScore = G.score;

    for (int d = 0; d < DIFF_COUNT; d++) {
        G.difficulty = d;
        game_start_run();
        G.lander.x = G.pad.x + G.pad.width / 2.0f - G.lander.w / 2.0f;
        G.lander.y = G.pad.y - G.lander.h - 5 * G.scale;
        G.lander.vx = 0;
        G.lander.vy = 18 * G.scale;
        G.lander.angle = 0;
        G.lander.angularVelocity = 0;
        for (int i = 0; i < 20 && G.state == GS_PLAYING; i++)
            game_tick();
        if (G.state != GS_LEVEL_COMPLETE || G.score <= 0) {
            printf("FAIL: %s safe landing failed (state=%d score=%d)\n",
                   DIFFICULTY_NAMES[d], G.state, G.score);
            game_shutdown();
            return 1;
        }
    }

    printf("PASS: seed=%u ticks=%d stability_state=%d safe_landing_score=%d difficulties=%d\n",
           seed, ticks, stabilityState, landingScore, DIFF_COUNT);
    game_shutdown();
    return 0;
}

static int input_test(void)
{
    int failures = 0;
#define EXPECT(condition, label) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", label); failures++; } \
    else printf("PASS: %s\n", label); \
} while (0)
    game_init(1000, 640, 1337);
    game_start_run();
    G.lander.x = G.W * 0.5f;
    G.lander.y = G.H * 0.1f;
    G.lander.vx = G.lander.vy = G.lander.angularVelocity = 0;
    G.lander.angle = 0;

    game_set_held_controls(true, true, true, false);
    game_tick();
    EXPECT(G.lander.mainThrust && G.lander.leftThrust && !G.lander.rightThrust,
           "main and side thrust apply simultaneously");

    game_set_held_controls(true, false, true, true);
    game_tick();
    EXPECT(!G.lander.leftThrust && !G.lander.rightThrust,
           "simultaneous opposite side controls cancel");

    game_set_held_controls(true, true, false, true);
    game_tick();
    EXPECT(G.lander.mainThrust && !G.lander.leftThrust && G.lander.rightThrust,
           "releasing one side preserves the other held controls");

    game_set_held_controls(true, false, false, true);
    game_tick();
    EXPECT(!G.lander.mainThrust && G.lander.rightThrust,
           "main thrust release does not release side thrust");

    game_set_held_controls(false, false, false, false);
    game_handle_key('w');
    game_handle_key('a');
    game_tick();
    EXPECT(G.lander.mainThrust && G.lander.leftThrust,
           "legacy press-only fallback retains simultaneous latch input");
    for (int i = 0; i < 6; i++) game_tick();
    EXPECT(!G.lander.mainThrust && !G.lander.leftThrust,
           "legacy input latches expire without release events");

    game_shutdown();
#undef EXPECT
    return failures ? 1 : 0;
}

static int render_test(unsigned seed)
{
    game_init(1000, 640, seed);
    render_init(G.W, G.H);

    render_frame();
    dump_ppm("render_title.ppm");

    game_start_run();
    for (int i = 0; i < 180; i++) {
        game_autopilot_tick();
        game_tick();
    }
    render_frame();
    dump_ppm("render_playing.ppm");

    G.lander.x = G.pad.x - G.lander.w * 2.2f;
    G.lander.y = terrain_height_at(G.lander.x) - G.lander.h - 2;
    G.lander.vy = 300 * G.scale;
    G.lander.angle = 0.7f;
    game_tick();
    for (int i = 0; i < 18; i++) game_tick();
    render_frame();
    dump_ppm("render_crash.ppm");

    game_start_run();
    G.lander.x = G.pad.x + G.pad.width / 2.0f - G.lander.w / 2.0f;
    G.lander.y = G.pad.y - G.lander.h;
    G.lander.vx = G.lander.vy = G.lander.angularVelocity = 0;
    G.lander.angle = 0;
    G.lander.landed = true;
    G.score += G.pad.points + 100;
    G.state = GS_LEVEL_COMPLETE;
    render_frame();
    dump_ppm("render_landed.ppm");

    render_shutdown();
    game_shutdown();
    return 0;
}

static int sound_test(void)
{
    bool ok = sound_init();
    if (!ok)
        printf("sound-test: no supported audio sink found; game will run silent\n");
    else
        printf("sound-test: playing every production sound bank\n");

    sound_play(SND_MENU, 0.6f, 1.0f);
    sleep_ms(180);
    sound_play(SND_BEEP, 0.65f, 1.0f);
    sleep_ms(200);
    sound_play(SND_WARNING, 0.65f, 1.0f);
    sleep_ms(330);
    sound_loop(SND_THRUST_MAIN, true, 0.55f, 1.0f);
    sleep_ms(700);
    sound_loop(SND_THRUST_SIDE, true, 0.35f, 1.1f);
    sleep_ms(500);
    sound_loop(SND_THRUST_SIDE, false, 0, 1);
    sound_loop(SND_THRUST_MAIN, false, 0, 1);
    sound_play(SND_LANDING, 0.8f, 1.0f);
    sleep_ms(800);
    sound_play(SND_CRASH, 0.7f, 0.9f);
    sleep_ms(1000);
    sound_shutdown();
    return ok ? 0 : 0;
}

static int run_interactive(void)
{
    int w, h;
    if (!term_init(&w, &h)) {
        fprintf(stderr, "terminal-lander: needs an interactive kitty-protocol terminal\n");
        fprintf(stderr, "or run --selftest / --render-test.\n");
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    atexit(term_shutdown);

    game_init(w, h, (uint32_t)time(NULL));
    render_init(w, h);
    sound_init();

    const double frameMs = 1000.0 / 30.0;
    double next = now_ms();

    while (!G.quit) {
        kittykb_event event;
        if (term_read_input() < 0) {
            G.quit = true;
            break;
        }
        bool heldInput = term_has_release_events();
        while (term_next_key_event(&event)) {
            if (event.action != KITTYKB_ACTION_PRESS) continue;
            if (interrupt_event(&event)) {
                G.quit = true;
                continue;
            }
            int key = game_key_from_event(&event);
            if (key < 0 || (heldInput && G.state == GS_PLAYING &&
                            gameplay_control_key(key))) continue;
            game_handle_key(key);
        }
        if (G.quit) break;
        game_set_held_controls(
            heldInput,
            term_key_down('w') || term_key_down(KITTYKB_KEY_UP),
            term_key_down('a') || term_key_down(KITTYKB_KEY_LEFT),
            term_key_down('d') || term_key_down(KITTYKB_KEY_RIGHT));

        game_tick();
        game_tick();

        render_frame();
        term_present(render_fb(), G.W, G.H);

        next += frameMs;
        double wait = next - now_ms();
        if (wait < -100) next = now_ms();
        sleep_ms(wait);
    }

    sound_shutdown();
    render_shutdown();
    game_shutdown();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--selftest")) {
        unsigned seed = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 1337;
        int ticks = argc > 3 ? atoi(argv[3]) : 3600;
        return selftest(seed, ticks);
    }
    if (argc > 1 && !strcmp(argv[1], "--input-test")) {
        return input_test();
    }
    if (argc > 1 && !strcmp(argv[1], "--render-test")) {
        unsigned seed = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 1337;
        return render_test(seed);
    }
    if (argc > 1 && !strcmp(argv[1], "--sound-test")) {
        return sound_test();
    }
    if (argc > 1 && !strcmp(argv[1], "--version")) {
        printf("terminal-lander 0.1.0\n");
        return 0;
    }
    return run_interactive();
}
