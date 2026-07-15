/* Game logic: lander physics, procedural terrain, input state, scoring. */
#include "terminal_lander.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

GameState G;
static float lowFuelCooldown = 0;
static float altitudeCooldown = 0;

const char *DIFFICULTY_NAMES[DIFF_COUNT] = {
    "Easy", "Medium", "Hard", "Extra Hard"
};

typedef struct {
    float gravity, thrust, sideThrust;
    float fuel, minFuel, fuelDrop, fuelMainRate, fuelSideRate;
    float safeSpeed, safeAngle;
    float angularThrust, angularDamping, stabilizer, drag, maxSpin;
    int lives, initialPadCells, minPadMargin, roughnessRate;
    float roughnessScale, padGrace;
} Difficulty;

static const Difficulty DIFF[DIFF_COUNT] = {
    [DIFF_EASY] = {
        .gravity = 120, .thrust = 460, .sideThrust = 190,
        .fuel = 280, .minFuel = 120, .fuelDrop = 0.5f,
        .fuelMainRate = 7.0f, .fuelSideRate = 2.5f,
        .safeSpeed = 180, .safeAngle = 0.40f,
        .angularThrust = 5.0f, .angularDamping = 0.74f,
        .stabilizer = 3.1f, .drag = 0.9945f, .maxSpin = 1.30f,
        .lives = 6, .initialPadCells = 24, .minPadMargin = 9,
        .roughnessRate = 6, .roughnessScale = 0.42f, .padGrace = 12.0f
    },
    [DIFF_MEDIUM] = {
        .gravity = 145, .thrust = 445, .sideThrust = 180,
        .fuel = 220, .minFuel = 75, .fuelDrop = 1.0f,
        .fuelMainRate = 8.5f, .fuelSideRate = 3.2f,
        .safeSpeed = 150, .safeAngle = 0.31f,
        .angularThrust = 5.5f, .angularDamping = 0.80f,
        .stabilizer = 2.4f, .drag = 0.9955f, .maxSpin = 1.65f,
        .lives = 5, .initialPadCells = 20, .minPadMargin = 6,
        .roughnessRate = 4, .roughnessScale = 0.62f, .padGrace = 8.0f
    },
    [DIFF_HARD] = {
        .gravity = 170, .thrust = 430, .sideThrust = 170,
        .fuel = 175, .minFuel = 45, .fuelDrop = 1.5f,
        .fuelMainRate = 9.4f, .fuelSideRate = 3.7f,
        .safeSpeed = 125, .safeAngle = 0.24f,
        .angularThrust = 5.9f, .angularDamping = 0.84f,
        .stabilizer = 1.75f, .drag = 0.9960f, .maxSpin = 2.0f,
        .lives = 4, .initialPadCells = 17, .minPadMargin = 4,
        .roughnessRate = 3, .roughnessScale = 0.82f, .padGrace = 5.0f
    },
    [DIFF_EXTRA_HARD] = {
        .gravity = 190, .thrust = 405, .sideThrust = 150,
        .fuel = 150, .minFuel = 30, .fuelDrop = 2.0f,
        .fuelMainRate = 10.0f, .fuelSideRate = 4.0f,
        .safeSpeed = 105, .safeAngle = 0.20f,
        .angularThrust = 6.2f, .angularDamping = 0.88f,
        .stabilizer = 1.15f, .drag = 0.9965f, .maxSpin = 2.4f,
        .lives = 3, .initialPadCells = 15, .minPadMargin = 2,
        .roughnessRate = 2, .roughnessScale = 1.0f, .padGrace = 2.5f
    },
};

static const Difficulty *difficulty(void)
{
    if (G.difficulty < 0 || G.difficulty >= DIFF_COUNT) G.difficulty = DIFF_MEDIUM;
    return &DIFF[G.difficulty];
}

void frand_seed(uint32_t seed)
{
    G.rng = seed ? seed : 0x9e3779b9u;
}

float frandf(void)
{
    G.rng ^= G.rng << 13;
    G.rng ^= G.rng >> 17;
    G.rng ^= G.rng << 5;
    return (G.rng >> 8) * (1.0f / 16777216.0f);
}

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static float smoothstep(float t)
{
    t = clampf(t, 0, 1);
    return t * t * (3 - 2 * t);
}

static float px_scale(void)
{
    return clampf(G.H / 640.0f, 0.55f, 1.8f);
}

static void configure_physics(void)
{
    const Difficulty *d = difficulty();
    G.scale = px_scale();
    G.gravity = d->gravity * G.scale;
    G.thrustPower = d->thrust * G.scale;
    G.sideThrustPower = d->sideThrust * G.scale;
    G.fuelMainRate = d->fuelMainRate;
    G.fuelSideRate = d->fuelSideRate;
    G.angularThrustPower = d->angularThrust;
    G.angularDamping = d->angularDamping;
    G.stabilizeStrength = d->stabilizer;
    G.dragCoefficient = d->drag;
    G.maxAngle = (float)M_PI / 2.0f;
    G.maxLandingAngle = d->safeAngle;
    G.maxSafeSpeed = d->safeSpeed * G.scale;
    G.maxSpin = d->maxSpin;
    G.padGrace = d->padGrace * G.scale;
}

static void generate_stars(void)
{
    G.numStars = MAX_STARS;
    for (int i = 0; i < G.numStars; i++) {
        Star *s = &G.stars[i];
        s->x = frandf() * G.W;
        s->y = frandf() * G.H * 0.56f;
        s->brightness = (uint8_t)(70 + frandf() * 170);
        s->phase = frandf() * 6.28318f;
        s->speed = 0.6f + frandf() * 2.6f;
        s->size = frandf() > 0.90f ? 2 : 1;
    }
}

void game_init(int w, int h, uint32_t seed)
{
    memset(&G, 0, sizeof G);
    G.W = w;
    G.H = h;
    G.difficulty = DIFF_MEDIUM;
    frand_seed(seed);
    configure_physics();
    G.terrain = malloc((size_t)w * sizeof *G.terrain);
    G.terrainW = w;
    generate_stars();
    G.level = 1;
    G.lives = 3;
    game_create_level();
    game_reset_to_title();
}

void game_shutdown(void)
{
    sound_loop(SND_THRUST_MAIN, false, 0, 1);
    sound_loop(SND_THRUST_SIDE, false, 0, 1);
    free(G.terrain);
    G.terrain = NULL;
    G.terrainW = 0;
}

void game_reset_to_title(void)
{
    G.state = GS_TITLE;
    G.holdUp = G.holdLeft = G.holdRight = 0;
    G.levelTimer = G.crashTimer = 0;
    G.screenFlash = G.cameraShake = 0;
    sound_loop(SND_THRUST_MAIN, false, 0, 1);
    sound_loop(SND_THRUST_SIDE, false, 0, 1);
    memset(G.particles, 0, sizeof G.particles);
}

static float starting_fuel_for_level(int level)
{
    const Difficulty *d = difficulty();
    float fuel = d->fuel - (level - 1) * d->fuelDrop;
    return fuel < d->minFuel ? d->minFuel : fuel;
}

static int pad_width_for_level(int level)
{
    const Difficulty *d = difficulty();
    float cell = G.W / 120.0f;
    int initialCells = d->initialPadCells;
    int minCells = 4 + d->minPadMargin;
    int levelsToMin = initialCells - minCells;
    int widthCells;
    if (level <= levelsToMin)
        widthCells = initialCells - (level - 1);
    else
        widthCells = minCells - ((level - levelsToMin - 1) / 10);

    int absoluteMin = (int)(G.lander.w + 6 * G.scale);
    int width = (int)(widthCells * cell);
    return width < absoluteMin ? absoluteMin : width;
}

static void generate_terrain(void)
{
    if (!G.terrain || G.terrainW != G.W) return;
    const Difficulty *d = difficulty();

    float base = G.H * 0.79f;
    float rough = (8.0f + (G.level - 1) / (float)d->roughnessRate)
                * (G.H / 80.0f) * d->roughnessScale;
    float phase = frandf() * 1000.0f;
    float n0 = frandf() * 2 - 1;
    float n1 = n0;

    for (int x = 0; x < G.W; x++) {
        if ((x % 32) == 0) {
            n0 = n1;
            n1 = frandf() * 2 - 1;
        }
        float nt = smoothstep((x % 32) / 32.0f);
        float noise = lerpf(n0, n1, nt) * rough * 0.55f;
        float y = base;
        y += sinf((x + phase) * 0.006f) * rough * 1.65f;
        y += sinf((x + phase) * 0.018f) * rough * 0.90f;
        y += sinf((x + phase) * 0.047f) * rough * 0.38f;
        y += noise;
        G.terrain[x] = clampf(y, G.H * 0.42f, G.H - 24.0f * G.scale);
    }

    G.pad.width = pad_width_for_level(G.level);
    int margin = (int)(G.W * 0.12f);
    int span = G.W - margin * 2 - G.pad.width;
    if (span < 1) span = 1;
    G.pad.x = margin + (int)(frandf() * span);
    int pc = G.pad.x + G.pad.width / 2;
    G.pad.y = clampf(G.terrain[pc] + (frandf() - 0.5f) * rough * 0.45f,
                     G.H * 0.48f, G.H - 34.0f * G.scale);
    G.pad.points = G.pad.width <= (int)(G.lander.w + 18 * G.scale) ? 100 : 50;

    int approach = (int)(44 * G.scale);
    if (approach < 18) approach = 18;
    int left = G.pad.x;
    int right = G.pad.x + G.pad.width;
    for (int x = left; x <= right && x < G.W; x++)
        if (x >= 0) G.terrain[x] = G.pad.y;

    for (int d = 1; d <= approach; d++) {
        float t = smoothstep((float)d / approach);
        int lx = left - d;
        int rx = right + d;
        if (lx >= 0) G.terrain[lx] = lerpf(G.pad.y, G.terrain[lx], t);
        if (rx < G.W) G.terrain[rx] = lerpf(G.pad.y, G.terrain[rx], t);
    }
}

void game_create_level(void)
{
    configure_physics();
    G.lander.w = clampf(20.0f * G.scale, 14.0f, 34.0f);
    G.lander.h = G.lander.w * 1.06f;
    generate_terrain();

    float startX = G.W * (0.16f + frandf() * 0.68f);
    if (startX > G.pad.x - G.lander.w && startX < G.pad.x + G.pad.width + G.lander.w)
        startX = G.pad.x > G.W / 2 ? G.W * 0.20f : G.W * 0.74f;

    Lander *l = &G.lander;
    memset(l, 0, sizeof *l);
    l->w = clampf(20.0f * G.scale, 14.0f, 34.0f);
    l->h = l->w * 1.06f;
    l->x = clampf(startX, 20.0f * G.scale, G.W - l->w - 20.0f * G.scale);
    l->y = G.H * 0.12f;
    l->fuel = l->maxFuel = starting_fuel_for_level(G.level);

    G.holdUp = G.holdLeft = G.holdRight = 0;
    G.levelTimer = G.crashTimer = 0;
    lowFuelCooldown = 0;
    altitudeCooldown = 0;
    sound_loop(SND_THRUST_MAIN, false, 0, 1);
    sound_loop(SND_THRUST_SIDE, false, 0, 1);
    memset(G.particles, 0, sizeof G.particles);
}

void game_start_run(void)
{
    G.score = 0;
    G.level = 1;
    G.lives = difficulty()->lives;
    G.frameCount = 0;
    G.screenFlash = G.cameraShake = 0;
    game_create_level();
    G.state = GS_PLAYING;
    sound_play(SND_MENU, 0.45f, 1.0f);
}

static Particle *push_particle(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!G.particles[i].active) {
            memset(&G.particles[i], 0, sizeof G.particles[i]);
            G.particles[i].active = true;
            return &G.particles[i];
        }
    }
    return NULL;
}

static void add_particle(float x, float y, float vx, float vy, float life,
                         float size, uint32_t color, int type)
{
    Particle *p = push_particle();
    if (!p) return;
    p->x = x;
    p->y = y;
    p->vx = vx;
    p->vy = vy;
    p->life = p->maxLife = life;
    p->size = size;
    p->color = color;
    p->type = type;
}

static void add_thrust_particles(float dirx, float diry, int count)
{
    Lander *l = &G.lander;
    float nozzleX = l->x + l->w * 0.5f - sinf(l->angle) * l->h * 0.52f;
    float nozzleY = l->y + l->h * 0.5f + cosf(l->angle) * l->h * 0.52f;
    for (int i = 0; i < count; i++) {
        float spread = (frandf() - 0.5f) * 70.0f * G.scale;
        add_particle(nozzleX, nozzleY,
                     dirx * (80 + frandf() * 110) * G.scale + spread,
                     diry * (90 + frandf() * 130) * G.scale,
                     0.28f + frandf() * 0.35f,
                     1.6f + frandf() * 2.6f,
                     frandf() > 0.5f ? 0xffd166 : 0xff7a18, PT_THRUST);
    }
}

static void trigger_crash(void)
{
    Lander *l = &G.lander;
    l->crashed = true;
    G.state = GS_CRASHING;
    G.crashTimer = 0;
    G.screenFlash = 1;
    G.cameraShake = 16 * G.scale;
    sound_loop(SND_THRUST_MAIN, false, 0, 1);
    sound_loop(SND_THRUST_SIDE, false, 0, 1);
    sound_play(SND_CRASH, 0.95f, 1.0f);
    float cx = l->x + l->w * 0.5f;
    float cy = l->y + l->h * 0.5f;
    for (int i = 0; i < 90; i++) {
        float a = frandf() * 6.28318f;
        float sp = (70 + frandf() * 260) * G.scale;
        uint32_t col = i % 4 == 0 ? 0xfafafa : i % 3 == 0 ? 0xffd166 : 0xff4d00;
        add_particle(cx + (frandf() - 0.5f) * l->w,
                     cy + (frandf() - 0.5f) * l->h,
                     cosf(a) * sp,
                     sinf(a) * sp - 80 * G.scale,
                     0.35f + frandf() * 1.1f,
                     1.5f + frandf() * 4.5f, col,
                     i % 5 == 0 ? PT_DEBRIS : PT_FIRE);
    }
}

static void land_successfully(void)
{
    Lander *l = &G.lander;
    l->landed = true;
    l->vx = l->vy = l->angularVelocity = 0;
    l->y = G.pad.y - l->h;
    int bonus = game_landing_bonus();
    G.score += G.pad.points + bonus;
    G.state = GS_LEVEL_COMPLETE;
    G.levelTimer = 0;
    G.screenFlash = 0.35f;
    sound_loop(SND_THRUST_MAIN, false, 0, 1);
    sound_loop(SND_THRUST_SIDE, false, 0, 1);
    sound_play(SND_LANDING, 0.8f, 1.0f);
}

float terrain_height_at(float x)
{
    if (!G.terrain || G.terrainW <= 0) return (float)G.H;
    if (x < 0 || x >= G.W - 1) return (float)G.H;
    int ix = (int)x;
    float t = x - ix;
    return lerpf(G.terrain[ix], G.terrain[ix + 1], t);
}

float game_lander_speed(void)
{
    return sqrtf(G.lander.vx * G.lander.vx + G.lander.vy * G.lander.vy);
}

float game_lander_altitude(void)
{
    float cx = G.lander.x + G.lander.w * 0.5f;
    return terrain_height_at(cx) - (G.lander.y + G.lander.h);
}

bool game_lander_can_land(void)
{
    return game_lander_speed() <= G.maxSafeSpeed &&
           fabsf(G.lander.angle) <= G.maxLandingAngle;
}

int game_landing_bonus(void)
{
    float speed = game_lander_speed();
    if (speed <= G.maxSafeSpeed * 0.25f) return 100;
    if (speed <= G.maxSafeSpeed * 0.50f) return 50;
    if (speed <= G.maxSafeSpeed * 0.75f) return 25;
    return 0;
}

static bool is_over_pad(void)
{
    float grace = G.padGrace;
    float left = G.lander.x + G.lander.w * 0.16f;
    float right = G.lander.x + G.lander.w * 0.84f;
    return left >= G.pad.x - grace && right <= G.pad.x + G.pad.width + grace;
}

static void update_particles(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &G.particles[i];
        if (!p->active) continue;
        p->life -= TICK_DT;
        if (p->life <= 0) {
            p->active = false;
            continue;
        }
        p->x += p->vx * TICK_DT;
        p->y += p->vy * TICK_DT;
        if (p->type == PT_THRUST || p->type == PT_SMOKE) {
            p->vy -= 18.0f * G.scale * TICK_DT;
            p->vx *= 0.985f;
            p->size *= 1.006f;
        } else {
            p->vy += G.gravity * 0.45f * TICK_DT;
        }
    }
}

static void tick_holds(void)
{
    if (lowFuelCooldown > 0) lowFuelCooldown -= TICK_DT;
    if (altitudeCooldown > 0) altitudeCooldown -= TICK_DT;
    if (G.holdUp > 0) G.holdUp -= TICK_DT;
    if (G.holdLeft > 0) G.holdLeft -= TICK_DT;
    if (G.holdRight > 0) G.holdRight -= TICK_DT;
    if (G.holdUp < 0) G.holdUp = 0;
    if (G.holdLeft < 0) G.holdLeft = 0;
    if (G.holdRight < 0) G.holdRight = 0;
}

static void update_playing(void)
{
    Lander *l = &G.lander;
    bool mainActive;
    bool leftActive;
    bool rightActive;

    l->mainThrust = l->leftThrust = l->rightThrust = false;
    tick_holds();

    if (G.heldControls) {
        mainActive = G.heldUp;
        leftActive = G.heldLeft && !G.heldRight;
        rightActive = G.heldRight && !G.heldLeft;
    } else {
        mainActive = G.holdUp > 0;
        leftActive = G.holdLeft > 0 && G.holdRight <= 0;
        rightActive = G.holdRight > 0 && G.holdLeft <= 0;
    }

    if (l->fuel > 0) {
        if (mainActive) {
            l->vx += G.thrustPower * sinf(l->angle) * TICK_DT;
            l->vy -= G.thrustPower * cosf(l->angle) * TICK_DT;
            l->fuel = fmaxf(0, l->fuel - G.fuelMainRate * TICK_DT);
            l->mainThrust = true;
            add_thrust_particles(-sinf(l->angle), cosf(l->angle), 2);
        }
        if (leftActive) {
            l->angularVelocity -= G.angularThrustPower * TICK_DT;
            l->vx -= G.sideThrustPower * TICK_DT;
            l->fuel = fmaxf(0, l->fuel - G.fuelSideRate * TICK_DT);
            l->leftThrust = true;
            add_particle(l->x + l->w, l->y + l->h * 0.5f, 160 * G.scale,
                         (frandf() - 0.5f) * 60 * G.scale, 0.18f, 2.0f,
                         0xffb000, PT_THRUST);
        }
        if (rightActive) {
            l->angularVelocity += G.angularThrustPower * TICK_DT;
            l->vx += G.sideThrustPower * TICK_DT;
            l->fuel = fmaxf(0, l->fuel - G.fuelSideRate * TICK_DT);
            l->rightThrust = true;
            add_particle(l->x, l->y + l->h * 0.5f, -160 * G.scale,
                         (frandf() - 0.5f) * 60 * G.scale, 0.18f, 2.0f,
                         0xffb000, PT_THRUST);
        }
    }
    sound_loop(SND_THRUST_MAIN, l->mainThrust, 0.58f, 0.92f + fabsf(l->vy) / 900.0f);
    sound_loop(SND_THRUST_SIDE, l->leftThrust || l->rightThrust, 0.34f,
               0.95f + fabsf(l->angularVelocity) * 0.08f);

    bool sideActive = l->leftThrust || l->rightThrust;
    if (!sideActive) {
        l->angularVelocity -= l->angle * G.stabilizeStrength * TICK_DT;
        l->angularVelocity *= G.angularDamping;
    }

    l->vy += G.gravity * TICK_DT;
    l->x += l->vx * TICK_DT;
    l->y += l->vy * TICK_DT;
    l->angle += l->angularVelocity * TICK_DT;
    l->angularVelocity = clampf(l->angularVelocity, -G.maxSpin, G.maxSpin);
    if (l->angle > G.maxAngle) { l->angle = G.maxAngle; l->angularVelocity = 0; }
    if (l->angle < -G.maxAngle) { l->angle = -G.maxAngle; l->angularVelocity = 0; }
    l->vx *= G.dragCoefficient;
    l->vy *= G.dragCoefficient;

    if (l->x < -l->w || l->x > G.W || l->y > G.H + l->h) {
        trigger_crash();
        return;
    }

    float bottom = l->y + l->h;
    float centreX = l->x + l->w * 0.5f;
    float leftX = l->x + l->w * 0.18f;
    float rightX = l->x + l->w * 0.82f;
    float groundC = terrain_height_at(centreX);
    float groundL = terrain_height_at(leftX);
    float groundR = terrain_height_at(rightX);
    float ground = fminf(groundC, fminf(groundL, groundR));
    float padWindow = fmaxf(5.0f * G.scale, fabsf(l->vy) * TICK_DT + 3.0f * G.scale);
    if (bottom >= ground || (is_over_pad() && bottom >= G.pad.y - padWindow)) {
        if (is_over_pad() && fabsf(bottom - G.pad.y) <= padWindow + 3 * G.scale &&
            game_lander_can_land()) {
            land_successfully();
        } else {
            trigger_crash();
        }
    }

    if (G.state == GS_PLAYING) {
        if (l->fuel > 0 && l->fuel < 25.0f && lowFuelCooldown <= 0) {
            sound_play(SND_WARNING, 0.55f, 1.0f);
            lowFuelCooldown = 1.0f;
        }
        float alt = game_lander_altitude();
        float threshold = 160.0f * G.scale;
        if (alt > 0 && alt < threshold && l->vy > 0 && altitudeCooldown <= 0) {
            float interval = fmaxf(0.10f, (alt / threshold) * 0.50f);
            sound_play(SND_BEEP, 0.28f, 1.3f - clampf(alt / threshold, 0, 1) * 0.4f);
            altitudeCooldown = interval;
        }
    }
}

void game_tick(void)
{
    G.frameCount++;

    switch (G.state) {
    case GS_PLAYING:
        update_playing();
        break;
    case GS_CRASHING:
        G.crashTimer += TICK_DT;
        if (G.crashTimer > 1.25f) {
            G.lives--;
            if (G.lives <= 0) {
                G.state = GS_GAMEOVER;
            } else {
                game_create_level();
                G.state = GS_PLAYING;
            }
        }
        break;
    case GS_LEVEL_COMPLETE:
        G.levelTimer += TICK_DT;
        if (G.levelTimer > 2.0f) {
            G.level++;
            game_create_level();
            G.state = GS_PLAYING;
        }
        break;
    default:
        sound_loop(SND_THRUST_MAIN, false, 0, 1);
        sound_loop(SND_THRUST_SIDE, false, 0, 1);
        break;
    }

    update_particles();
    G.cameraShake = G.cameraShake > 0.2f ? G.cameraShake * 0.90f : 0;
    G.screenFlash = G.screenFlash > 0.01f ? G.screenFlash * 0.90f : 0;
}

static void thrust_hold_for_key(int key)
{
    const float hold = 0.075f;
    if (key == KEY_UP || key == 'w' || key == 'W') G.holdUp = hold;
    if (key == KEY_LEFT || key == 'a' || key == 'A') G.holdLeft = hold;
    if (key == KEY_RIGHT || key == 'd' || key == 'D') G.holdRight = hold;
}

void game_set_held_controls(bool available, bool up, bool left, bool right)
{
    G.heldControls = available;
    G.heldUp = available && up;
    G.heldLeft = available && left;
    G.heldRight = available && right;
    if (available) G.holdUp = G.holdLeft = G.holdRight = 0;
}

void game_handle_key(int key)
{
    if (key == 'q' || key == 'Q') {
        G.quit = true;
        return;
    }

    switch (G.state) {
    case GS_TITLE:
        if (key == KEY_ENTER || key == ' ') game_start_run();
        else if (key == KEY_LEFT || key == '[' || key == '-') {
            G.difficulty = (G.difficulty + DIFF_COUNT - 1) % DIFF_COUNT;
            configure_physics();
            game_create_level();
            sound_play(SND_MENU, 0.4f, 0.85f);
        } else if (key == KEY_RIGHT || key == ']' || key == '+' || key == '=') {
            G.difficulty = (G.difficulty + 1) % DIFF_COUNT;
            configure_physics();
            game_create_level();
            sound_play(SND_MENU, 0.4f, 1.15f);
        } else if (key >= '1' && key <= '4') {
            G.difficulty = key - '1';
            configure_physics();
            game_create_level();
            sound_play(SND_MENU, 0.4f, 1.0f);
        }
        else if (key == 'c' || key == 'C') {
            G.state = GS_CONTROLS;
            sound_play(SND_MENU, 0.4f, 1.25f);
        }
        break;
    case GS_CONTROLS:
        if (key == KEY_ESC || key == KEY_ENTER || key == ' ') {
            G.state = GS_TITLE;
            sound_play(SND_MENU, 0.4f, 0.8f);
        }
        break;
    case GS_PLAYING:
        if (key == KEY_ESC) {
            game_reset_to_title();
            sound_play(SND_MENU, 0.4f, 0.75f);
            return;
        }
        thrust_hold_for_key(key);
        break;
    case GS_CRASHING:
        break;
    case GS_LEVEL_COMPLETE:
        if (key == KEY_ENTER || key == ' ') {
            G.level++;
            game_create_level();
            G.state = GS_PLAYING;
            sound_play(SND_MENU, 0.4f, 1.15f);
        }
        break;
    case GS_GAMEOVER:
        if (key == KEY_ENTER || key == ' ') game_start_run();
        break;
    default:
        break;
    }
}

void game_autopilot_tick(void)
{
    if (G.state == GS_TITLE) {
        game_start_run();
        return;
    }
    if (G.state == GS_LEVEL_COMPLETE) {
        G.level++;
        game_create_level();
        G.state = GS_PLAYING;
        return;
    }
    if (G.state == GS_GAMEOVER) return;
    if (G.state != GS_PLAYING) return;

    Lander *l = &G.lander;
    float targetX = G.pad.x + G.pad.width * 0.5f;
    float landerX = l->x + l->w * 0.5f;
    float dx = targetX - landerX;
    float alt = game_lander_altitude();
    float maxTilt = alt < 120 * G.scale ? 0.18f : 0.34f;
    float targetAngle = clampf(dx / (G.W * 0.58f), -maxTilt, maxTilt);
    float desiredAV = clampf((targetAngle - l->angle) * 3.2f, -1.15f, 1.15f);
    if (l->angularVelocity > desiredAV + 0.06f) G.holdLeft = 0.10f;
    if (l->angularVelocity < desiredAV - 0.06f) G.holdRight = 0.10f;

    float desiredVy = alt > 220 * G.scale ? 115 * G.scale
                    : alt > 80 * G.scale ? 70 * G.scale : 35 * G.scale;
    if ((fabsf(l->angle) < 0.55f && l->vy > desiredVy) ||
        (alt < 95 * G.scale && game_lander_speed() > G.maxSafeSpeed * 0.70f &&
         fabsf(l->angle) < 0.75f))
        G.holdUp = 0.10f;
}
