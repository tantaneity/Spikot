#include "raylib.h"
#include "config.h"
#include "cat/genome.h"
#include "cat/pixel_cat.h"
#include "snn/network.h"
#include "env/world.h"
#include "agent/agent.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define EXPORT_FLAG "--export"
#define EXPORT_COUNT 2
#define EXPORT_DIR "export"

#define SNN_TEST_FLAG "--snn-test"
#define SNN_TEST_TICKS 800
#define SNN_TEST_REPORT_EVERY 100
#define SNN_TEST_INPUT_NEURONS 24
#define SNN_TEST_INPUT_PROB 0.6f
#define SNN_TEST_INPUT_DRIVE 0.85f

#define AGENT_TEST_FLAG "--agent-test"
#define AGENT_TEST_STEPS 8000
#define AGENT_TEST_WINDOW 1000

#define LEARN_TEST_FLAG "--learn-test"
#define LEARN_TEST_STEPS 30000
#define LEARN_TEST_WINDOW 2000

#define SHOT_FLAG "--shot"
#define SHOT_WARMUP_STEPS 400
#define SHOT_PATH "export/shot.png"

#define GRID_ORIGIN_X 40
#define GRID_ORIGIN_Y 40
#define SIM_FRAME_INTERVAL 6

#define BRAIN_VIS_COLS 32
#define BRAIN_VIS_CELL 13
#define BRAIN_VIS_Y 470
#define BRAIN_INPUT_END SNN_INPUT_NEURONS
#define BRAIN_OUTPUT_BEGIN (SNN_NEURON_COUNT - SNN_OUTPUT_NEURONS)

#define CAT_A_SEED_OFFSET 101u
#define CAT_B_SEED_OFFSET 202u

#define MOVE_LERP 0.25f
#define CAT_DRAW_SCALE 1.75f
#define BOB_SPEED 5.0f
#define BOB_AMP 1.6f
#define MEOW_THRESHOLD 0.11f

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
    bool faceLeft;
} CatView;

static uint32_t xorshiftSeed(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static uint32_t nextSeed(void)
{
    return (uint32_t)GetRandomValue(1, 2000000000);
}

static int runSnnTest(void)
{
    Network *network = malloc(sizeof(Network));
    if (!network) { fprintf(stderr, "alloc failed\n"); return 1; }

    NetworkInit(network, 12345u);

    float external[SNN_NEURON_COUNT];
    uint32_t state = 999u;
    long totalSpikes = 0;

    for (int tick = 0; tick < SNN_TEST_TICKS; tick++)
    {
        for (int j = 0; j < SNN_NEURON_COUNT; j++) external[j] = 0.0f;
        for (int j = 0; j < SNN_TEST_INPUT_NEURONS; j++)
            if ((xorshiftSeed(&state) >> 8) * (1.0f / 16777216.0f) < SNN_TEST_INPUT_PROB)
                external[j] = SNN_TEST_INPUT_DRIVE;

        NetworkStep(network, external);
        NetworkApplyReward(network, 1.0f);
        totalSpikes += NetworkSpikeCount(network);
        if (tick % SNN_TEST_REPORT_EVERY == 0)
            printf("tick %4d   spikes %3d\n", tick, NetworkSpikeCount(network));
    }

    printf("---\navg spikes/tick %.2f\n", (double)totalSpikes / SNN_TEST_TICKS);
    free(network);
    return 0;
}

static int runAgentTest(void)
{
    CatAgent *agentA = malloc(sizeof(CatAgent));
    CatAgent *agentB = malloc(sizeof(CatAgent));
    World *world = malloc(sizeof(World));
    if (!agentA || !agentB || !world)
    {
        fprintf(stderr, "alloc failed\n");
        free(agentA); free(agentB); free(world);
        return 1;
    }

    AgentInit(agentA, 4242u);
    AgentInit(agentB, 7777u);
    WorldInit(world, 777u);

    CatBody bodyA, bodyB;
    CatBodyInit(&bodyA, CAT_A_START_X, CAT_A_START_Y);
    CatBodyInit(&bodyB, CAT_B_START_X, CAT_B_START_Y);

    float voiceA = 0.0f, voiceB = 0.0f;
    int windowFoodA = 0, windowFoodB = 0;
    double windowVoiceA = 0.0, windowVoiceB = 0.0;

    for (int step = 1; step <= AGENT_TEST_STEPS; step++)
    {
        int foodBeforeA = bodyA.foodEaten;
        int foodBeforeB = bodyB.foodEaten;
        float newVoiceA = 0.0f, newVoiceB = 0.0f;

        AgentAct(agentA, world, &bodyA, bodyB.x, bodyB.y, voiceB, true, NULL, &newVoiceA);
        AgentAct(agentB, world, &bodyB, bodyA.x, bodyA.y, voiceA, true, NULL, &newVoiceB);
        voiceA = newVoiceA;
        voiceB = newVoiceB;

        windowFoodA += bodyA.foodEaten - foodBeforeA;
        windowFoodB += bodyB.foodEaten - foodBeforeB;
        windowVoiceA += voiceA;
        windowVoiceB += voiceB;

        if (step % AGENT_TEST_WINDOW == 0)
        {
            printf("steps %5d   foodA %3d  foodB %3d   voiceA %.3f  voiceB %.3f\n",
                   step, windowFoodA, windowFoodB,
                   windowVoiceA / AGENT_TEST_WINDOW, windowVoiceB / AGENT_TEST_WINDOW);
            windowFoodA = 0; windowFoodB = 0;
            windowVoiceA = 0.0; windowVoiceB = 0.0;
        }
    }

    free(agentA); free(agentB); free(world);
    return 0;
}

static int runLearnTest(void)
{
    CatAgent *learner = malloc(sizeof(CatAgent));
    CatAgent *control = malloc(sizeof(CatAgent));
    World *learnerWorld = malloc(sizeof(World));
    World *controlWorld = malloc(sizeof(World));
    if (!learner || !control || !learnerWorld || !controlWorld)
    {
        fprintf(stderr, "alloc failed\n");
        free(learner); free(control); free(learnerWorld); free(controlWorld);
        return 1;
    }

    AgentInit(learner, 4242u);
    AgentInit(control, 4242u);
    WorldInitOpen(learnerWorld, 777u);
    WorldInitOpen(controlWorld, 777u);

    CatBody learnerBody, controlBody;
    CatBodyInit(&learnerBody, WORLD_WIDTH / 2, WORLD_HEIGHT / 2);
    CatBodyInit(&controlBody, WORLD_WIDTH / 2, WORLD_HEIGHT / 2);

    int windowLearner = 0, windowControl = 0;

    printf("open field, single cat, learner vs frozen control (same seed)\n");
    for (int step = 1; step <= LEARN_TEST_STEPS; step++)
    {
        int learnerBefore = learnerBody.foodEaten;
        int controlBefore = controlBody.foodEaten;
        AgentAct(learner, learnerWorld, &learnerBody, -1, -1, 0.0f, true, NULL, NULL);
        AgentAct(control, controlWorld, &controlBody, -1, -1, 0.0f, false, NULL, NULL);
        windowLearner += learnerBody.foodEaten - learnerBefore;
        windowControl += controlBody.foodEaten - controlBefore;

        if (step % LEARN_TEST_WINDOW == 0)
        {
            printf("steps %6d   learner %3d   control %3d\n", step, windowLearner, windowControl);
            windowLearner = 0;
            windowControl = 0;
        }
    }

    free(learner); free(control); free(learnerWorld); free(controlWorld);
    return 0;
}

static const char *emotionName(CatEmotion emotion)
{
    switch (emotion)
    {
        case EMOTION_HAPPY: return "happy";
        case EMOTION_CURIOUS: return "curious";
        case EMOTION_SCARED: return "scared";
        case EMOTION_HUNGRY: return "hungry";
        default: return "content";
    }
}

static int runExport(void)
{
    SetTraceLogLevel(LOG_WARNING);
    for (int i = 0; i < EXPORT_COUNT; i++)
    {
        CatGenome genome = CatGenomeRandom(nextSeed());
        for (int emotion = 0; emotion < EMOTION_COUNT; emotion++)
        {
            Image image = CatRenderImage(genome, (CatEmotion)emotion);
            ImageResizeNN(&image, CAT_CANVAS_SIZE * CAT_EXPORT_SCALE, CAT_CANVAS_SIZE * CAT_EXPORT_SCALE);
            char path[128];
            snprintf(path, sizeof(path), "%s/cat_%d_%s.png", EXPORT_DIR, i, emotionName((CatEmotion)emotion));
            ExportImage(image, path);
            UnloadImage(image);
        }
    }
    return 0;
}

static const char *actionName(CatAction action)
{
    switch (action)
    {
        case ACTION_UP: return "up";
        case ACTION_DOWN: return "down";
        case ACTION_LEFT: return "left";
        case ACTION_RIGHT: return "right";
        default: return "stay";
    }
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

static bool foodInSight(const World *world, const CatBody *body, int otherX, int otherY)
{
    float vision[64];
    WorldVisionFor(world, body, otherX, otherY, vision);
    int size = WorldVisionSize();
    for (int i = 0; i < size; i++)
        if (vision[i] > 0.5f) return true;
    return false;
}

static CatEmotion deriveEmotion(const CatAgent *agent, const World *world,
                                const CatBody *body, int otherX, int otherY)
{
    if (agent->lastReward >= REWARD_FOOD * 0.5f) return EMOTION_HAPPY;
    if (agent->lastReward <= REWARD_OBSTACLE * 0.5f) return EMOTION_SCARED;
    if (body->hunger > 0.7f) return EMOTION_HUNGRY;
    if (foodInSight(world, body, otherX, otherY)) return EMOTION_CURIOUS;
    return EMOTION_CONTENT;
}

static void viewUpdate(CatView *view, const CatBody *body)
{
    if (body->x < view->x - 0.05f) view->faceLeft = true;
    else if (body->x > view->x + 0.05f) view->faceLeft = false;
    view->x += (body->x - view->x) * MOVE_LERP;
    view->y += (body->y - view->y) * MOVE_LERP;
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

static void drawCat(const PixelCat *cat, const CatView *view, CatEmotion emotion, float voice, double time)
{
    float drawSize = CAT_CANVAS_SIZE * CAT_DRAW_SCALE;
    float centerX = GRID_ORIGIN_X + view->x * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
    float centerY = GRID_ORIGIN_Y + view->y * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
    float bob = sinf((float)time * BOB_SPEED + view->x * 1.7f) * BOB_AMP;

    DrawEllipse((int)centerX, (int)(centerY + 8.0f), 9.0f, 3.5f, SHADOW_COLOR);

    Vector2 position = { centerX - drawSize * 0.5f, centerY - drawSize * 0.5f - 3.0f + bob };
    PixelCatDraw(cat, position, CAT_DRAW_SCALE, emotion, view->faceLeft);

    if (voice > MEOW_THRESHOLD) drawMeow(centerX, centerY - drawSize * 0.5f);
}

static void drawVoiceBar(int x, int y, int width, float level, Color tint)
{
    DrawRectangle(x, y, width, 10, (Color){ 40, 40, 52, 255 });
    DrawRectangle(x, y, (int)(width * level), 10, tint);
}

static int drawCatCard(int panelX, int y, const char *label, const PixelCat *cat,
                       const CatBody *body, float voice, CatEmotion emotion)
{
    DrawRectangleRounded((Rectangle){ panelX, y, 300, 74 }, 0.12f, 6, (Color){ 46, 40, 46, 255 });
    PixelCatDraw(cat, (Vector2){ panelX + 8, y + 9 }, 3.5f, emotion, false);

    int textX = panelX + 72;
    char line[96];
    snprintf(line, sizeof(line), "cat %s", label);
    DrawText(line, textX, y + 8, 20, RAYWHITE);
    snprintf(line, sizeof(line), "%s   fish %d", emotionName(emotion), body->foodEaten);
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

static void renderScene(const CatAgent *agentA, const CatBody *bodyA, const CatView *viewA, const PixelCat *catA, float voiceA,
                        const CatAgent *agentB, const CatBody *bodyB, const CatView *viewB, const PixelCat *catB, float voiceB,
                        const World *world, bool showBrain, double time)
{
    int panelX = GRID_ORIGIN_X + WORLD_WIDTH * WORLD_TILE_PX + 28;

    CatEmotion emotionA = deriveEmotion(agentA, world, bodyA, bodyB->x, bodyB->y);
    CatEmotion emotionB = deriveEmotion(agentB, world, bodyB, bodyA->x, bodyA->y);

    BeginDrawing();
    ClearBackground(BACKGROUND_COLOR);
    drawWorld(world);
    drawCat(catA, viewA, emotionA, voiceA, time);
    drawCat(catB, viewB, emotionB, voiceB, time);

    int y = GRID_ORIGIN_Y;
    DrawText(WINDOW_TITLE, panelX, y, 30, RAYWHITE); y += 38;
    DrawText("two cats, two spiking brains", panelX, y, 15, GRAY); y += 22;
    DrawText(showBrain ? "b: hide brain    r: new cats    esc: quit"
                       : "b: peek inside the brain    r: new cats    esc: quit",
             panelX, y, 15, GRAY); y += 28;

    if (showBrain)
    {
        drawBrain(&agentA->net, panelX, "cat A brain (blue in / orange out)");
    }
    else
    {
        y = drawCatCard(panelX, y, "A", catA, bodyA, voiceA, emotionA);
        y += 12;
        y = drawCatCard(panelX, y, "B", catB, bodyB, voiceB, emotionB);
    }

    DrawFPS(WINDOW_WIDTH - 90, 12);
    EndDrawing();
}

static int runShot(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(TARGET_FPS);

    CatAgent *agentA = malloc(sizeof(CatAgent));
    CatAgent *agentB = malloc(sizeof(CatAgent));
    World *world = malloc(sizeof(World));
    if (!agentA || !agentB || !world) { CloseWindow(); free(agentA); free(agentB); free(world); return 1; }

    AgentInit(agentA, 4242u);
    AgentInit(agentB, 7777u);
    WorldInit(world, 777u);
    PixelCat catA = PixelCatCreate(CatGenomeRandom(20260615u));
    PixelCat catB = PixelCatCreate(CatGenomeRandom(31415926u));

    CatBody bodyA, bodyB;
    CatBodyInit(&bodyA, CAT_A_START_X, CAT_A_START_Y);
    CatBodyInit(&bodyB, CAT_B_START_X, CAT_B_START_Y);

    float voiceA = 0.0f, voiceB = 0.0f;
    for (int step = 0; step < SHOT_WARMUP_STEPS; step++)
    {
        float nva = 0.0f, nvb = 0.0f;
        AgentAct(agentA, world, &bodyA, bodyB.x, bodyB.y, voiceB, true, NULL, &nva);
        AgentAct(agentB, world, &bodyB, bodyA.x, bodyA.y, voiceA, true, NULL, &nvb);
        voiceA = nva; voiceB = nvb;
    }

    CatView viewA = { (float)bodyA.x, (float)bodyA.y, false };
    CatView viewB = { (float)bodyB.x, (float)bodyB.y, false };

    for (int frame = 0; frame < 8; frame++)
        renderScene(agentA, &bodyA, &viewA, &catA, voiceA,
                    agentB, &bodyB, &viewB, &catB, voiceB, world, false, GetTime());
    TakeScreenshot(SHOT_PATH);

    PixelCatUnload(&catA);
    PixelCatUnload(&catB);
    free(agentA); free(agentB); free(world);
    CloseWindow();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], SNN_TEST_FLAG) == 0) return runSnnTest();
    if (argc > 1 && strcmp(argv[1], AGENT_TEST_FLAG) == 0) return runAgentTest();
    if (argc > 1 && strcmp(argv[1], LEARN_TEST_FLAG) == 0) return runLearnTest();
    if (argc > 1 && strcmp(argv[1], SHOT_FLAG) == 0) return runShot();
    if (argc > 1 && strcmp(argv[1], EXPORT_FLAG) == 0)
    {
        InitWindow(1, 1, EXPORT_FLAG);
        int code = runExport();
        CloseWindow();
        return code;
    }

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(TARGET_FPS);
    SetRandomSeed((unsigned int)(GetTime() * 1000.0) + 1u);

    CatAgent *agentA = malloc(sizeof(CatAgent));
    CatAgent *agentB = malloc(sizeof(CatAgent));
    World *world = malloc(sizeof(World));
    if (!agentA || !agentB || !world) { CloseWindow(); return 1; }

    uint32_t seed = nextSeed();
    AgentInit(agentA, seed + CAT_A_SEED_OFFSET);
    AgentInit(agentB, seed + CAT_B_SEED_OFFSET);
    WorldInit(world, nextSeed());

    PixelCat catA = PixelCatCreate(CatGenomeRandom(nextSeed()));
    PixelCat catB = PixelCatCreate(CatGenomeRandom(nextSeed()));

    CatBody bodyA, bodyB;
    CatBodyInit(&bodyA, CAT_A_START_X, CAT_A_START_Y);
    CatBodyInit(&bodyB, CAT_B_START_X, CAT_B_START_Y);

    float voiceA = 0.0f, voiceB = 0.0f;
    int frame = 0;
    bool showBrain = false;

    CatView viewA = { (float)bodyA.x, (float)bodyA.y, false };
    CatView viewB = { (float)bodyB.x, (float)bodyB.y, false };

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_R))
        {
            AgentInit(agentA, nextSeed());
            AgentInit(agentB, nextSeed());
            WorldInit(world, nextSeed());
            CatBodyInit(&bodyA, CAT_A_START_X, CAT_A_START_Y);
            CatBodyInit(&bodyB, CAT_B_START_X, CAT_B_START_Y);
            viewA = (CatView){ (float)bodyA.x, (float)bodyA.y, false };
            viewB = (CatView){ (float)bodyB.x, (float)bodyB.y, false };
            voiceA = 0.0f; voiceB = 0.0f;
        }
        if (IsKeyPressed(KEY_B)) showBrain = !showBrain;

        if (++frame >= SIM_FRAME_INTERVAL)
        {
            frame = 0;
            float nva = 0.0f, nvb = 0.0f;
            AgentAct(agentA, world, &bodyA, bodyB.x, bodyB.y, voiceB, true, NULL, &nva);
            AgentAct(agentB, world, &bodyB, bodyA.x, bodyA.y, voiceA, true, NULL, &nvb);
            voiceA = nva; voiceB = nvb;
        }

        viewUpdate(&viewA, &bodyA);
        viewUpdate(&viewB, &bodyB);

        renderScene(agentA, &bodyA, &viewA, &catA, voiceA,
                    agentB, &bodyB, &viewB, &catB, voiceB, world, showBrain, GetTime());
    }

    PixelCatUnload(&catA);
    PixelCatUnload(&catB);
    free(agentA); free(agentB); free(world);
    CloseWindow();
    return 0;
}
