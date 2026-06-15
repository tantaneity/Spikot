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
#define EXPORT_COUNT 8
#define EXPORT_DIR "export"

#define SNN_TEST_FLAG "--snn-test"
#define SNN_TEST_TICKS 800
#define SNN_TEST_REPORT_EVERY 100
#define SNN_TEST_INPUT_NEURONS 24
#define SNN_TEST_INPUT_PROB 0.6f
#define SNN_TEST_INPUT_DRIVE 1.4f

#define AGENT_TEST_FLAG "--agent-test"
#define AGENT_TEST_STEPS 6000
#define AGENT_TEST_WINDOW 1000

#define GRID_ORIGIN_X 48
#define GRID_ORIGIN_Y 48
#define SIM_FRAME_INTERVAL 6

static const Color TILE_EMPTY_COLOR = (Color){ 30, 30, 42, 255 };
static const Color TILE_FOOD_COLOR = (Color){ 96, 204, 124, 255 };
static const Color TILE_OBSTACLE_COLOR = (Color){ 66, 66, 84, 255 };

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

static double weightSum(const Network *network)
{
    double total = 0.0;
    for (int pre = 0; pre < SNN_NEURON_COUNT; pre++)
        for (int post = 0; post < SNN_NEURON_COUNT; post++)
            total += network->weights[pre][post];
    return total;
}

static int runSnnTest(void)
{
    Network *network = malloc(sizeof(Network));
    if (!network) { fprintf(stderr, "alloc failed\n"); return 1; }

    NetworkInit(network, 12345u);
    double before = weightSum(network);

    float external[SNN_NEURON_COUNT];
    uint32_t state = 999u;
    long totalSpikes = 0;

    for (int tick = 0; tick < SNN_TEST_TICKS; tick++)
    {
        for (int j = 0; j < SNN_NEURON_COUNT; j++) external[j] = 0.0f;
        for (int j = 0; j < SNN_TEST_INPUT_NEURONS; j++)
            if ((xorshiftSeed(&state) >> 8) * (1.0f / 16777216.0f) < SNN_TEST_INPUT_PROB)
                external[j] = SNN_TEST_INPUT_DRIVE;

        NetworkStep(network, external, 1.0f);
        int spikes = NetworkSpikeCount(network);
        totalSpikes += spikes;
        if (tick % SNN_TEST_REPORT_EVERY == 0) printf("tick %4d   spikes %3d\n", tick, spikes);
    }

    double after = weightSum(network);
    printf("---\nneurons %d  ticks %d\n", SNN_NEURON_COUNT, SNN_TEST_TICKS);
    printf("avg spikes/tick %.2f\n", (double)totalSpikes / SNN_TEST_TICKS);
    printf("weight sum %.4f -> %.4f  (delta %+.4f)\n", before, after, after - before);

    free(network);
    return 0;
}

static int runAgentTest(void)
{
    CatAgent *agent = malloc(sizeof(CatAgent));
    World *world = malloc(sizeof(World));
    if (!agent || !world) { fprintf(stderr, "alloc failed\n"); free(agent); free(world); return 1; }

    AgentInit(agent, 4242u);
    WorldInit(world, 777u);

    int windowFood = 0;
    double windowReward = 0.0;

    for (int step = 1; step <= AGENT_TEST_STEPS; step++)
    {
        int foodBefore = world->foodEaten;
        float reward = 0.0f;
        AgentAct(agent, world, &reward);
        windowReward += reward;
        windowFood += (world->foodEaten - foodBefore);

        if (step % AGENT_TEST_WINDOW == 0)
        {
            printf("steps %5d   food eaten %3d   avg reward %+.4f\n",
                   step, windowFood, windowReward / AGENT_TEST_WINDOW);
            windowFood = 0;
            windowReward = 0.0;
        }
    }

    free(agent);
    free(world);
    return 0;
}

static int runExport(void)
{
    SetTraceLogLevel(LOG_WARNING);
    for (int i = 0; i < EXPORT_COUNT; i++)
    {
        CatGenome genome = CatGenomeRandom(nextSeed());
        Image image = CatRenderImage(genome);
        ImageResizeNN(&image, CAT_CANVAS_SIZE * CAT_EXPORT_SCALE, CAT_CANVAS_SIZE * CAT_EXPORT_SCALE);
        char path[128];
        snprintf(path, sizeof(path), "%s/cat_%d_%u.png", EXPORT_DIR, i, genome.seed);
        ExportImage(image, path);
        UnloadImage(image);
    }
    return 0;
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

static void drawCat(const PixelCat *cat, const World *world)
{
    Vector2 position = {
        (float)(GRID_ORIGIN_X + world->catX * WORLD_TILE_PX),
        (float)(GRID_ORIGIN_Y + world->catY * WORLD_TILE_PX)
    };
    float scale = (float)WORLD_TILE_PX / (float)CAT_CANVAS_SIZE;
    PixelCatDraw(cat, position, scale);
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

static void drawHud(const CatAgent *agent, const World *world, float reward)
{
    int panelX = GRID_ORIGIN_X + WORLD_WIDTH * WORLD_TILE_PX + 32;
    int y = GRID_ORIGIN_Y;

    DrawText(WINDOW_TITLE, panelX, y, 32, RAYWHITE); y += 44;
    DrawText("r: reset   space: new look   esc: quit", panelX, y, 16, GRAY); y += 32;

    char line[96];
    snprintf(line, sizeof(line), "food eaten   %d", world->foodEaten);
    DrawText(line, panelX, y, 20, LIGHTGRAY); y += 26;
    snprintf(line, sizeof(line), "hunger       %.2f", world->hunger);
    DrawText(line, panelX, y, 20, LIGHTGRAY); y += 26;
    snprintf(line, sizeof(line), "action       %s", actionName(agent->lastAction));
    DrawText(line, panelX, y, 20, LIGHTGRAY); y += 26;
    snprintf(line, sizeof(line), "reward       %+.2f", reward);
    DrawText(line, panelX, y, 20, LIGHTGRAY); y += 26;
    snprintf(line, sizeof(line), "spikes       %d", NetworkSpikeCount(&agent->net));
    DrawText(line, panelX, y, 20, LIGHTGRAY); y += 34;

    DrawText("action votes", panelX, y, 16, GRAY); y += 22;
    for (int action = 0; action < ACTION_COUNT; action++)
    {
        snprintf(line, sizeof(line), "%-6s %d", actionName((CatAction)action), agent->actionSpikes[action]);
        DrawText(line, panelX, y, 18, agent->lastAction == action ? GREEN : LIGHTGRAY);
        y += 22;
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], SNN_TEST_FLAG) == 0) return runSnnTest();
    if (argc > 1 && strcmp(argv[1], AGENT_TEST_FLAG) == 0) return runAgentTest();
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

    CatAgent *agent = malloc(sizeof(CatAgent));
    World *world = malloc(sizeof(World));
    if (!agent || !world) { CloseWindow(); return 1; }

    AgentInit(agent, nextSeed());
    WorldInit(world, nextSeed());
    PixelCat cat = PixelCatCreate(CatGenomeRandom(nextSeed()));

    int frame = 0;
    float lastReward = 0.0f;

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_R))
        {
            AgentInit(agent, nextSeed());
            WorldInit(world, nextSeed());
        }
        if (IsKeyPressed(KEY_SPACE))
        {
            PixelCatUnload(&cat);
            cat = PixelCatCreate(CatGenomeRandom(nextSeed()));
        }

        if (++frame >= SIM_FRAME_INTERVAL)
        {
            frame = 0;
            AgentAct(agent, world, &lastReward);
        }

        BeginDrawing();
        ClearBackground(BACKGROUND_COLOR);
        drawWorld(world);
        drawCat(&cat, world);
        drawHud(agent, world, lastReward);
        DrawFPS(WINDOW_WIDTH - 96, 16);
        EndDrawing();
    }

    PixelCatUnload(&cat);
    free(agent);
    free(world);
    CloseWindow();
    return 0;
}
