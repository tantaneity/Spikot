#include "render/render.h"
#include "config.h"
#include "snn/network.h"
#include <math.h>
#include <stdio.h>

#define MOVE_LERP 0.22f
#define CAT_DRAW_SCALE 2.6f
#define BOB_SPEED 5.0f
#define BOB_AMP 1.8f
#define MEOW_THRESHOLD 0.11f
#define WALK_TILT 8.0f
#define WALK_FREQ 14.0f
#define MOOD_HOLD_HAPPY 1.0f
#define MOOD_HOLD_SCARED 0.6f
#define MOOD_HOLD_PET 1.6f
#define MAX_PARTICLES 96
#define MOTE_COUNT 26
#define DAY_LENGTH 90.0

#define BRAIN_VIS_COLS 32
#define BRAIN_VIS_CELL 13
#define BRAIN_VIS_Y 470
#define BRAIN_INPUT_END SNN_INPUT_NEURONS
#define BRAIN_OUTPUT_BEGIN (SNN_NEURON_COUNT - SNN_OUTPUT_NEURONS)

#define ROOM_W (WORLD_WIDTH * WORLD_TILE_PX)
#define ROOM_H (WORLD_HEIGHT * WORLD_TILE_PX)
#define PANEL_X (GRID_ORIGIN_X + ROOM_W + 36)
#define PALETTE_Y 360
#define PALETTE_SLOT 60
#define PALETTE_GAP 8

static const Color BG_TOP = (Color){ 38, 34, 44, 255 };
static const Color BG_BOTTOM = (Color){ 24, 22, 30, 255 };
static const Color PLANK_A = (Color){ 120, 90, 64, 255 };
static const Color PLANK_B = (Color){ 130, 98, 70, 255 };
static const Color PLANK_SEAM = (Color){ 96, 70, 48, 255 };
static const Color WALL_BODY = (Color){ 168, 158, 146, 255 };
static const Color WALL_BASE = (Color){ 134, 124, 112, 255 };
static const Color INK = (Color){ 32, 26, 28, 255 };
static const Color PANEL_GLASS = (Color){ 30, 28, 40, 230 };
static const Color PANEL_LINE = (Color){ 64, 62, 86, 255 };
static const Color TEXT_DIM = (Color){ 162, 160, 180, 255 };
static const Color ACCENT = (Color){ 255, 196, 90, 255 };

typedef enum { P_HEART, P_DOT } PKind;
typedef struct {
    float x, y, vx, vy, grav, life, maxLife, size;
    Color color;
    PKind kind;
    bool active;
} Particle;
static Particle g_particles[MAX_PARTICLES];

static Particle *allocParticle(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (!g_particles[i].active) return &g_particles[i];
    return NULL;
}

static float randSpread(float range)
{
    return ((float)GetRandomValue(-1000, 1000) / 1000.0f) * range;
}

void ParticleHeart(float x, float y)
{
    Particle *p = allocParticle();
    if (!p) return;
    *p = (Particle){ x, y, randSpread(6.0f), -26.0f, 0.0f, 1.0f, 1.0f, 3.2f,
                     (Color){ 255, 120, 150, 255 }, P_HEART, true };
}

void ParticleCrumbs(float x, float y)
{
    for (int i = 0; i < 5; i++)
    {
        Particle *p = allocParticle();
        if (!p) return;
        *p = (Particle){ x, y - 4.0f, randSpread(34.0f), -randSpread(34.0f) - 20.0f, 150.0f,
                         0.55f, 0.55f, (float)GetRandomValue(15, 26) / 10.0f,
                         (Color){ 235, 150, 80, 255 }, P_DOT, true };
    }
}

void ParticleDust(float x, float y)
{
    for (int i = 0; i < 3; i++)
    {
        Particle *p = allocParticle();
        if (!p) return;
        *p = (Particle){ x + randSpread(5.0f), y + 6.0f, randSpread(10.0f), -randSpread(6.0f) - 4.0f, 0.0f,
                         0.4f, 0.4f, (float)GetRandomValue(15, 28) / 10.0f,
                         (Color){ 180, 160, 140, 160 }, P_DOT, true };
    }
}

void ParticlesUpdate(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        Particle *p = &g_particles[i];
        if (!p->active) continue;
        p->vy += p->grav * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->life -= dt;
        if (p->life <= 0.0f) p->active = false;
    }
}

static void drawParticles(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        Particle *p = &g_particles[i];
        if (!p->active) continue;
        float a = p->life / p->maxLife;
        Color c = p->color;
        c.a = (unsigned char)(c.a * a);
        if (p->kind == P_HEART)
        {
            DrawCircle((int)p->x - 3, (int)p->y, p->size, c);
            DrawCircle((int)p->x + 3, (int)p->y, p->size, c);
            DrawTriangle((Vector2){ p->x - 6, p->y + 1 }, (Vector2){ p->x, p->y + 8 }, (Vector2){ p->x + 6, p->y + 1 }, c);
        }
        else
        {
            DrawCircle((int)p->x, (int)p->y, p->size, c);
        }
    }
}

static void glow(float cx, float cy, float radius, Color tint, float strength)
{
    Color inner = tint;
    inner.a = (unsigned char)(strength * 255.0f);
    DrawCircleGradient((int)cx, (int)cy, radius, inner, BLANK);
}

static CatEmotion affectMood(const CatAgent *agent, const CatBody *body)
{
    const Neuromods *m = &agent->mods;
    if (m->arousal > 0.6f && m->valence < MOOD_VALENCE_LOW) return EMOTION_SCARED;
    if (m->valence > MOOD_VALENCE_HAPPY) return EMOTION_HAPPY;
    if (body->hunger > 0.6f) return EMOTION_HUNGRY;
    if (m->acetylcholine > 0.6f && m->arousal > 0.4f) return EMOTION_CURIOUS;
    return EMOTION_CONTENT;
}

void MoodUpdate(CatView *view, const CatAgent *agent, const World *world,
                const CatBody *body, int otherX, int otherY, float dt)
{
    (void)world; (void)otherX; (void)otherY;
    if (view->moodHold > 0.0f) view->moodHold -= dt;
    if (agent->lastReward >= REWARD_FOOD * 0.5f) { view->mood = EMOTION_HAPPY; view->moodHold = MOOD_HOLD_HAPPY; }
    else if (agent->lastReward <= REWARD_OBSTACLE * 0.5f) { view->mood = EMOTION_SCARED; view->moodHold = MOOD_HOLD_SCARED; }
    else if (view->moodHold <= 0.0f) view->mood = affectMood(agent, body);
}

void MoodPet(CatView *view)
{
    view->mood = EMOTION_HAPPY;
    view->moodHold = MOOD_HOLD_PET;
    view->pets++;
}

void ViewUpdate(CatView *view, const CatBody *body)
{
    if (body->x < view->x - 0.05f) view->faceLeft = true;
    else if (body->x > view->x + 0.05f) view->faceLeft = false;
    view->x += (body->x - view->x) * MOVE_LERP;
    view->y += (body->y - view->y) * MOVE_LERP;
}

static void drawBackground(double time)
{
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, BG_TOP, BG_BOTTOM);
    for (int i = 0; i < MOTE_COUNT; i++)
    {
        float seed = (float)i * 12.9898f;
        float baseX = fmodf(seed * 78.233f, (float)WINDOW_WIDTH);
        float baseY = fmodf(seed * 39.425f, (float)WINDOW_HEIGHT);
        float driftX = sinf((float)time * 0.3f + seed) * 16.0f;
        float driftY = cosf((float)time * 0.22f + seed * 1.7f) * 12.0f;
        float tw = 0.2f + 0.2f * sinf((float)time * 1.5f + seed * 3.1f);
        DrawCircle((int)(baseX + driftX), (int)(baseY + driftY), 1.3f, (Color){ 220, 210, 180, (unsigned char)(tw * 60.0f) });
    }
}

static unsigned char lerpU8(int a, int b, float t) { return (unsigned char)(a + (b - a) * t); }

static void drawRoom(const World *world, float daylight)
{
    int ox = GRID_ORIGIN_X, oy = GRID_ORIGIN_Y;
    DrawRectangle(ox - 8, oy - 8, ROOM_W + 16, ROOM_H + 16, (Color){ 30, 26, 30, 255 });

    for (int y = 0; y < WORLD_HEIGHT; y++)
        for (int x = 0; x < WORLD_WIDTH; x++)
        {
            int px = ox + x * WORLD_TILE_PX, py = oy + y * WORLD_TILE_PX;
            bool border = (x == 0 || y == 0 || x == WORLD_WIDTH - 1 || y == WORLD_HEIGHT - 1);
            if (border)
            {
                DrawRectangle(px, py, WORLD_TILE_PX, WORLD_TILE_PX, WALL_BODY);
                DrawRectangle(px, py, WORLD_TILE_PX, WORLD_TILE_PX / 3, WALL_BASE);
            }
            else
            {
                DrawRectangle(px, py, WORLD_TILE_PX, WORLD_TILE_PX, (y & 1) ? PLANK_A : PLANK_B);
                DrawRectangle(px, py, WORLD_TILE_PX, 1, PLANK_SEAM);
                if ((x + y * 2) % 6 == 0) DrawRectangle(px, py, 1, WORLD_TILE_PX, PLANK_SEAM);
            }
        }

    Rectangle inner = { ox + WORLD_TILE_PX, oy + WORLD_TILE_PX, ROOM_W - 2 * WORLD_TILE_PX, ROOM_H - 2 * WORLD_TILE_PX };
    DrawRectangleLinesEx(inner, 3, (Color){ 0, 0, 0, 60 });

    int wx = ox + (WORLD_WIDTH / 2 - 3) * WORLD_TILE_PX;
    int ww = 6 * WORLD_TILE_PX;
    int wy = oy, wh = WORLD_TILE_PX;
    float slant = 70.0f, beamLen = ROOM_H * 0.6f;
    Color beam = (Color){ 255, 240, 195, (unsigned char)(28 * daylight) };
    Vector2 tl = { wx + 6, wy + wh }, tr = { wx + ww - 6, wy + wh };
    Vector2 bl = { wx + 6 + slant, wy + wh + beamLen }, br = { wx + ww - 6 + slant * 1.6f, wy + wh + beamLen };
    DrawTriangle(tl, bl, br, beam);
    DrawTriangle(tl, br, tr, beam);

    DrawRectangle(wx - 3, wy - 2, ww + 6, wh + 5, (Color){ 92, 80, 68, 255 });
    Color sky = (Color){ lerpU8(28, 158, daylight), lerpU8(36, 206, daylight), lerpU8(66, 238, daylight), 255 };
    DrawRectangle(wx, wy, ww, wh, sky);
    if (daylight < 0.45f)
    {
        DrawCircle(wx + ww - 16, wy + wh / 2, 4.5f, (Color){ 235, 235, 210, 255 });
        DrawCircle(wx + 14, wy + 6, 1.0f, (Color){ 255, 255, 240, 200 });
        DrawCircle(wx + 34, wy + 13, 1.0f, (Color){ 255, 255, 240, 180 });
    }
    DrawRectangle(wx + ww / 2 - 1, wy, 2, wh, (Color){ 92, 80, 68, 255 });
    DrawRectangle(wx, wy + wh / 2 - 1, ww, 2, (Color){ 92, 80, 68, 255 });
}

static void itemCenter(const RoomItem *item, float *cx, float *cy)
{
    *cx = GRID_ORIGIN_X + item->x * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
    *cy = GRID_ORIGIN_Y + item->y * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
}

static void drawItemShape(ItemType type, float cx, float cy, bool hasFood, double time)
{
    switch (type)
    {
        case ITEM_RUG:
            DrawEllipse((int)cx, (int)cy, 26, 18, (Color){ 176, 96, 104, 255 });
            DrawEllipse((int)cx, (int)cy, 20, 13, (Color){ 206, 130, 132, 255 });
            DrawEllipse((int)cx, (int)cy, 11, 7, (Color){ 176, 96, 104, 255 });
            break;
        case ITEM_BED:
            DrawEllipse((int)cx, (int)(cy + 3), 20, 13, (Color){ 90, 110, 170, 255 });
            DrawEllipse((int)cx, (int)cy, 16, 10, (Color){ 150, 170, 220, 255 });
            break;
        case ITEM_PLANT:
            DrawRectangle((int)cx - 6, (int)cy + 2, 12, 10, (Color){ 158, 96, 60, 255 });
            DrawCircle((int)cx - 4, (int)cy - 3, 5, (Color){ 86, 162, 84, 255 });
            DrawCircle((int)cx + 4, (int)cy - 3, 5, (Color){ 96, 176, 92, 255 });
            DrawCircle((int)cx, (int)cy - 8, 6, (Color){ 110, 190, 100, 255 });
            break;
        case ITEM_POST:
            DrawEllipse((int)cx, (int)cy + 9, 12, 5, (Color){ 150, 120, 80, 255 });
            DrawRectangle((int)cx - 4, (int)cy - 12, 8, 22, (Color){ 206, 184, 138, 255 });
            DrawRectangle((int)cx - 4, (int)cy - 12, 8, 3, (Color){ 170, 146, 104, 255 });
            break;
        case ITEM_LITTER:
            DrawRectangle((int)cx - 15, (int)cy - 4, 30, 14, (Color){ 110, 104, 118, 255 });
            DrawRectangle((int)cx - 13, (int)cy - 6, 26, 11, (Color){ 198, 188, 168, 255 });
            DrawCircle((int)cx - 6, (int)cy - 1, 1.6f, (Color){ 168, 158, 138, 255 });
            DrawCircle((int)cx + 3, (int)cy - 2, 1.6f, (Color){ 180, 170, 150, 255 });
            DrawCircle((int)cx + 8, (int)cy + 1, 1.4f, (Color){ 158, 148, 130, 255 });
            break;
        case ITEM_BOWL:
        default:
        {
            float pulse = 0.5f + 0.5f * sinf((float)time * 3.0f + cx * 0.1f);
            if (hasFood) DrawCircleGradient((int)cx, (int)cy, 14.0f, (Color){ 255, 170, 90, (unsigned char)(70 * pulse) }, BLANK);
            DrawEllipse((int)cx, (int)cy + 2, 13, 8, (Color){ 150, 156, 166, 255 });
            DrawEllipse((int)cx, (int)cy, 11, 6, (Color){ 96, 100, 112, 255 });
            if (hasFood)
            {
                DrawCircle((int)cx - 4, (int)cy, 2.6f, (Color){ 235, 150, 80, 255 });
                DrawCircle((int)cx + 2, (int)cy - 1, 2.6f, (Color){ 245, 168, 96, 255 });
                DrawCircle((int)cx + 5, (int)cy + 1, 2.3f, (Color){ 230, 140, 76, 255 });
            }
            break;
        }
    }
}

static void drawItems(const RoomItem *items, int count, int held, double time)
{
    for (int i = 0; i < count; i++)
        if (items[i].type == ITEM_RUG) { float cx, cy; itemCenter(&items[i], &cx, &cy); drawItemShape(ITEM_RUG, cx, cy, false, time); }

    for (int i = 0; i < count; i++)
    {
        if (items[i].type == ITEM_RUG) continue;
        float cx, cy;
        itemCenter(&items[i], &cx, &cy);
        if (i == held) glow(cx, cy, 26.0f, ACCENT, 0.35f);
        DrawEllipse((int)cx, (int)cy + 9, 12, 4, (Color){ 0, 0, 0, 70 });
        drawItemShape(items[i].type, cx, cy, items[i].hasFood, time);
    }
}

static void drawZzz(float cx, float topY, double time)
{
    Color c = (Color){ 210, 220, 245, 235 };
    float w = sinf((float)time * 2.0f) * 2.0f;
    DrawText("z", (int)(cx + 2 + w), (int)(topY - 8), 12, c);
    DrawText("z", (int)(cx + 8 + w), (int)(topY - 16), 16, c);
    DrawText("z", (int)(cx + 15 + w), (int)(topY - 26), 20, c);
}

static void drawMeow(float cx, float topY)
{
    Rectangle bubble = { cx - 12.0f, topY - 22.0f, 24.0f, 16.0f };
    DrawRectangleRounded(bubble, 0.6f, 6, (Color){ 248, 244, 236, 245 });
    DrawTriangle((Vector2){ cx - 4, topY - 6 }, (Vector2){ cx, topY + 1 }, (Vector2){ cx + 4, topY - 6 }, (Color){ 248, 244, 236, 245 });
    DrawCircle((int)cx - 2, (int)topY - 11, 2.6f, INK);
    DrawRectangle((int)cx, (int)topY - 19, 2, 9, INK);
}

static void drawThought(float cx, float topY)
{
    Rectangle bubble = { cx - 10.0f, topY - 23.0f, 20.0f, 17.0f };
    DrawRectangleRounded(bubble, 0.6f, 6, (Color){ 248, 244, 236, 245 });
    DrawTriangle((Vector2){ cx - 4, topY - 6 }, (Vector2){ cx, topY + 1 }, (Vector2){ cx + 4, topY - 6 }, (Color){ 248, 244, 236, 245 });
    DrawText("?", (int)cx - 3, (int)topY - 21, 16, INK);
}

static void drawCat(const PixelCat *cat, const CatView *view, const CatBody *body,
                    float voice, float energy, double time)
{
    float drawSize = CAT_CANVAS_SIZE * CAT_DRAW_SCALE;
    float centerX = GRID_ORIGIN_X + view->x * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
    float centerY = GRID_ORIGIN_Y + view->y * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;

    float distance = fabsf(body->x - view->x) + fabsf(body->y - view->y);
    bool walking = !view->asleep && distance > 0.06f;
    float t = (float)time;
    float bob = view->asleep ? sinf(t * 2.0f) * 0.8f
                             : sinf(t * BOB_SPEED + view->x * 1.7f) * (walking ? BOB_AMP * 2.0f : BOB_AMP);
    float tilt = walking ? sinf(t * WALK_FREQ) * WALK_TILT : 0.0f;

    Color aura = cat->genome.primary;
    float auraPulse = 0.10f + energy * 0.45f + 0.06f * sinf(t * 6.0f);
    if (view->asleep) auraPulse *= 0.4f;
    glow(centerX, centerY, drawSize * 0.8f, aura, auraPulse);

    DrawEllipse((int)centerX, (int)(centerY + drawSize * 0.40f), drawSize * 0.28f, drawSize * 0.10f, (Color){ 0, 0, 0, 110 });

    Texture2D texture;
    float sx = drawSize, sy = drawSize, offsetY = 0.0f;
    if (view->asleep)
    {
        texture = cat->lie;
        offsetY = drawSize * 0.10f;
    }
    else if (walking)
    {
        texture = cat->walk[((int)(t * 8.0f)) & 1];
    }
    else
    {
        texture = cat->textures[view->mood];
        sy = drawSize * (1.0f + view->stretch * 0.5f);
        sx = drawSize * (1.0f - view->stretch * 0.18f);
        offsetY = -(sy - drawSize) * 0.5f;
    }

    Rectangle src = { 0.0f, 0.0f, (view->faceLeft ? -1.0f : 1.0f) * texture.width, (float)texture.height };
    Rectangle dest = { centerX, centerY + offsetY + bob, sx, sy };
    Vector2 origin = { sx * 0.5f, sy * 0.5f };
    DrawTexturePro(texture, src, dest, origin, tilt, WHITE);

    if (view->asleep) drawZzz(centerX, centerY - drawSize * 0.35f, time);
    else if (voice > MEOW_THRESHOLD) drawMeow(centerX, centerY - drawSize * 0.42f + bob);
    else if (view->curiosity > 0.4f) drawThought(centerX, centerY - drawSize * 0.42f + bob);
}

static void drawCatCard(int x, int y, const PixelCat *cat, const CatBody *body,
                        float voice, CatEmotion emotion, int pets, const char *activity)
{
    int w = 320, h = 80;
    DrawRectangleRounded((Rectangle){ x, y, w, h }, 0.13f, 8, PANEL_GLASS);
    DrawRectangleRoundedLines((Rectangle){ x, y, w, h }, 0.13f, 8, PANEL_LINE);
    DrawRectangleRounded((Rectangle){ x, y + 10, 4, h - 20 }, 1.0f, 4, cat->genome.primary);

    DrawRectangleRounded((Rectangle){ x + 12, y + 12, 56, 56 }, 0.2f, 6, (Color){ 20, 18, 28, 255 });
    PixelCatDraw(cat, (Vector2){ x + 18, y + 15 }, 3.0f, emotion, false);

    int tx = x + 80;
    char line[96];
    DrawText("Spikot", tx, y + 10, 22, RAYWHITE);
    snprintf(line, sizeof(line), "%s  -  %s   fish %d   pets %d", activity, CatEmotionName(emotion), body->foodEaten, pets);
    DrawText(line, tx, y + 36, 14, TEXT_DIM);
    DrawText("voice", tx, y + 57, 13, TEXT_DIM);
    DrawRectangleRounded((Rectangle){ tx + 44, y + 57, 160, 9 }, 1.0f, 4, (Color){ 20, 18, 28, 255 });
    if (voice > 0.01f)
        DrawRectangleRounded((Rectangle){ tx + 44, y + 57, (int)(160 * voice), 9 }, 1.0f, 4, ACCENT);
}

static const char *itemName(ItemType type)
{
    switch (type)
    {
        case ITEM_BOWL: return "bowl";
        case ITEM_BED: return "bed";
        case ITEM_PLANT: return "plant";
        case ITEM_POST: return "post";
        case ITEM_LITTER: return "litter";
        default: return "rug";
    }
}

static Rectangle paletteSlot(int i)
{
    return (Rectangle){ PANEL_X + i * (PALETTE_SLOT + PALETTE_GAP), PALETTE_Y + 22, PALETTE_SLOT, PALETTE_SLOT };
}

int PalettePick(Vector2 mouse)
{
    for (int i = 0; i < ITEM_TYPE_COUNT; i++)
        if (CheckCollisionPointRec(mouse, paletteSlot(i))) return i;
    return -1;
}

static void drawPalette(double time)
{
    DrawText("add things  (click, then drag to place)", PANEL_X, PALETTE_Y, 15, TEXT_DIM);
    for (int i = 0; i < ITEM_TYPE_COUNT; i++)
    {
        Rectangle slot = paletteSlot(i);
        bool hover = CheckCollisionPointRec(GetMousePosition(), slot);
        DrawRectangleRounded(slot, 0.2f, 6, hover ? (Color){ 48, 46, 64, 255 } : PANEL_GLASS);
        DrawRectangleRoundedLines(slot, 0.2f, 6, hover ? ACCENT : PANEL_LINE);
        drawItemShape((ItemType)i, slot.x + slot.width * 0.5f, slot.y + slot.height * 0.45f, true, time);
        DrawText(itemName((ItemType)i), (int)slot.x + 6, (int)(slot.y + slot.height - 14), 10, TEXT_DIM);
    }
}

static void drawBrain(const Network *net, int x)
{
    DrawText("the brain  -  blue senses, teal thinks, orange acts", x, BRAIN_VIS_Y - 26, 15, TEXT_DIM);
    for (int i = 0; i < SNN_NEURON_COUNT; i++)
    {
        int col = i % BRAIN_VIS_COLS, row = i / BRAIN_VIS_COLS;
        float cx = x + col * BRAIN_VIS_CELL + BRAIN_VIS_CELL * 0.5f;
        float cy = BRAIN_VIS_Y + row * BRAIN_VIS_CELL + BRAIN_VIS_CELL * 0.5f;
        Color base;
        if (i < BRAIN_INPUT_END) base = (Color){ 80, 150, 255, 255 };
        else if (i >= BRAIN_OUTPUT_BEGIN) base = (Color){ 255, 150, 70, 255 };
        else base = (Color){ 90, 230, 210, 255 };

        if (net->spiked[i])
        {
            glow(cx, cy, 7.0f, base, 0.5f);
            DrawCircle((int)cx, (int)cy, 3.0f, RAYWHITE);
        }
        else
        {
            float v = net->potential[i] / SNN_V_THRESHOLD;
            if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
            DrawCircle((int)cx, (int)cy, 2.4f, (Color){
                (unsigned char)(22 + (base.r - 22) * v),
                (unsigned char)(22 + (base.g - 22) * v),
                (unsigned char)(22 + (base.b - 22) * v), 255 });
        }
    }
}

static void drawVignette(void)
{
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, 80, (Color){ 0, 0, 0, 110 }, BLANK);
    DrawRectangleGradientV(0, WINDOW_HEIGHT - 80, WINDOW_WIDTH, 80, BLANK, (Color){ 0, 0, 0, 110 });
    DrawRectangleGradientH(0, 0, 80, WINDOW_HEIGHT, (Color){ 0, 0, 0, 110 }, BLANK);
    DrawRectangleGradientH(WINDOW_WIDTH - 80, 0, 80, WINDOW_HEIGHT, BLANK, (Color){ 0, 0, 0, 110 });
}

static const char *activityName(const CatAgent *agent, const CatView *view)
{
    if (view->asleep) return "sleeping";
    if (agent->exploring) return "searching";
    switch (agent->activeDrive)
    {
        case DRIVE_HUNGER: return "to the bowl";
        case DRIVE_CURIOSITY: return "exploring";
        case DRIVE_FATIGUE: return "to bed";
        case DRIVE_SCRATCH: return "scratching";
        case DRIVE_BLADDER: return "to the litter";
        case DRIVE_PLAY: return "zoomies!";
        case DRIVE_GROOM: return "grooming";
        default: return "wandering";
    }
}

static void drawModBar(int x, int y, int w, const char *label, float value, Color color)
{
    DrawText(label, x, y, 12, TEXT_DIM);
    int bx = x + 36;
    DrawRectangleRounded((Rectangle){ bx, y, w, 9 }, 1.0f, 4, (Color){ 20, 18, 28, 255 });
    if (value > 0.01f)
        DrawRectangleRounded((Rectangle){ bx, y, (int)(w * value), 9 }, 1.0f, 4, color);
}

static void drawNeuromods(int x, int y, const Neuromods *mods)
{
    DrawText("brain chemistry", x, y, 15, TEXT_DIM);
    y += 22;
    int w = 150;
    drawModBar(x, y, w, "DA",  mods->dopamine,      (Color){ 255, 196, 90, 255 }); y += 16;
    drawModBar(x, y, w, "5HT", mods->serotonin,     (Color){ 120, 210, 130, 255 }); y += 16;
    drawModBar(x, y, w, "NE",  mods->noradrenaline, (Color){ 240, 110, 110, 255 }); y += 16;
    drawModBar(x, y, w, "ACh", mods->acetylcholine, (Color){ 110, 170, 255, 255 }); y += 20;
    char line[64];
    snprintf(line, sizeof(line), "mood %+.2f   arousal %.2f", mods->valence, mods->arousal);
    DrawText(line, x, y, 12, TEXT_DIM);
}

static float catEnergy(const CatAgent *agent)
{
    float e = (float)NetworkSpikeCount(&agent->net) / (SNN_NEURON_COUNT * 0.14f);
    return e > 1.0f ? 1.0f : e;
}

void RenderScene(const CatAgent *agent, const CatBody *body, const CatView *view, const PixelCat *cat,
                 float voice, const World *world, const RoomItem *items, int itemCount,
                 int heldItem, bool showBrain, double time)
{
    float phase = (float)(fmod(time, DAY_LENGTH) / DAY_LENGTH);
    float daylight = 0.5f + 0.5f * sinf(phase * 2.0f * PI);

    BeginDrawing();
    drawBackground(time);
    drawRoom(world, daylight);
    drawItems(items, itemCount, heldItem, time);
    drawCat(cat, view, body, voice, catEnergy(agent), time);
    drawParticles();

    if (daylight < 0.95f)
        DrawRectangle(GRID_ORIGIN_X - 8, GRID_ORIGIN_Y - 8, ROOM_W + 16, ROOM_H + 16,
                      (Color){ 22, 28, 64, (unsigned char)((1.0f - daylight) * 120.0f) });

    int y = GRID_ORIGIN_Y - 4;
    DrawText("Spikot", PANEL_X, y, 36, RAYWHITE); y += 44;
    DrawText("a cat with a spiking brain", PANEL_X, y, 15, TEXT_DIM); y += 22;
    DrawText("drag the cat to carry it, click to pet", PANEL_X, y, 14, TEXT_DIM); y += 18;
    DrawText(showBrain ? "b: hide brain    r: reset    esc: quit"
                       : "b: peek inside the brain    r: reset    esc: quit",
             PANEL_X, y, 14, TEXT_DIM); y += 26;

    drawCatCard(PANEL_X, y, cat, body, voice, view->mood, view->pets, activityName(agent, view));
    drawNeuromods(PANEL_X, y + 92, &agent->mods);

    if (showBrain) drawBrain(&agent->net, PANEL_X);
    else drawPalette(time);

    drawVignette();
    DrawFPS(WINDOW_WIDTH - 88, 12);
    EndDrawing();
}
