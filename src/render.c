/* Software renderer: draws the game into an RGBA framebuffer for term.c. */
#include "terminal_lander.h"
#include "font8x16.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *fb = NULL;
static uint8_t *sky = NULL;
static int W = 0, H = 0;
static int OX = 0, OY = 0;

uint8_t *render_fb(void) { return fb; }

void render_init(int w, int h)
{
    W = w;
    H = h;
    fb = malloc((size_t)W * H * 4);
    sky = malloc((size_t)W * H * 4);

    for (int y = 0; y < H; y++) {
        float t = (float)y / H;
        float band = floorf(t * 18.0f) / 18.0f;
        float u = t * 0.65f + band * 0.35f;
        uint8_t r = (uint8_t)(4 + 10 * u);
        uint8_t g = (uint8_t)(5 + 10 * u);
        uint8_t b = (uint8_t)(17 + 34 * u);
        for (int x = 0; x < W; x++) {
            uint8_t *p = sky + ((size_t)y * W + x) * 4;
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}

void render_shutdown(void)
{
    free(fb);
    free(sky);
    fb = sky = NULL;
}

static inline void set_px(int x, int y, uint32_t rgb)
{
    x += OX;
    y += OY;
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    uint8_t *p = fb + ((size_t)y * W + x) * 4;
    p[0] = (rgb >> 16) & 255;
    p[1] = (rgb >> 8) & 255;
    p[2] = rgb & 255;
}

static void pixel_block(float fx, float fy, float size, uint32_t rgb)
{
    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    int s = (int)ceilf(size);
    if (s < 1) s = 1;
    for (int y = 0; y < s; y++)
        for (int x = 0; x < s; x++)
            set_px(x0 + x, y0 + y, rgb);
}

static void pixel_rect(float fx, float fy, float fw, float fh, uint32_t rgb)
{
    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    int x1 = (int)ceilf(fx + fw);
    int y1 = (int)ceilf(fy + fh);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            set_px(x, y, rgb);
}

static inline void px_blend(int x, int y, uint32_t rgb, float a)
{
    x += OX;
    y += OY;
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    int ai = (int)(a * 256.0f + 0.5f);
    if (ai <= 0) return;
    if (ai > 256) ai = 256;
    uint8_t *p = fb + ((size_t)y * W + x) * 4;
    int r = (rgb >> 16) & 255;
    int g = (rgb >> 8) & 255;
    int b = rgb & 255;
    p[0] = (uint8_t)(p[0] + (((r - p[0]) * ai) >> 8));
    p[1] = (uint8_t)(p[1] + (((g - p[1]) * ai) >> 8));
    p[2] = (uint8_t)(p[2] + (((b - p[2]) * ai) >> 8));
}

static void fill_rect(float fx, float fy, float fw, float fh, uint32_t rgb, float a)
{
    if (fw <= 0 || fh <= 0) return;
    int x0 = (int)floorf(fx), x1 = (int)ceilf(fx + fw);
    int y0 = (int)floorf(fy), y1 = (int)ceilf(fy + fh);
    for (int y = y0; y < y1; y++) {
        float cy = fminf((float)(y + 1), fy + fh) - fmaxf((float)y, fy);
        if (cy <= 0) continue;
        if (cy > 1) cy = 1;
        for (int x = x0; x < x1; x++) {
            float cx = fminf((float)(x + 1), fx + fw) - fmaxf((float)x, fx);
            if (cx <= 0) continue;
            if (cx > 1) cx = 1;
            px_blend(x, y, rgb, a * cx * cy);
        }
    }
}

static void fill_circle(float cx, float cy, float r, uint32_t rgb, float a)
{
    if (r <= 0) return;
    float rOut = r + 0.5f, rIn = r - 0.5f;
    float rOut2 = rOut * rOut, rIn2 = rIn > 0 ? rIn * rIn : 0;
    int y0 = (int)floorf(cy - rOut), y1 = (int)ceilf(cy + rOut);
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - cy;
        float w2 = rOut2 - dy * dy;
        if (w2 <= 0) continue;
        float half = sqrtf(w2);
        int x0 = (int)floorf(cx - half), x1 = (int)ceilf(cx + half);
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx;
            float d2 = dx * dx + dy * dy;
            if (d2 >= rOut2) continue;
            if (d2 <= rIn2) {
                px_blend(x, y, rgb, a);
            } else {
                float cov = rOut - sqrtf(d2);
                px_blend(x, y, rgb, a * (cov > 1 ? 1 : cov));
            }
        }
    }
}

static void draw_line(float x0, float y0, float x1, float y1, float width,
                      uint32_t rgb, float a, int dashOn, int dashOff)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float hw = width / 2;
    if (hw < 0.5f) hw = 0.5f;
    if (len2 < 0.25f) {
        fill_circle(x0, y0, hw, rgb, a);
        return;
    }
    float len = sqrtf(len2);
    int xMin = (int)floorf(fminf(x0, x1) - hw) - 1;
    int xMax = (int)ceilf(fmaxf(x0, x1) + hw) + 1;
    int yMin = (int)floorf(fminf(y0, y1) - hw) - 1;
    int yMax = (int)ceilf(fmaxf(y0, y1) + hw) + 1;
    int period = dashOn + dashOff;
    for (int y = yMin; y < yMax; y++) {
        for (int x = xMin; x < xMax; x++) {
            float px = x + 0.5f - x0;
            float py = y + 0.5f - y0;
            float t = (px * dx + py * dy) / len2;
            float tc = clampf(t, 0, 1);
            float qx = px - tc * dx;
            float qy = py - tc * dy;
            float cov = hw + 0.5f - sqrtf(qx * qx + qy * qy);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            if (period > 0 && fmodf(tc * len, (float)period) >= dashOn) continue;
            px_blend(x, y, rgb, a * cov);
        }
    }
}

static void ring(float cx, float cy, float r, float width, uint32_t rgb, float a)
{
    float hw = width / 2;
    int x0 = (int)floorf(cx - r - hw) - 1;
    int x1 = (int)ceilf(cx + r + hw) + 1;
    int y0 = (int)floorf(cy - r - hw) - 1;
    int y1 = (int)ceilf(cy + r + hw) + 1;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float dx = x + 0.5f - cx;
            float dy = y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = hw + 0.5f - fabsf(d - r);
            if (cov <= 0) continue;
            px_blend(x, y, rgb, a * (cov > 1 ? 1 : cov));
        }
    }
}

static int text_width(const char *s, int scale)
{
    return (int)strlen(s) * FONT_W * scale;
}

static void draw_glyph(int x, int y, const unsigned char *glyph,
                       uint32_t rgb, float a, int scale)
{
    for (int gy = 0; gy < FONT_H; gy++) {
        uint8_t row = glyph[gy];
        for (int gx = 0; gx < FONT_W; gx++) {
            if (!((row >> (7 - gx)) & 1)) continue;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++)
                    px_blend(x + gx * scale + sx, y + gy * scale + sy, rgb, a);
        }
    }
}

static void draw_text(float fx, float fy, const char *s, uint32_t rgb, float a, int scale)
{
    int x = (int)fx, y = (int)fy;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        draw_glyph(x, y, font8x16[c - 32], rgb, a, scale);
        x += FONT_W * scale;
    }
}

static void draw_text_outlined(float x, float y, const char *s, uint32_t rgb,
                               float a, int scale)
{
    draw_text(x - 1, y, s, 0x000000, a, scale);
    draw_text(x + 1, y, s, 0x000000, a, scale);
    draw_text(x, y - 1, s, 0x000000, a, scale);
    draw_text(x, y + 1, s, 0x000000, a, scale);
    draw_text(x, y, s, rgb, a, scale);
}

static void draw_text_center(float cx, float y, const char *s,
                             uint32_t rgb, float a, int scale)
{
    draw_text(cx - text_width(s, scale) / 2.0f, y, s, rgb, a, scale);
}

static void draw_stars(void)
{
    float t = G.frameCount / 60.0f;
    for (int i = 0; i < G.numStars; i++) {
        Star *s = &G.stars[i];
        int b = (int)(s->brightness + sinf(t * s->speed + s->phase) * 38.0f);
        if (b < 35) b = 35;
        if (b > 255) b = 255;
        uint32_t col = ((uint32_t)b << 16) | ((uint32_t)b << 8) | (uint32_t)clampf(b + 12, 0, 255);
        set_px((int)s->x, (int)s->y, col);
        if (s->size > 1 && b > 150) {
            int d = (int)(b * 0.35f);
            uint32_t dim = ((uint32_t)d << 16) | ((uint32_t)d << 8) | (uint32_t)d;
            set_px((int)s->x + 1, (int)s->y, dim);
            set_px((int)s->x, (int)s->y + 1, dim);
        }
    }
}

static void draw_earth(void)
{
    int block = (int)clampf(H / 280.0f, 2.0f, 4.0f);
    int r = (int)clampf(H * 0.040f, 13.0f, 34.0f);
    bool hud = G.state == GS_PLAYING || G.state == GS_CRASHING ||
               G.state == GS_LEVEL_COMPLETE || G.state == GS_GAMEOVER;
    int cx = W - r * 3;
    int cy = hud ? (int)(H * 0.24f) : r * 2;
    for (int y = cy - r - block; y <= cy + r + block; y += block) {
        for (int x = cx - r - block; x <= cx + r + block; x += block) {
            float dx = x + block * 0.5f - cx;
            float dy = y + block * 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > r + block * 0.4f) continue;
            uint32_t col = 0x1d4ed8;
            if (d > r * 0.88f) col = 0x60a5fa;
            if (fabsf(dy) > r * 0.68f && d < r * 0.92f) col = 0xe2e8f0;
            int land = ((int)(dx * 0.55f + dy * 0.90f + 300) % 13);
            if ((land >= 0 && land < 5 && d > r * 0.18f && fabsf(dy) < r * 0.72f) ||
                ((int)(dx - dy + 200) % 17) < 4)
                col = 0x22c55e;
            if (dx > r * 0.35f && d < r * 0.92f) col = (col == 0xe2e8f0) ? 0xcbd5e1 : 0x1e3a8a;
            pixel_rect(x, y, block, block, col);
        }
    }
    pixel_rect(cx - r - block, cy - block, block, block, 0x93c5fd);
    pixel_rect(cx + r, cy, block, block, 0x0f172a);
}

static void draw_terrain(void)
{
    if (!G.terrain) return;
    for (int x = 0; x < W; x++) {
        int sx = x + OX;
        if (sx < 0 || sx >= W) continue;
        int top = (int)G.terrain[x];
        for (int y = top; y < H; y++) {
            int sy = y + OY;
            if (sy < 0 || sy >= H) continue;
            float depth = (float)(y - top) / (H - top + 1);
            uint32_t col = depth < 0.10f ? 0xbdb6a5
                         : depth < 0.28f ? 0x928b7c
                         : depth < 0.58f ? 0x6f695f : 0x4c4740;
            if (((x * 3 + y * 5) & 31) == 0 && depth > 0.08f)
                col = depth < 0.35f ? 0xa69e8d : 0x5f594f;
            set_px(x, y, col);
        }
        set_px(x, top - 2, 0xf1f5d0);
        set_px(x, top - 1, 0xd7d1b8);
        set_px(x, top, 0x8b8679);
    }

    LandingPad *p = &G.pad;
    pixel_rect(p->x, p->y - 2 * G.scale, p->width, 5 * G.scale,
               p->points >= 100 ? 0x22c55e : 0xfacc15);
    for (int x = p->x; x <= p->x + p->width; x += (int)fmaxf(8, 10 * G.scale)) {
        pixel_block(x, p->y - 7 * G.scale, 3.0f * G.scale,
                    ((G.frameCount / 18) & 1) ? 0xffff66 : 0x777711);
    }
    char buf[32];
    snprintf(buf, sizeof buf, "%d", p->points);
    draw_text_center(p->x + p->width / 2.0f, p->y + 8 * G.scale, buf, 0x111111, 0.8f, 1);
}

static bool render_lander_over_pad(void)
{
    float grace = G.padGrace;
    float left = G.lander.x + G.lander.w * 0.16f;
    float right = G.lander.x + G.lander.w * 0.84f;
    return left >= G.pad.x - grace && right <= G.pad.x + G.pad.width + grace;
}

static void draw_landing_guides(void)
{
    if (!(G.state == GS_PLAYING || G.state == GS_LEVEL_COMPLETE)) return;

    LandingPad *p = &G.pad;
    bool over = render_lander_over_pad();
    bool speedOk = game_lander_speed() <= G.maxSafeSpeed;
    bool angleOk = fabsf(G.lander.angle) <= G.maxLandingAngle;
    uint32_t guide = over && speedOk && angleOk ? 0x22c55e
                   : over ? 0xfacc15 : 0x38bdf8;
    float top = p->y - 145.0f * G.scale;
    if (top < 90.0f * G.scale) top = 90.0f * G.scale;

    for (int side = 0; side < 2; side++) {
        float x = side == 0 ? p->x : p->x + p->width;
        draw_line(x, top, x, p->y - 8 * G.scale, 1.2f * G.scale,
                  guide, 0.28f, 7, 8);
        for (float y = p->y - 18 * G.scale; y > top; y -= 24 * G.scale) {
            float pulse = ((G.frameCount / 12) & 1) ? 1.0f : 0.45f;
            fill_circle(x, y, 2.6f * G.scale, guide, 0.55f * pulse);
        }
    }

    float cx = p->x + p->width * 0.5f;
    draw_line(cx, top, cx, p->y - 5 * G.scale, 1.0f * G.scale,
              guide, over ? 0.20f : 0.11f, 3, 10);
    draw_line(p->x - 12 * G.scale, p->y - 20 * G.scale,
              p->x - 12 * G.scale, p->y - 4 * G.scale, 2.0f * G.scale,
              guide, 0.65f, 0, 0);
    draw_line(p->x + p->width + 12 * G.scale, p->y - 20 * G.scale,
              p->x + p->width + 12 * G.scale, p->y - 4 * G.scale, 2.0f * G.scale,
              guide, 0.65f, 0, 0);
}

static void draw_flight_vectors(void)
{
    if (G.state != GS_PLAYING) return;
    Lander *l = &G.lander;
    float cx = l->x + l->w * 0.5f;
    float cy = l->y + l->h * 0.5f;
    float ground = terrain_height_at(cx);
    float bottom = l->y + l->h;
    if (ground > bottom && ground < H) {
        uint32_t altCol = game_lander_altitude() < 75 * G.scale ? 0xfacc15 : 0x64748b;
        draw_line(cx, bottom, cx, ground, 1.0f * G.scale, altCol, 0.34f, 5, 7);
        draw_line(cx - l->w * 0.38f, ground, cx + l->w * 0.38f, ground,
                  2.2f * G.scale, altCol, 0.28f, 0, 0);
    }

    float speed = game_lander_speed();
    if (speed > 8 * G.scale) {
        float vx = clampf(l->vx * 0.18f, -90 * G.scale, 90 * G.scale);
        float vy = clampf(l->vy * 0.18f, -90 * G.scale, 90 * G.scale);
        uint32_t col = speed <= G.maxSafeSpeed ? 0x22c55e : 0xef4444;
        draw_line(cx, cy, cx + vx, cy + vy, 2.0f * G.scale, col, 0.75f, 0, 0);
        fill_circle(cx + vx, cy + vy, 3.0f * G.scale, col, 0.85f);
    }
}

static void lander_point(const Lander *l, float lx, float ly, float *ox, float *oy)
{
    float ca = cosf(l->angle);
    float sa = sinf(l->angle);
    float cx = l->x + l->w * 0.5f;
    float cy = l->y + l->h * 0.5f;
    *ox = cx + lx * ca - ly * sa;
    *oy = cy + lx * sa + ly * ca;
}

static void draw_lander_flames(const Lander *l)
{
    if (l->mainThrust) {
        float nx, ny;
        lander_point(l, 0, l->h * 0.52f, &nx, &ny);
        float dx = -sinf(l->angle);
        float dy = cosf(l->angle);
        float pix = fmaxf(2.0f, l->w * 0.11f);
        for (int i = 0; i < 5; i++) {
            float d = (i + 1) * l->h * 0.13f;
            uint32_t col = i == 0 ? 0xfff4c4 : i < 3 ? 0xffd166 : 0xff6b00;
            pixel_block(nx + dx * d - pix * 0.5f, ny + dy * d - pix * 0.5f,
                        pix * (i < 2 ? 1.15f : 0.85f), col);
        }
    }

    if (l->leftThrust || l->rightThrust) {
        float lx = l->leftThrust ? l->w * 0.56f : -l->w * 0.56f;
        float vx = l->leftThrust ? cosf(l->angle) : -cosf(l->angle);
        float vy = l->leftThrust ? sinf(l->angle) : -sinf(l->angle);
        float px, py;
        lander_point(l, lx, -l->h * 0.05f, &px, &py);
        for (int i = 0; i < 3; i++)
            pixel_block(px + vx * i * l->w * 0.13f,
                        py + vy * i * l->w * 0.13f,
                        fmaxf(1.5f, l->w * 0.09f), i == 0 ? 0xffe08a : 0xff8a00);
    }
}

static void lander_pixel(const Lander *l, float lx, float ly, float size, uint32_t col)
{
    float px, py;
    lander_point(l, lx, ly, &px, &py);
    pixel_block(px - size * 0.5f, py - size * 0.5f, size, col);
}

static void draw_lander(void)
{
    const Lander *l = &G.lander;
    if (G.state == GS_CRASHING || l->crashed) return;

    draw_lander_flames(l);

    uint32_t body = l->landed ? 0x80ff9a : 0xd9d9e8;
    if (G.state == GS_PLAYING) {
        bool over = render_lander_over_pad();
        bool safe = game_lander_can_land();
        if (over)
            ring(l->x + l->w * 0.5f, l->y + l->h * 0.50f,
                 l->w * 0.86f, 1.6f * G.scale,
                 safe ? 0x22c55e : 0xfacc15, safe ? 0.28f : 0.22f);
    }

    float px = fmaxf(2.0f, l->w / 6.8f);
    static const signed char bodyCells[][2] = {
        { 0,-2 }, { -1,-1 }, { 0,-1 }, { 1,-1 },
        { -1,0 }, { 0,0 }, { 1,0 },
        { -1,1 }, { 0,1 }, { 1,1 },
        { 0,2 }
    };
    for (size_t i = 0; i < sizeof bodyCells / sizeof bodyCells[0]; i++)
        lander_pixel(l, bodyCells[i][0] * px, bodyCells[i][1] * px, px, body);

    lander_pixel(l, 0, -3 * px, px, 0x7dd3fc);
    lander_pixel(l, -px, -3 * px, px, 0x2563eb);
    lander_pixel(l, px, -3 * px, px, 0x93c5fd);
    lander_pixel(l, 0, 3 * px, px, 0x64748b);

    static const signed char legCells[][2] = {
        { -2,2 }, { 2,2 }, { -3,3 }, { 3,3 }
    };
    for (size_t i = 0; i < sizeof legCells / sizeof legCells[0]; i++)
        lander_pixel(l, legCells[i][0] * px, legCells[i][1] * px, px * 0.82f, 0xcbd5e1);

    if (l->landed)
        ring(l->x + l->w * 0.5f, l->y + l->h * 0.52f,
             l->w * 0.82f, 2.0f * G.scale, 0x22c55e, 0.55f);
}

static void draw_particles(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &G.particles[i];
        if (!p->active) continue;
        float a = p->maxLife > 0 ? clampf(p->life / p->maxLife, 0, 1) : 1;
        if (p->type == PT_SMOKE) a *= 0.35f;
        if (a < 0.18f) continue;
        pixel_block(p->x, p->y, fmaxf(2.0f, p->size), p->color);
    }
}

static void draw_hud(void)
{
    char buf[128];
    Lander *l = &G.lander;

    fill_rect(8, 8, 300, 86, 0x101015, 0.86f);
    snprintf(buf, sizeof buf, "SCORE %d", G.score);
    draw_text(18, 16, buf, 0xf8fafc, 1, 1);
    snprintf(buf, sizeof buf, "LEVEL %d   LIVES %d   %s", G.level, G.lives,
             DIFFICULTY_NAMES[G.difficulty]);
    draw_text(18, 36, buf, 0xa1a1aa, 1, 1);

    float fuelPct = l->maxFuel > 0 ? l->fuel / l->maxFuel : 0;
    fill_rect(18, 62, 150, 12, 0x3f3f46, 1);
    fill_rect(19, 63, 148 * clampf(fuelPct, 0, 1), 10,
              fuelPct > 0.25f ? 0xfacc15 : 0xef4444, 1);
    snprintf(buf, sizeof buf, "FUEL %3d", (int)l->fuel);
    draw_text(180, 60, buf, fuelPct > 0.25f ? 0xfacc15 : 0xef4444, 1, 1);

    fill_rect(W - 300, 8, 292, 128, 0x101015, 0.86f);
    float speed = game_lander_speed();
    uint32_t speedCol = speed <= G.maxSafeSpeed ? 0x22c55e : 0xef4444;
    snprintf(buf, sizeof buf, "SPEED %3d / SAFE %3d", (int)speed, (int)G.maxSafeSpeed);
    draw_text(W - 286, 18, buf, speedCol, 1, 1);
    float speedPct = clampf(speed / (G.maxSafeSpeed * 2.4f), 0, 1);
    fill_rect(W - 286, 40, 180, 12, 0x3f3f46, 1);
    fill_rect(W - 285, 41, 178 * speedPct, 10, speedCol, 1);
    float safeX = W - 286 + 180 * (G.maxSafeSpeed / (G.maxSafeSpeed * 2.4f));
    fill_rect(safeX, 38, 2, 16, 0xffffff, 1);

    float alt = fmaxf(0, game_lander_altitude());
    uint32_t altCol = alt > 110 * G.scale ? 0x22c55e : alt > 45 * G.scale ? 0xfacc15 : 0xef4444;
    snprintf(buf, sizeof buf, "ALT %4d   VY %4d", (int)alt, (int)l->vy);
    draw_text(W - 286, 64, buf, altCol, 1, 1);

    int deg = (int)(l->angle * 180.0f / 3.14159265f);
    uint32_t angCol = fabsf(l->angle) <= G.maxLandingAngle ? 0x22c55e
                     : fabsf(l->angle) <= G.maxLandingAngle * 3 ? 0xfacc15 : 0xef4444;
    snprintf(buf, sizeof buf, "ANGLE %4d DEG", deg);
    draw_text(W - 286, 86, buf, angCol, 1, 1);

    uint32_t landCol = render_lander_over_pad() && game_lander_can_land() ? 0x22c55e
                     : render_lander_over_pad() ? 0xfacc15 : 0x71717a;
    const char *status = render_lander_over_pad()
        ? (game_lander_can_land() ? "LANDING OK" : "TOO HOT / TILTED")
        : "SEEK PAD";
    draw_text(W - 286, 110, status, landCol, 1, 1);

    fill_rect(0, H - 40, W, 40, 0x08080c, 0.82f);
    draw_text(16, H - 28, "UP/W thrust   LEFT/A RIGHT/D rotate   ESC title   Q quit",
              0x71717a, 1, 1);
}

static void panel(int pw, int ph, int *px, int *py)
{
    *px = (W - pw) / 2;
    *py = (H - ph) / 2;
    fill_rect(*px - 2, *py - 2, pw + 4, ph + 4, 0x3b82f6, 0.38f);
    fill_rect(*px, *py, pw, ph, 0x121218, 0.96f);
}

static void draw_title(void)
{
    int px, py;
    panel(700, 348, &px, &py);
    draw_text_outlined(W / 2.0f - text_width("TERMINAL LANDER", 3) / 2.0f,
                       py + 34, "TERMINAL LANDER", 0x7dd3fc, 1, 3);
    draw_text_center(W / 2.0f, py + 94, "lunar landing in a kitty terminal", 0xa1a1aa, 1, 1);
    char buf[128];
    snprintf(buf, sizeof buf, "DIFFICULTY      < %s >", DIFFICULTY_NAMES[G.difficulty]);
    draw_text(px + 112, py + 132, buf, 0xfacc15, 1, 1);
    draw_text(px + 112, py + 166, "ENTER / SPACE   START", 0xf8fafc, 1, 1);
    draw_text(px + 112, py + 198, "LEFT / RIGHT    CHANGE DIFFICULTY", 0xf8fafc, 1, 1);
    draw_text(px + 112, py + 230, "1-4             EASY..EXTRA HARD", 0xf8fafc, 1, 1);
    draw_text(px + 112, py + 262, "C               CONTROLS", 0xf8fafc, 1, 1);
    draw_text(px + 112, py + 294, "Q               QUIT", 0xf8fafc, 1, 1);
    draw_text_center(W / 2.0f, py + 326,
                     "Extra Hard is the original tuning; lower presets add control assist",
                     0x52525b, 1, 1);
}

static void draw_controls(void)
{
    int px, py;
    panel(760, 430, &px, &py);
    draw_text_center(W / 2.0f, py + 24, "CONTROLS", 0x7dd3fc, 1, 2);
    draw_text(px + 70, py + 78,  "UP / W       main thrust", 0xf8fafc, 1, 1);
    draw_text(px + 70, py + 108, "LEFT / A     rotate left and push left", 0xf8fafc, 1, 1);
    draw_text(px + 70, py + 138, "RIGHT / D    rotate right and push right", 0xf8fafc, 1, 1);
    draw_text(px + 70, py + 168, "TITLE: LEFT/RIGHT or 1-4 change difficulty", 0xf8fafc, 1, 1);
    draw_text(px + 70, py + 218, "Land fully on the lit pad.", 0xa1a1aa, 1, 1);
    draw_text(px + 70, py + 248, "Speed and angle must both be green.", 0xa1a1aa, 1, 1);
    draw_text(px + 70, py + 278, "Easy and Medium add stronger auto-stabilization.", 0xa1a1aa, 1, 1);
    draw_text(px + 70, py + 308, "Hard is stricter; Extra Hard is the original game.", 0xa1a1aa, 1, 1);
    draw_text_center(W / 2.0f, py + 378, "ENTER / ESC  BACK", 0x71717a, 1, 1);
}

static void draw_level_complete(void)
{
    int px, py;
    panel(440, 210, &px, &py);
    draw_text_center(W / 2.0f, py + 28, "LANDED", 0x22c55e, 1, 3);
    char buf[96];
    int bonus = game_landing_bonus();
    snprintf(buf, sizeof buf, "+%d PAD   +%d BONUS", G.pad.points, bonus);
    draw_text_center(W / 2.0f, py + 94, buf, 0xfacc15, 1, 1);
    snprintf(buf, sizeof buf, "SCORE %d", G.score);
    draw_text_center(W / 2.0f, py + 124, buf, 0xf8fafc, 1, 1);
    draw_text_center(W / 2.0f, py + 166, "ENTER next level", 0x71717a, 1, 1);
}

static void draw_gameover(void)
{
    int px, py;
    panel(500, 240, &px, &py);
    draw_text_center(W / 2.0f, py + 32, "GAME OVER", 0xef4444, 1, 3);
    char buf[96];
    snprintf(buf, sizeof buf, "FINAL SCORE %d", G.score);
    draw_text_center(W / 2.0f, py + 104, buf, 0xf8fafc, 1, 1);
    snprintf(buf, sizeof buf, "REACHED LEVEL %d", G.level);
    draw_text_center(W / 2.0f, py + 134, buf, 0xa1a1aa, 1, 1);
    draw_text_center(W / 2.0f, py + 186, "ENTER restart   Q quit", 0x71717a, 1, 1);
}

static void draw_flash(void)
{
    if (G.screenFlash <= 0) return;
    int ai = (int)(G.screenFlash * 0.34f * 256);
    if (ai <= 0) return;
    if (ai > 256) ai = 256;
    uint8_t *p = fb;
    for (size_t i = 0, n = (size_t)W * H; i < n; i++, p += 4) {
        p[0] = (uint8_t)(p[0] + (((255 - p[0]) * ai) >> 8));
        p[1] = (uint8_t)(p[1] + (((220 - p[1]) * ai) >> 8));
        p[2] = (uint8_t)(p[2] + (((160 - p[2]) * ai) >> 8));
    }
}

void render_frame(void)
{
    if (!fb || !sky) return;
    memcpy(fb, sky, (size_t)W * H * 4);

    float shake = G.cameraShake;
    OX = (int)(sinf(G.frameCount * 12.989f) * shake);
    OY = (int)(cosf(G.frameCount * 9.173f) * shake);

    draw_stars();
    draw_earth();
    draw_terrain();
    draw_landing_guides();
    draw_particles();
    draw_flight_vectors();
    draw_lander();
    OX = OY = 0;

    if (G.state == GS_PLAYING || G.state == GS_CRASHING ||
        G.state == GS_LEVEL_COMPLETE || G.state == GS_GAMEOVER) {
        draw_hud();
    }

    switch (G.state) {
    case GS_TITLE: draw_title(); break;
    case GS_CONTROLS: draw_controls(); break;
    case GS_LEVEL_COMPLETE: draw_level_complete(); break;
    case GS_GAMEOVER: draw_gameover(); break;
    default: break;
    }

    draw_flash();
}
