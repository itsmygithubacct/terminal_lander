/*
 * Terminal Lander - kitty-protocol lunar lander.
 *
 * The layout follows Bashed Earth: a global game state, fixed-timestep
 * simulation, software-rendered RGBA framebuffer, and term.c presenting it
 * through the kitty graphics protocol.
 */
#ifndef TERMINAL_LANDER_H
#define TERMINAL_LANDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kitty_keyboard.h"

#define TICK_DT  (1.0f / 60.0f)
#define TICK_MS  16.666666f

#define MAX_PARTICLES 260
#define MAX_STARS     180

enum {
    KEY_ENTER = 1000, KEY_BACKSPACE, KEY_TAB, KEY_ESC,
    KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT
};

enum {
    GS_TITLE,
    GS_CONTROLS,
    GS_PLAYING,
    GS_CRASHING,
    GS_LEVEL_COMPLETE,
    GS_GAMEOVER
};

enum {
    DIFF_EASY,
    DIFF_MEDIUM,
    DIFF_HARD,
    DIFF_EXTRA_HARD,
    DIFF_COUNT
};

enum {
    PT_THRUST,
    PT_FIRE,
    PT_SPARK,
    PT_DEBRIS,
    PT_SMOKE
};

enum {
    SND_THRUST_MAIN,
    SND_THRUST_SIDE,
    SND_CRASH,
    SND_LANDING,
    SND_BEEP,
    SND_WARNING,
    SND_MENU,
    SOUND_COUNT
};

typedef struct {
    float x, y, vx, vy;
    float angle, angularVelocity;
    float fuel, maxFuel;
    float w, h;
    bool mainThrust, leftThrust, rightThrust;
    bool landed, crashed;
} Lander;

typedef struct {
    bool active;
    float x, y, vx, vy;
    float life, maxLife, size;
    uint32_t color;
    int type;
} Particle;

typedef struct {
    float x, y;
    uint8_t brightness;
    float phase, speed;
    int size;
} Star;

typedef struct {
    int x, width, points;
    float y;
} LandingPad;

typedef struct {
    int state;
    int W, H;
    bool quit, headless;

    Lander lander;
    LandingPad pad;
    float *terrain;
    int terrainW;

    Particle particles[MAX_PARTICLES];
    Star stars[MAX_STARS];
    int numStars;

    int score, level, lives;
    int difficulty;
    int frameCount;
    float levelTimer, crashTimer;
    float holdUp, holdLeft, holdRight;
    bool heldControls, heldUp, heldLeft, heldRight;
    float screenFlash, cameraShake;

    float scale;
    float gravity;
    float thrustPower;
    float sideThrustPower;
    float fuelMainRate;
    float fuelSideRate;
    float angularThrustPower;
    float angularDamping;
    float stabilizeStrength;
    float dragCoefficient;
    float maxAngle;
    float maxLandingAngle;
    float maxSafeSpeed;
    float maxSpin;
    float padGrace;

    uint32_t rng;
} GameState;

extern GameState G;
extern const char *DIFFICULTY_NAMES[DIFF_COUNT];

/* ---------- utilities / game ---------- */
void frand_seed(uint32_t seed);
float frandf(void);
float clampf(float v, float lo, float hi);

void game_init(int w, int h, uint32_t seed);
void game_shutdown(void);
void game_reset_to_title(void);
void game_start_run(void);
void game_create_level(void);
void game_tick(void);
void game_handle_key(int key);
void game_set_held_controls(bool available, bool up, bool left, bool right);
void game_autopilot_tick(void);

float terrain_height_at(float x);
float game_lander_speed(void);
float game_lander_altitude(void);
bool game_lander_can_land(void);
int game_landing_bonus(void);

/* ---------- render.c ---------- */
void render_init(int w, int h);
void render_shutdown(void);
void render_frame(void);
uint8_t *render_fb(void);

/* ---------- term.c ---------- */
bool term_init(int *outW, int *outH);
void term_present(const uint8_t *rgba, int w, int h);
int term_read_input(void);
bool term_next_key_event(kittykb_event *event);
bool term_key_down(uint32_t key);
bool term_has_release_events(void);
void term_shutdown(void);
void term_emergency_restore(void);

/* ---------- sound.c ---------- */
bool sound_init(void);
void sound_shutdown(void);
void sound_set_enabled(bool on);
bool sound_is_enabled(void);
void sound_play(int id, float vol, float pitch);
void sound_loop(int id, bool on, float vol, float pitch);

#endif
