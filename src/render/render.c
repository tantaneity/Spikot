#include "render/render.h"
#include "config.h"
#include "snn/network.h"
#include <math.h>
#include <stdio.h>

#define MOVE_LERP 0.25f
#define CAT_DRAW_SCALE 1.75f
#define BOB_SPEED 5.0f
#define BOB_AMP 1.6f
#define MEOW_THRESHOLD 0.11f
#define WALK_TILT 7.0f
#define WALK_FREQ 14.0f
#define MOOD_HOLD_HAPPY 1.0f
#define MOOD_HOLD_SCARED 0.6f
#define MOOD_HOLD_PET 1.6f
#define MAX_HEARTS 32
#define HEART_RISE 22.0f

#define BRAIN_VIS_COLS 32
#define BRAIN_VIS_CELL 13
#define BRAIN_VIS_Y 470
#define BRAIN_INPUT_END SNN_INPUT_NEURONS
#define BRAIN_OUTPUT_BEGIN (SNN_NEURON_COUNT - SNN_OUTPUT_NEURONS)

static const Color FLOOR_COLOR_A = (Color){ 70, 56, 52, 255 };
static const Color FLOOR_COLOR_B = (Color){ 78, 63, 58, 255 };
static const Color WALL_COLOR = (Color){ 120, 84, 60, 255 };
static const Color WALL_TOP_COLOR = (Color){ 150, 110, 82, 255 };
static const Color FISH_COLOR = (Color){ 240, 158, 78, 255 };
static const Color FISH_TAIL_COLOR = (Color){ 224, 128, 60, 255 };
static const Color SHADOW_COLOR = (Color){ 0, 0, 0, 70 };
static const Color VOICE_COLOR = (Color){ 255, 196, 80, 255 };
static const Color ROOM_BG_COLOR = (Color){ 38, 30, 34, 255 };

typedef struct {
    float x;
    float y;
    float life;
    bool active;
} Heart;

static Heart g_hearts[MAX_HEARTS];

void HeartsSpawn(float x, float y)
{
    for (int i = 0; i < MAX_HEARTS; i++)
        if (!g_hearts[i].active)
        {
            g_hearts[i] = (Heart){ x, y, 1.0f, true };
            return;
        }
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
        float x = g_hearts[i].x;
        float y = g_hearts[i].y;
        Color pink = (Color){ 255, 120, 150, (unsigned char)(g_hearts[i].life * 230.0f) };
        DrawCircle((int)x - 3, (int)y, 3.2f, pink);
        DrawCircle((int)x + 3, (int)y, 3.2f, pink);
        DrawTriangle((Vector2){ x - 6, y + 1 }, (Vector2){ x, y + 8 }, (Vector2){ x + 6, y + 1 }, pink);
    }
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

    if (agent->lastReward >= REWARD_FOOD * 0.5f)
    {
        view->mood = EMOTION_HAPPY;
        view->moodHold = MOOD_HOLD_HAPPY;
    }
    else if (agent->lastReward <= REWARD_OBSTACLE * 0.5f)
    {
        view->mood = EMOTION_SCARED;
        view->moodHold = MOOD_HOLD_SCARED;
    }
    else if (view->moodHold <= 0.0f)
    {
        view->mood = calmMood(world, body, otherX, otherY);
    }
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

static void drawFish(int cx, int cy)
{
    DrawEllipse(cx, cy, 6.0f, 4.0f, FISH_COLOR);
    DrawTriangle((Vector2){ cx + 4, cy },
                 (Vector2){ cx + 10, cy + 4 },
                 (Vector2){ cx + 10, cy - 4 }, FISH_TAIL_COLOR);
    DrawCircle(cx - 3, cy - 1, 1.3f, (Color){ 36, 24, 24, 255 });
}

static void drawWorld(const World *world)
{
    DrawRectangle(GRID_ORIGIN_X - 6, GRID_ORIGIN_Y - 6,
                  WORLD_WIDTH * WORLD_TILE_PX + 12, WORLD_HEIGHT * WORLD_TILE_PX + 12, ROOM_BG_COLOR);

    for (int y = 0; y < WORLD_HEIGHT; y++)
    {
        for (int x = 0; x < WORLD_WIDTH; x++)
        {
            int px = GRID_ORIGIN_X + x * WORLD_TILE_PX;
            int py = GRID_ORIGIN_Y + y * WORLD_TILE_PX;
            TileType tile = world->tiles[y][x];

            if (tile == TILE_OBSTACLE)
            {
                DrawRectangle(px, py, WORLD_TILE_PX, WORLD_TILE_PX, WALL_COLOR);
                DrawRectangle(px, py, WORLD_TILE_PX, 4, WALL_TOP_COLOR);
            }
            else
            {
                Color floor = ((x + y) & 1) ? FLOOR_COLOR_A : FLOOR_COLOR_B;
                DrawRectangle(px, py, WORLD_TILE_PX, WORLD_TILE_PX, floor);
                if (tile == TILE_FOOD)
                    drawFish(px + WORLD_TILE_PX / 2, py + WORLD_TILE_PX / 2);
            }
        }
    }
}

static void drawMeow(float cx, float topY)
{
    Rectangle bubble = { cx - 12.0f, topY - 22.0f, 24.0f, 16.0f };
    DrawRectangleRounded(bubble, 0.6f, 6, (Color){ 246, 240, 230, 235 });
    DrawTriangle((Vector2){ cx - 4, topY - 6 },
                 (Vector2){ cx, topY + 1 },
                 (Vector2){ cx + 4, topY - 6 }, (Color){ 246, 240, 230, 235 });
    Color ink = (Color){ 70, 56, 84, 255 };
    DrawCircle((int)cx - 2, (int)topY - 11, 2.6f, ink);
    DrawRectangle((int)cx, (int)topY - 19, 2, 9, ink);
}

static void drawZzz(float cx, float topY, double time)
{
    Color ink = (Color){ 210, 220, 245, 230 };
    float wave = sinf((float)time * 2.0f) * 2.0f;
    DrawText("z", (int)(cx + 2 + wave), (int)(topY - 8), 12, ink);
    DrawText("z", (int)(cx + 8 + wave), (int)(topY - 16), 16, ink);
    DrawText("z", (int)(cx + 15 + wave), (int)(topY - 26), 20, ink);
}

static void drawCat(const PixelCat *cat, const CatView *view, const CatBody *body, float voice, double time)
{
    float drawSize = CAT_CANVAS_SIZE * CAT_DRAW_SCALE;
    float centerX = GRID_ORIGIN_X + view->x * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
    float centerY = GRID_ORIGIN_Y + view->y * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;

    float distance = fabsf(body->x - view->x) + fabsf(body->y - view->y);
    bool walking = !view->asleep && distance > 0.06f;
    float t = (float)time;
    float bob = view->asleep ? sinf(t * 2.0f) * 0.8f
                             : sinf(t * BOB_SPEED + view->x * 1.7f) * (walking ? BOB_AMP * 2.2f : BOB_AMP);
    float tilt = walking ? sinf(t * WALK_FREQ) * WALK_TILT : 0.0f;

    DrawEllipse((int)centerX, (int)(centerY + 8.0f), view->asleep ? 11.0f : 9.0f, 3.5f, SHADOW_COLOR);

    Texture2D texture;
    if (view->asleep) texture = cat->textures[EMOTION_HAPPY];
    else if (walking) texture = cat->walk[((int)(t * 8.0f)) & 1];
    else texture = cat->textures[view->mood];
    Rectangle source = { 0.0f, 0.0f, (view->faceLeft ? -1.0f : 1.0f) * texture.width, (float)texture.height };
    float offsetY = view->asleep ? 4.0f : -3.0f;
    Rectangle dest = { centerX, centerY + offsetY + bob, drawSize, drawSize };
    Vector2 origin = { drawSize * 0.5f, drawSize * 0.5f };
    DrawTexturePro(texture, source, dest, origin, tilt, WHITE);

    if (view->asleep) drawZzz(centerX, centerY - drawSize * 0.4f, time);
    else if (voice > MEOW_THRESHOLD) drawMeow(centerX, centerY - drawSize * 0.5f + bob);
}

static void drawVoiceBar(int x, int y, int width, float level, Color tint)
{
    DrawRectangle(x, y, width, 10, (Color){ 40, 40, 52, 255 });
    DrawRectangle(x, y, (int)(width * level), 10, tint);
}

static int drawCatCard(int panelX, int y, const char *label, const PixelCat *cat,
                       const CatBody *body, float voice, CatEmotion emotion, int pets)
{
    DrawRectangleRounded((Rectangle){ panelX, y, 300, 74 }, 0.12f, 6, (Color){ 46, 40, 46, 255 });
    PixelCatDraw(cat, (Vector2){ panelX + 8, y + 9 }, 3.5f, emotion, false);

    int textX = panelX + 72;
    char line[96];
    snprintf(line, sizeof(line), "cat %s", label);
    DrawText(line, textX, y + 8, 20, RAYWHITE);
    snprintf(line, sizeof(line), "%s   fish %d   pets %d", CatEmotionName(emotion), body->foodEaten, pets);
    DrawText(line, textX, y + 32, 16, (Color){ 200, 195, 190, 255 });
    DrawText("voice", textX, y + 54, 14, GRAY);
    drawVoiceBar(textX + 44, y + 54, 150, voice, VOICE_COLOR);
    return y + 86;
}

static Color neuronColor(const Network *net, int index)
{
    if (net->spiked[index]) return RAYWHITE;

    float intensity = net->potential[index] / SNN_V_THRESHOLD;
    if (intensity < 0.0f) intensity = 0.0f;
    else if (intensity > 1.0f) intensity = 1.0f;

    unsigned char r, g, b;
    if (index < BRAIN_INPUT_END) { r = 70; g = 130; b = 255; }
    else if (index >= BRAIN_OUTPUT_BEGIN) { r = 255; g = 150; b = 60; }
    else { r = 80; g = 220; b = 200; }

    unsigned char floor = 16;
    return (Color){
        (unsigned char)(floor + (r - floor) * intensity),
        (unsigned char)(floor + (g - floor) * intensity),
        (unsigned char)(floor + (b - floor) * intensity),
        255
    };
}

static void drawBrain(const Network *net, int originX, const char *label)
{
    DrawText(label, originX, BRAIN_VIS_Y - 24, 16, GRAY);
    for (int index = 0; index < SNN_NEURON_COUNT; index++)
    {
        int col = index % BRAIN_VIS_COLS;
        int row = index / BRAIN_VIS_COLS;
        int px = originX + col * BRAIN_VIS_CELL;
        int py = BRAIN_VIS_Y + row * BRAIN_VIS_CELL;
        DrawRectangle(px, py, BRAIN_VIS_CELL - 2, BRAIN_VIS_CELL - 2, neuronColor(net, index));
    }
}

void RenderScene(const CatAgent *agentA, const CatBody *bodyA, const CatView *viewA, const PixelCat *catA, float voiceA,
                 const CatAgent *agentB, const CatBody *bodyB, const CatView *viewB, const PixelCat *catB, float voiceB,
                 const World *world, bool showBrain, double time)
{
    int panelX = GRID_ORIGIN_X + WORLD_WIDTH * WORLD_TILE_PX + 28;

    BeginDrawing();
    ClearBackground(BACKGROUND_COLOR);
    drawWorld(world);
    drawCat(catA, viewA, bodyA, voiceA, time);
    drawCat(catB, viewB, bodyB, voiceB, time);
    drawHearts();

    int y = GRID_ORIGIN_Y;
    DrawText(WINDOW_TITLE, panelX, y, 30, RAYWHITE); y += 38;
    DrawText("drag a cat to move it, click to pet", panelX, y, 15, GRAY); y += 22;
    DrawText(showBrain ? "b: hide brain    r: new cats    esc: quit"
                       : "b: peek inside the brain    r: new cats    esc: quit",
             panelX, y, 15, GRAY); y += 28;

    if (showBrain)
    {
        drawBrain(&agentA->net, panelX, "cat A brain (blue in / orange out)");
    }
    else
    {
        y = drawCatCard(panelX, y, "A", catA, bodyA, voiceA, viewA->mood, viewA->pets);
        y += 12;
        y = drawCatCard(panelX, y, "B", catB, bodyB, voiceB, viewB->mood, viewB->pets);
    }

    DrawFPS(WINDOW_WIDTH - 90, 12);
    EndDrawing();
}
