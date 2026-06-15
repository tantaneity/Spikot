#include "app/diagnostics.h"
#include "config.h"
#include "snn/network.h"
#include "env/world.h"
#include "agent/agent.h"
#include "cat/pixel_cat.h"
#include "raylib.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#define SNN_TEST_TICKS 800
#define SNN_TEST_REPORT_EVERY 100
#define SNN_TEST_INPUT_NEURONS 24
#define SNN_TEST_INPUT_PROB 0.6f
#define SNN_TEST_INPUT_DRIVE 0.85f

#define AGENT_TEST_STEPS 8000
#define AGENT_TEST_WINDOW 1000

#define LEARN_TEST_STEPS 30000
#define LEARN_TEST_WINDOW 2000

#define EXPORT_COUNT 2
#define EXPORT_DIR "export"

static uint32_t xorshiftSeed(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

int RunSnnTest(void)
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

int RunAgentTest(void)
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

        CatSenses none = { 0 };
        AgentAct(agentA, world, &bodyA, bodyB.x, bodyB.y, -1, -1, voiceB, none, NULL, 0, true, NULL, &newVoiceA);
        AgentAct(agentB, world, &bodyB, bodyA.x, bodyA.y, -1, -1, voiceA, none, NULL, 0, true, NULL, &newVoiceB);
        voiceA = newVoiceA;
        voiceB = newVoiceB;

        if (bodyA.foodEaten > foodBeforeA) WorldSpawnFood(world);
        if (bodyB.foodEaten > foodBeforeB) WorldSpawnFood(world);

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

int RunLearnTest(void)
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
        CatSenses none = { 0 };
        AgentAct(learner, learnerWorld, &learnerBody, -1, -1, -1, -1, 0.0f, none, NULL, 0, true, NULL, NULL);
        AgentAct(control, controlWorld, &controlBody, -1, -1, -1, -1, 0.0f, none, NULL, 0, false, NULL, NULL);
        if (learnerBody.foodEaten > learnerBefore) WorldSpawnFood(learnerWorld);
        if (controlBody.foodEaten > controlBefore) WorldSpawnFood(controlWorld);
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

#define SPATIAL_TEST_EPISODES 80
#define SPATIAL_TEST_MAX_STEPS 600

int RunSpatialTest(void)
{
    CatAgent *agent = malloc(sizeof(CatAgent));
    World *world = malloc(sizeof(World));
    if (!agent || !world)
    {
        fprintf(stderr, "alloc failed\n");
        free(agent); free(world);
        return 1;
    }

    AgentInit(agent, 4242u);
    WorldInitRoom(world, 777u);
    RoomItem bowl = { ITEM_BOWL, 22, 16, true, 0.0f };
    int startX = 6, startY = 6;

    printf("place-cell spatial memory: steps to reach the bowl, fixed start far from a fixed bowl\n");
    printf("if the neural map learns, average steps should fall across episodes\n");

    int blockSteps = 0, blockReached = 0;
    for (int ep = 1; ep <= SPATIAL_TEST_EPISODES; ep++)
    {
        CatBody body;
        CatBodyInit(&body, startX, startY);
        int steps = 0;
        bool reached = false;
        for (; steps < SPATIAL_TEST_MAX_STEPS; steps++)
        {
            body.hunger = 1.0f;
            world->tiles[bowl.y][bowl.x] = TILE_FOOD;
            int before = body.foodEaten;
            AgentAct(agent, world, &body, -1, -1, -1, -1, 0.0f, (CatSenses){ 0 }, &bowl, 1, true, NULL, NULL);
            if (body.foodEaten > before) { steps++; reached = true; break; }
        }
        blockSteps += steps;
        blockReached += reached ? 1 : 0;
        if (ep % 10 == 0)
        {
            printf("episodes %2d-%2d   avg steps %6.1f   reached %2d/10\n",
                   ep - 9, ep, (double)blockSteps / 10.0, blockReached);
            blockSteps = 0;
            blockReached = 0;
        }
    }

    free(agent); free(world);
    return 0;
}

#define SLEEP_TEST_WARMUP 8
#define SLEEP_TEST_MAX_STEPS 600
#define SLEEP_TEST_REST_TICKS 300

int RunSleepTest(void)
{
    CatAgent *agent = malloc(sizeof(CatAgent));
    World *world = malloc(sizeof(World));
    if (!agent || !world)
    {
        fprintf(stderr, "alloc failed\n");
        free(agent); free(world);
        return 1;
    }

    AgentInit(agent, 4242u);
    WorldInitRoom(world, 777u);
    RoomItem bowl = { ITEM_BOWL, 22, 16, true, 0.0f };
    int startX = 6, startY = 6;

    for (int ep = 1; ep <= SLEEP_TEST_WARMUP; ep++)
    {
        CatBody body;
        CatBodyInit(&body, startX, startY);
        for (int steps = 0; steps < SLEEP_TEST_MAX_STEPS; steps++)
        {
            body.hunger = 1.0f;
            world->tiles[bowl.y][bowl.x] = TILE_FOOD;
            int before = body.foodEaten;
            AgentAct(agent, world, &body, -1, -1, -1, -1, 0.0f, (CatSenses){ 0 }, &bowl, 1, true, NULL, NULL);
            if (body.foodEaten > before) break;
        }
    }

    int probes[4][2] = { { startX, startY }, { 12, 9 }, { 17, 13 }, { 20, 15 } };
    float before[4];
    for (int i = 0; i < 4; i++)
        before[i] = SpatialValue(&agent->spatial, 0, probes[i][0], probes[i][1]);

    printf("hunger-value of the place map before vs after a sleep of pure replay (no new steps)\n");
    for (int t = 0; t < SLEEP_TEST_REST_TICKS; t++) AgentRest(agent);

    for (int i = 0; i < 4; i++)
    {
        float after = SpatialValue(&agent->spatial, 0, probes[i][0], probes[i][1]);
        printf("(%2d,%2d) dist %2d   before %.4f   after %.4f   %s\n",
               probes[i][0], probes[i][1], abs(probes[i][0] - bowl.x) + abs(probes[i][1] - bowl.y),
               before[i], after, after > before[i] + 1e-4f ? "consolidated up" : "--");
    }

    free(agent); free(world);
    return 0;
}

int RunExport(void)
{
    InitWindow(1, 1, "spikot export");
    SetTraceLogLevel(LOG_WARNING);

    for (int i = 0; i < EXPORT_COUNT; i++)
    {
        CatGenome genome = CatGenomeRandom((uint32_t)GetRandomValue(1, 2000000000));
        for (int emotion = 0; emotion < EMOTION_COUNT; emotion++)
        {
            Image image = CatRenderImage(genome, (CatEmotion)emotion);
            ImageResizeNN(&image, CAT_CANVAS_SIZE * CAT_EXPORT_SCALE, CAT_CANVAS_SIZE * CAT_EXPORT_SCALE);
            char path[128];
            snprintf(path, sizeof(path), "%s/cat_%d_%s.png", EXPORT_DIR, i, CatEmotionName((CatEmotion)emotion));
            ExportImage(image, path);
            UnloadImage(image);
        }
    }

    CloseWindow();
    return 0;
}
