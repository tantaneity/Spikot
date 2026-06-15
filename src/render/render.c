#include "render/render.h"
#include "config.h"
#include "snn/network.h"
#include <math.h>
#include <stdio.h>

#define MOVE_LERP 0.22f
#define CAT_DRAW_SCALE 2.4f
#define BOB_SPEED 5.0f
#define BOB_AMP 1.8f
#define MEOW_THRESHOLD 0.11f
#define WALK_TILT 8.0f
#define WALK_FREQ 14.0f
#define MOOD_HOLD_HAPPY 1.0f
#define MOOD_HOLD_SCARED 0.6f
#define MOOD_HOLD_PET 1.6f
#define MAX_HEARTS 32
#define HEART_RISE 24.0f
#define MOTE_COUNT 38

#define BRAIN_VIS_COLS 32
#define BRAIN_VIS_CELL 13
#define BRAIN_VIS_Y 474
#define BRAIN_INPUT_END SNN_INPUT_NEURONS
#define BRAIN_OUTPUT_BEGIN (SNN_NEURON_COUNT - SNN_OUTPUT_NEURONS)

#define ROOM_W (WORLD_WIDTH * WORLD_TILE_PX)
#define ROOM_H (WORLD_HEIGHT * WORLD_TILE_PX)

static const Color BG_TOP = (Color){ 18, 18, 30, 255 };
static const Color BG_BOTTOM = (Color){ 9, 9, 16, 255 };
static const Color FLOOR_A = (Color){ 30, 32, 48, 255 };
static const Color FLOOR_B = (Color){ 34, 37, 55, 255 };
static const Color FLOOR_GROUT = (Color){ 22, 23, 36, 255 };
static const Color WALL_BODY = (Color){ 56, 54, 82, 255 };
static const Color WALL_TOP = (Color){ 92, 88, 130, 255 };
static const Color FISH_BODY = (Color){ 255, 184, 96, 255 };
static const Color FISH_TAIL = (Color){ 235, 140, 70, 255 };
static const Color FISH_GLOW = (Color){ 255, 170, 90, 90 };
static const Color INK = (Color){ 18, 16, 28, 255 };
static const Color PANEL_GLASS = (Color){ 26, 26, 40, 220 };
static const Color PANEL_LINE = (Color){ 60, 62, 92, 255 };
static const Color TEXT_DIM = (Color){ 150, 152, 178, 255 };

typedef struct { float x, y, life; bool active; } Heart;
static Heart g_hearts[MAX_HEARTS];

void HeartsSpawn(float x, float y)
{
    for (int i = 0; i < MAX_HEARTS; i++)
        if (!g_hearts[i].active) { g_hearts[i] = (Heart){ x, y, 1.0f, true }; return; }
}

void HeartsUpdate(float dt)
{
    for (int i = 0; i < MAX_HEARTS; i++)
        if (g_hearts[i].active)
        {
            g_hearts[i].y -= HEART_RISE * dt;
            g_hearts[i].life -= dt;
            if (g_hearts[i].life <= 0.0f) g_hearts[i].active = false;
        }
}

static void drawHearts(void)
{
    for (int i = 0; i < MAX_HEARTS; i++)
    {
        if (!g_hearts[i].active) continue;
        float x = g_hearts[i].x, y = g_hearts[i].y, a = g_hearts[i].life;
        Color pink = (Color){ 255, 120, 150, (unsigned char)(a * 230.0f) };
        DrawCircleGradient((int)x, (int)y, 12.0f, (Color){ 255, 120, 150, (unsigned char)(a * 60.0f) }, BLANK);
        DrawCircle((int)x - 3, (int)y, 3.2f, pink);
        DrawCircle((int)x + 3, (int)y, 3.2f, pink);
        DrawTriangle((Vector2){ x - 6, y + 1 }, (Vector2){ x, y + 8 }, (Vector2){ x + 6, y + 1 }, pink);
    }
}

static void glow(float cx, float cy, float radius, Color tint, float strength)
{
    Color inner = tint;
    inner.a = (unsigned char)(strength * 255.0f);
    DrawCircleGradient((int)cx, (int)cy, radius, inner, BLANK);
}

static bool foodInSight(const World *world, const CatBody *body, int otherX, int otherY)
{
    float vision[64];
    WorldVisionFor(world, body, otherX, otherY, vision);
    int size = WorldVisionSize();
    for (int i = 0; i < size; i++)
        if (vision[i] > 0.5f) return true;
    return false;
}

static CatEmotion calmMood(const World *world, const CatBody *body, int otherX, int otherY)
{
    if (body->hunger > 0.7f) return EMOTION_HUNGRY;
    if (foodInSight(world, body, otherX, otherY)) return EMOTION_CURIOUS;
    return EMOTION_CONTENT;
}

void MoodUpdate(CatView *view, const CatAgent *agent, const World *world,
                const CatBody *body, int otherX, int otherY, float dt)
{
    if (view->moodHold > 0.0f) view->moodHold -= dt;

    if (agent->lastReward >= REWARD_FOOD * 0.5f) { view->mood = EMOTION_HAPPY; view->moodHold = MOOD_HOLD_HAPPY; }
    else if (agent->lastReward <= REWARD_OBSTACLE * 0.5f) { view->mood = EMOTION_SCARED; view->moodHold = MOOD_HOLD_SCARED; }
    else if (view->moodHold <= 0.0f) view->mood = calmMood(world, body, otherX, otherY);
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
        float driftX = sinf((float)time * 0.3f + seed) * 18.0f;
        float driftY = cosf((float)time * 0.22f + seed * 1.7f) * 14.0f;
        float twinkle = 0.25f + 0.25f * sinf((float)time * 1.5f + seed * 3.1f);
        DrawCircle((int)(baseX + driftX), (int)(baseY + driftY), 1.4f,
                   (Color){ 150, 170, 210, (unsigned char)(twinkle * 90.0f) });
    }
}

static void drawFish(float cx, float cy, double time)
{
    float pulse = 0.6f + 0.4f * sinf((float)time * 3.0f + cx * 0.1f);
    DrawCircleGradient((int)cx, (int)cy, 11.0f, (Color){ FISH_GLOW.r, FISH_GLOW.g, FISH_GLOW.b, (unsigned char)(FISH_GLOW.a * pulse) }, BLANK);
    DrawEllipse((int)cx, (int)cy, 6.0f, 4.0f, FISH_BODY);
    DrawTriangle((Vector2){ cx + 4, cy }, (Vector2){ cx + 10, cy + 4 }, (Vector2){ cx + 10, cy - 4 }, FISH_TAIL);
    DrawCircle((int)cx - 3, (int)cy - 1, 1.3f, INK);
}

static void drawRoom(const World *world, double time)
{
    int ox = GRID_ORIGIN_X, oy = GRID_ORIGIN_Y;

    DrawRectangleRounded((Rectangle){ ox - 10, oy - 10, ROOM_W + 20, ROOM_H + 20 }, 0.03f, 8, (Color){ 20, 21, 33, 255 });

    for (int y = 0; y < WORLD_HEIGHT; y++)
        for (int x = 0; x < WORLD_WIDTH; x++)
        {
            if (world->tiles[y][x] == TILE_OBSTACLE) continue;
            int px = ox + x * WORLD_TILE_PX, py = oy + y * WORLD_TILE_PX;
            DrawRectangle(px, py, WORLD_TILE_PX, WORLD_TILE_PX, ((x + y) & 1) ? FLOOR_A : FLOOR_B);
            DrawRectangle(px, py, WORLD_TILE_PX, 1, FLOOR_GROUT);
            DrawRectangle(px, py, 1, WORLD_TILE_PX, FLOOR_GROUT);
        }

    DrawCircleGradient(ox + ROOM_W / 2, oy + ROOM_H / 2, ROOM_W * 0.55f, (Color){ 90, 110, 160, 26 }, BLANK);

    for (int y = 0; y < WORLD_HEIGHT; y++)
        for (int x = 0; x < WORLD_WIDTH; x++)
        {
            int px = ox + x * WORLD_TILE_PX, py = oy + y * WORLD_TILE_PX;
            if (world->tiles[y][x] == TILE_OBSTACLE)
            {
                DrawRectangle(px + 2, py + 4, WORLD_TILE_PX, WORLD_TILE_PX, (Color){ 0, 0, 0, 90 });
                DrawRectangle(px, py, WORLD_TILE_PX, WORLD_TILE_PX, WALL_BODY);
                DrawRectangle(px, py, WORLD_TILE_PX, 4, WALL_TOP);
            }
            else if (world->tiles[y][x] == TILE_FOOD)
            {
                drawFish(px + WORLD_TILE_PX * 0.5f, py + WORLD_TILE_PX * 0.5f, time);
            }
        }
}

static void drawZzz(float cx, float topY, double time)
{
    Color c = (Color){ 200, 215, 245, 235 };
    float w = sinf((float)time * 2.0f) * 2.0f;
    DrawText("z", (int)(cx + 2 + w), (int)(topY - 8), 12, c);
    DrawText("z", (int)(cx + 8 + w), (int)(topY - 16), 16, c);
    DrawText("z", (int)(cx + 15 + w), (int)(topY - 26), 20, c);
}

static void drawMeow(float cx, float topY)
{
    Rectangle bubble = { cx - 12.0f, topY - 22.0f, 24.0f, 16.0f };
    DrawRectangleRounded(bubble, 0.6f, 6, (Color){ 246, 240, 230, 240 });
    DrawTriangle((Vector2){ cx - 4, topY - 6 }, (Vector2){ cx, topY + 1 }, (Vector2){ cx + 4, topY - 6 }, (Color){ 246, 240, 230, 240 });
    DrawCircle((int)cx - 2, (int)topY - 11, 2.6f, INK);
    DrawRectangle((int)cx, (int)topY - 19, 2, 9, INK);
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
    float auraPulse = 0.10f + energy * 0.5f + 0.06f * sinf(t * 6.0f);
    if (view->asleep) auraPulse *= 0.4f;
    glow(centerX, centerY, drawSize * 0.85f, aura, auraPulse);

    DrawEllipse((int)centerX, (int)(centerY + drawSize * 0.42f), drawSize * 0.30f, drawSize * 0.11f, (Color){ 0, 0, 0, 110 });

    Texture2D texture;
    if (view->asleep) texture = cat->textures[EMOTION_HAPPY];
    else if (walking) texture = cat->walk[((int)(t * 8.0f)) & 1];
    else texture = cat->textures[view->mood];

    Rectangle src = { 0.0f, 0.0f, (view->faceLeft ? -1.0f : 1.0f) * texture.width, (float)texture.height };
    float offsetY = view->asleep ? drawSize * 0.12f : 0.0f;
    Rectangle dest = { centerX, centerY + offsetY + bob, drawSize, drawSize };
    Vector2 origin = { drawSize * 0.5f, drawSize * 0.5f };
    DrawTexturePro(texture, src, dest, origin, tilt, WHITE);

    if (view->asleep) drawZzz(centerX, centerY - drawSize * 0.35f, time);
    else if (voice > MEOW_THRESHOLD) drawMeow(centerX, centerY - drawSize * 0.45f + bob);
}

static int drawCatCard(int x, int y, const char *label, const PixelCat *cat,
                       const CatBody *body, float voice, CatEmotion emotion, int pets)
{
    int w = 300, h = 78;
    DrawRectangleRounded((Rectangle){ x, y, w, h }, 0.14f, 8, PANEL_GLASS);
    DrawRectangleRoundedLines((Rectangle){ x, y, w, h }, 0.14f, 8, PANEL_LINE);
    DrawRectangleRounded((Rectangle){ x, y + 10, 4, h - 20 }, 1.0f, 4, cat->genome.primary);

    DrawRectangleRounded((Rectangle){ x + 12, y + 11, 56, 56 }, 0.2f, 6, (Color){ 18, 19, 30, 255 });
    PixelCatDraw(cat, (Vector2){ x + 18, y + 14 }, 3.0f, emotion, false);

    int tx = x + 80;
    char line[96];
    snprintf(line, sizeof(line), "cat %s", label);
    DrawText(line, tx, y + 10, 22, RAYWHITE);
    snprintf(line, sizeof(line), "%s   fish %d   pets %d", CatEmotionName(emotion), body->foodEaten, pets);
    DrawText(line, tx, y + 36, 15, TEXT_DIM);

    DrawText("voice", tx, y + 56, 13, TEXT_DIM);
    DrawRectangleRounded((Rectangle){ tx + 44, y + 56, 150, 9 }, 1.0f, 4, (Color){ 18, 19, 30, 255 });
    if (voice > 0.01f)
        DrawRectangleRounded((Rectangle){ tx + 44, y + 56, (int)(150 * voice), 9 }, 1.0f, 4, (Color){ 255, 196, 80, 255 });
    return y + h + 12;
}

static void drawBrain(const Network *net, int x, const char *label)
{
    DrawText(label, x, BRAIN_VIS_Y - 26, 15, TEXT_DIM);
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
                (unsigned char)(20 + (base.r - 20) * v),
                (unsigned char)(20 + (base.g - 20) * v),
                (unsigned char)(20 + (base.b - 20) * v), 255 });
        }
    }
}

static void drawVignette(void)
{
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, 90, (Color){ 0, 0, 0, 120 }, BLANK);
    DrawRectangleGradientV(0, WINDOW_HEIGHT - 90, WINDOW_WIDTH, 90, BLANK, (Color){ 0, 0, 0, 120 });
    DrawRectangleGradientH(0, 0, 90, WINDOW_HEIGHT, (Color){ 0, 0, 0, 120 }, BLANK);
    DrawRectangleGradientH(WINDOW_WIDTH - 90, 0, 90, WINDOW_HEIGHT, BLANK, (Color){ 0, 0, 0, 120 });
}

static float catEnergy(const CatAgent *agent)
{
    float e = (float)NetworkSpikeCount(&agent->net) / (SNN_NEURON_COUNT * 0.14f);
    return e > 1.0f ? 1.0f : e;
}

void RenderScene(const CatAgent *agentA, const CatBody *bodyA, const CatView *viewA, const PixelCat *catA, float voiceA,
                 const CatAgent *agentB, const CatBody *bodyB, const CatView *viewB, const PixelCat *catB, float voiceB,
                 const World *world, bool showBrain, double time)
{
    int panelX = GRID_ORIGIN_X + ROOM_W + 34;

    BeginDrawing();
    drawBackground(time);
    drawRoom(world, time);
    drawCat(catA, viewA, bodyA, voiceA, catEnergy(agentA), time);
    drawCat(catB, viewB, bodyB, voiceB, catEnergy(agentB), time);
    drawHearts();

    int y = GRID_ORIGIN_Y - 2;
    DrawText(WINDOW_TITLE, panelX, y, 34, RAYWHITE); y += 42;
    DrawText("two cats, two spiking brains", panelX, y, 15, TEXT_DIM); y += 21;
    DrawText("drag to move   click to pet", panelX, y, 14, TEXT_DIM); y += 19;
    DrawText(showBrain ? "b: hide brain    r: new cats    esc: quit"
                       : "b: peek inside the brain    r: new cats    esc: quit",
             panelX, y, 14, TEXT_DIM); y += 26;

    if (showBrain)
    {
        drawBrain(&agentA->net, panelX, "cat A brain  -  blue: senses   teal: hidden   orange: actions");
    }
    else
    {
        y = drawCatCard(panelX, y, "A", catA, bodyA, voiceA, viewA->mood, viewA->pets);
        y = drawCatCard(panelX, y, "B", catB, bodyB, voiceB, viewB->mood, viewB->pets);
    }

    drawVignette();
    DrawFPS(WINDOW_WIDTH - 88, 12);
    EndDrawing();
}
