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

static const Color TILE_EMPTY_COLOR = (Color){ 30, 30, 42, 255 };
static const Color TILE_FOOD_COLOR = (Color){ 96, 204, 124, 255 };
static const Color TILE_OBSTACLE_COLOR = (Color){ 66, 66, 84, 255 };
static const Color VOICE_COLOR = (Color){ 255, 196, 80, 255 };

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
    CatBodyInit(&bodyA, WORLD_WIDTH / 2 - 3, WORLD_HEIGHT / 2);
    CatBodyInit(&bodyB, WORLD_WIDTH / 2 + 3, WORLD_HEIGHT / 2);

    float voiceA = 0.0f, voiceB = 0.0f;
    int windowFoodA = 0, windowFoodB = 0;
    double windowVoiceA = 0.0, windowVoiceB = 0.0;

    for (int step = 1; step <= AGENT_TEST_STEPS; step++)
    {
        int foodBeforeA = bodyA.foodEaten;
        int foodBeforeB = bodyB.foodEaten;
        float newVoiceA = 0.0f, newVoiceB = 0.0f;

        AgentAct(agentA, world, &bodyA, bodyB.x, bodyB.y, voiceB, NULL, &newVoiceA);
        AgentAct(agentB, world, &bodyB, bodyA.x, bodyA.y, voiceA, NULL, &newVoiceB);
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

static void drawWorld(const World *world)
{
    for (int y = 0; y < WORLD_HEIGHT; y++)
    {
        for (int x = 0; x < WORLD_WIDTH; x++)
        {
            Color color = TILE_EMPTY_COLOR;
            if (world->tiles[y][x] == TILE_FOOD) color = TILE_FOOD_COLOR;
            else if (world->tiles[y][x] == TILE_OBSTACLE) color = TILE_OBSTACLE_COLOR;

            int px = GRID_ORIGIN_X + x * WORLD_TILE_PX;
            int py = GRID_ORIGIN_Y + y * WORLD_TILE_PX;
            DrawRectangle(px, py, WORLD_TILE_PX - 1, WORLD_TILE_PX - 1, color);
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

static void drawCat(const PixelCat *cat, const CatBody *body, CatEmotion emotion)
{
    Vector2 position = {
        (float)(GRID_ORIGIN_X + body->x * WORLD_TILE_PX),
        (float)(GRID_ORIGIN_Y + body->y * WORLD_TILE_PX)
    };
    float scale = (float)WORLD_TILE_PX / (float)CAT_CANVAS_SIZE;
    PixelCatDraw(cat, position, scale, emotion);
}

static void drawVoiceBar(int x, int y, int width, float level, Color tint)
{
    DrawRectangle(x, y, width, 10, (Color){ 40, 40, 52, 255 });
    DrawRectangle(x, y, (int)(width * level), 10, tint);
}

static int drawCatStatus(int panelX, int y, const char *label, Color swatch,
                         const CatAgent *agent, const CatBody *body, float voice)
{
    DrawRectangle(panelX, y + 2, 16, 16, swatch);
    char line[96];
    snprintf(line, sizeof(line), "%s  food %d  hunger %.2f  %s",
             label, body->foodEaten, body->hunger, actionName(agent->lastAction));
    DrawText(line, panelX + 24, y, 18, LIGHTGRAY);
    y += 24;
    DrawText("voice", panelX + 24, y, 14, GRAY);
    drawVoiceBar(panelX + 72, y, 160, voice, VOICE_COLOR);
    return y + 22;
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

static void renderScene(const CatAgent *agentA, const CatBody *bodyA, const PixelCat *catA, float voiceA,
                        const CatAgent *agentB, const CatBody *bodyB, const PixelCat *catB, float voiceB,
                        const World *world)
{
    int panelX = GRID_ORIGIN_X + WORLD_WIDTH * WORLD_TILE_PX + 28;

    CatEmotion emotionA = deriveEmotion(agentA, world, bodyA, bodyB->x, bodyB->y);
    CatEmotion emotionB = deriveEmotion(agentB, world, bodyB, bodyA->x, bodyA->y);

    BeginDrawing();
    ClearBackground(BACKGROUND_COLOR);
    drawWorld(world);
    drawCat(catA, bodyA, emotionA);
    drawCat(catB, bodyB, emotionB);

    int y = GRID_ORIGIN_Y;
    DrawText(WINDOW_TITLE, panelX, y, 30, RAYWHITE); y += 40;
    DrawText("two cats, two brains, talking", panelX, y, 15, GRAY); y += 22;
    DrawText("r: reset   esc: quit", panelX, y, 15, GRAY); y += 26;

    y = drawCatStatus(panelX, y, "A", catA->genome.primary, agentA, bodyA, voiceA);
    y = drawCatStatus(panelX, y, "B", catB->genome.primary, agentB, bodyB, voiceB);

    drawBrain(&agentA->net, panelX, "cat A brain (blue in / orange out)");

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
    CatBodyInit(&bodyA, WORLD_WIDTH / 2 - 3, WORLD_HEIGHT / 2);
    CatBodyInit(&bodyB, WORLD_WIDTH / 2 + 3, WORLD_HEIGHT / 2);

    float voiceA = 0.0f, voiceB = 0.0f;
    for (int step = 0; step < SHOT_WARMUP_STEPS; step++)
    {
        float nva = 0.0f, nvb = 0.0f;
        AgentAct(agentA, world, &bodyA, bodyB.x, bodyB.y, voiceB, NULL, &nva);
        AgentAct(agentB, world, &bodyB, bodyA.x, bodyA.y, voiceA, NULL, &nvb);
        voiceA = nva; voiceB = nvb;
    }

    for (int frame = 0; frame < 8; frame++)
        renderScene(agentA, &bodyA, &catA, voiceA, agentB, &bodyB, &catB, voiceB, world);
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
    CatBodyInit(&bodyA, WORLD_WIDTH / 2 - 3, WORLD_HEIGHT / 2);
    CatBodyInit(&bodyB, WORLD_WIDTH / 2 + 3, WORLD_HEIGHT / 2);

    float voiceA = 0.0f, voiceB = 0.0f;
    int frame = 0;

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_R))
        {
            AgentInit(agentA, nextSeed());
            AgentInit(agentB, nextSeed());
            WorldInit(world, nextSeed());
            CatBodyInit(&bodyA, WORLD_WIDTH / 2 - 3, WORLD_HEIGHT / 2);
            CatBodyInit(&bodyB, WORLD_WIDTH / 2 + 3, WORLD_HEIGHT / 2);
            voiceA = 0.0f; voiceB = 0.0f;
        }

        if (++frame >= SIM_FRAME_INTERVAL)
        {
            frame = 0;
            float nva = 0.0f, nvb = 0.0f;
            AgentAct(agentA, world, &bodyA, bodyB.x, bodyB.y, voiceB, NULL, &nva);
            AgentAct(agentB, world, &bodyB, bodyA.x, bodyA.y, voiceA, NULL, &nvb);
            voiceA = nva; voiceB = nvb;
        }

        renderScene(agentA, &bodyA, &catA, voiceA, agentB, &bodyB, &catB, voiceB, world);
    }

    PixelCatUnload(&catA);
    PixelCatUnload(&catB);
    free(agentA); free(agentB); free(world);
    CloseWindow();
    return 0;
}
