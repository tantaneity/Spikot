#include "agent/agent.h"
#include "config.h"
#include <string.h>

#define OUTPUT_BASE (SNN_NEURON_COUNT - ACTION_COUNT * BRAIN_OUTPUT_GROUP)

static uint32_t nextRandom(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static float randomUnit(uint32_t *state)
{
    return (nextRandom(state) >> 8) * (1.0f / 16777216.0f);
}

static void encodeWorld(const World *world, float *external)
{
    for (int i = 0; i < SNN_NEURON_COUNT; i++) external[i] = 0.0f;

    int visionSize = WorldVisionSize();
    float vision[WORLD_VISION_RADIUS * 2 + 1][WORLD_VISION_RADIUS * 2 + 1];
    WorldVision(world, &vision[0][0]);

    const float *cells = &vision[0][0];
    int obstacleBase = visionSize;
    int hungerBase = visionSize * 2;

    for (int i = 0; i < visionSize; i++)
    {
        if (cells[i] > 0.0f) external[i] = BRAIN_INPUT_DRIVE;
        else if (cells[i] < 0.0f) external[obstacleBase + i] = BRAIN_INPUT_DRIVE;
    }

    for (int i = 0; i < BRAIN_HUNGER_NEURONS; i++)
        external[hungerBase + i] = world->hunger * BRAIN_INPUT_DRIVE;
}

static void accumulateOutputSpikes(const Network *net, int *counts)
{
    for (int action = 0; action < ACTION_COUNT; action++)
    {
        int base = OUTPUT_BASE + action * BRAIN_OUTPUT_GROUP;
        for (int n = 0; n < BRAIN_OUTPUT_GROUP; n++)
            if (net->spiked[base + n]) counts[action]++;
    }
}

static CatAction sampleAction(const int *counts, uint32_t *rng)
{
    float weights[ACTION_COUNT];
    float total = 0.0f;
    for (int action = 0; action < ACTION_COUNT; action++)
    {
        weights[action] = (float)counts[action] + BRAIN_EXPLORE_BASE;
        total += weights[action];
    }

    float pick = randomUnit(rng) * total;
    for (int action = 0; action < ACTION_COUNT; action++)
    {
        pick -= weights[action];
        if (pick <= 0.0f) return (CatAction)action;
    }
    return ACTION_STAY;
}

void AgentInit(CatAgent *agent, uint32_t seed)
{
    NetworkInit(&agent->net, seed);
    agent->lastReward = REWARD_STEP;
    agent->lastAction = ACTION_STAY;
    agent->rng = seed ^ 0x5BD1E995u;
    if (agent->rng == 0u) agent->rng = 1u;
    memset(agent->actionSpikes, 0, sizeof(agent->actionSpikes));
}

CatAction AgentAct(CatAgent *agent, World *world, float *outReward)
{
    float external[SNN_NEURON_COUNT];
    encodeWorld(world, external);

    memset(agent->actionSpikes, 0, sizeof(agent->actionSpikes));
    for (int substep = 0; substep < BRAIN_SUBSTEPS; substep++)
    {
        NetworkStep(&agent->net, external);
        accumulateOutputSpikes(&agent->net, agent->actionSpikes);
    }

    CatAction action = sampleAction(agent->actionSpikes, &agent->rng);
    float reward = WorldStep(world, action);
    NetworkApplyReward(&agent->net, reward);

    agent->lastReward = reward;
    agent->lastAction = action;
    if (outReward) *outReward = reward;
    return action;
}
