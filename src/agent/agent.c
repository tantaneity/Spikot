#include "agent/agent.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>

#define ACTION_BASE (SNN_NEURON_COUNT - SNN_OUTPUT_NEURONS)
#define VOICE_BASE (SNN_NEURON_COUNT - BRAIN_VOICE_NEURONS)
#define VOICE_MAX_SPIKES (BRAIN_VOICE_NEURONS * BRAIN_SUBSTEPS)

static bool nearestFood(const World *world, int bx, int by, int *outDx, int *outDy)
{
    int best = 1 << 30, bdx = 0, bdy = 0;
    bool found = false;
    for (int y = 0; y < WORLD_HEIGHT; y++)
        for (int x = 0; x < WORLD_WIDTH; x++)
            if (world->tiles[y][x] == TILE_FOOD)
            {
                int d = abs(x - bx) + abs(y - by);
                if (d < best) { best = d; bdx = x - bx; bdy = y - by; found = true; }
            }
    *outDx = bdx;
    *outDy = bdy;
    return found;
}

static CatAction towardAction(int dx, int dy)
{
    if (dx == 0 && dy == 0) return ACTION_STAY;
    if (abs(dx) >= abs(dy)) return dx > 0 ? ACTION_RIGHT : ACTION_LEFT;
    return dy > 0 ? ACTION_DOWN : ACTION_UP;
}

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

static void encode(const World *world, const CatBody *body, int otherX, int otherY,
                   float heard, int foodDx, int foodDy, float scentStrength, float *external)
{
    for (int i = 0; i < SNN_NEURON_COUNT; i++) external[i] = 0.0f;

    int visionSize = WorldVisionSize();
    float vision[WORLD_VISION_RADIUS * 2 + 1][WORLD_VISION_RADIUS * 2 + 1];
    WorldVisionFor(world, body, otherX, otherY, &vision[0][0]);
    const float *cells = &vision[0][0];

    int obstacleBase = visionSize;
    int hungerBase = visionSize * 2;
    int heardBase = hungerBase + BRAIN_HUNGER_NEURONS;
    int scentBase = heardBase + BRAIN_HEARD_NEURONS;

    for (int i = 0; i < visionSize; i++)
    {
        if (cells[i] > 0.0f) external[i] = BRAIN_INPUT_DRIVE;
        else if (cells[i] < 0.0f) external[obstacleBase + i] = BRAIN_INPUT_DRIVE;
    }

    for (int i = 0; i < BRAIN_HUNGER_NEURONS; i++)
        external[hungerBase + i] = body->hunger * BRAIN_INPUT_DRIVE;

    for (int i = 0; i < BRAIN_HEARD_NEURONS; i++)
        external[heardBase + i] = heard * BRAIN_INPUT_DRIVE;

    if (scentStrength > 0.0f && (foodDx != 0 || foodDy != 0))
    {
        float dist = (float)(abs(foodDx) + abs(foodDy));
        float s = scentStrength * BRAIN_INPUT_DRIVE;
        external[scentBase + 0] = (foodDy < 0 ? -foodDy : 0) / dist * s;
        external[scentBase + 1] = (foodDy > 0 ? foodDy : 0) / dist * s;
        external[scentBase + 2] = (foodDx < 0 ? -foodDx : 0) / dist * s;
        external[scentBase + 3] = (foodDx > 0 ? foodDx : 0) / dist * s;
    }
}

static void accumulateOutputSpikes(const Network *net, int *actionCounts, int *voiceCount)
{
    for (int action = 0; action < ACTION_COUNT; action++)
    {
        int base = ACTION_BASE + action * BRAIN_OUTPUT_GROUP;
        for (int n = 0; n < BRAIN_OUTPUT_GROUP; n++)
            if (net->spiked[base + n]) actionCounts[action]++;
    }
    for (int n = 0; n < BRAIN_VOICE_NEURONS; n++)
        if (net->spiked[VOICE_BASE + n]) (*voiceCount)++;
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
    agent->lastVoice = 0.0f;
    agent->rewardBaseline = 0.0f;
    agent->rng = seed ^ 0x5BD1E995u;
    if (agent->rng == 0u) agent->rng = 1u;
    memset(agent->actionSpikes, 0, sizeof(agent->actionSpikes));
}

CatAction AgentAct(CatAgent *agent, World *world, CatBody *body,
                   int otherX, int otherY, float heard, bool learn,
                   float *outReward, float *outVoice)
{
    int foodDx = 0, foodDy = 0;
    bool smelled = nearestFood(world, body->x, body->y, &foodDx, &foodDy);
    float drive = (body->hunger - INSTINCT_HUNGER_GATE) / (1.0f - INSTINCT_HUNGER_GATE);
    if (drive < 0.0f) drive = 0.0f;
    float scent = smelled ? drive : 0.0f;

    float external[SNN_NEURON_COUNT];
    encode(world, body, otherX, otherY, heard, foodDx, foodDy, scent, external);

    memset(agent->actionSpikes, 0, sizeof(agent->actionSpikes));
    int voiceCount = 0;
    for (int substep = 0; substep < BRAIN_SUBSTEPS; substep++)
    {
        NetworkStep(&agent->net, external);
        accumulateOutputSpikes(&agent->net, agent->actionSpikes, &voiceCount);
    }

    if (smelled && drive > 0.0f)
        agent->actionSpikes[towardAction(foodDx, foodDy)] += (int)(INSTINCT_STRENGTH * drive);

    CatAction action = sampleAction(agent->actionSpikes, &agent->rng);
    float reward = WorldStepCat(world, body, action, otherX, otherY);

    if (learn)
    {
        float advantage = reward - agent->rewardBaseline;
        agent->rewardBaseline += LEARNING_BASELINE_RATE * (reward - agent->rewardBaseline);

        float weights[ACTION_COUNT];
        float total = 0.0f;
        for (int a = 0; a < ACTION_COUNT; a++)
        {
            weights[a] = (float)agent->actionSpikes[a] + BRAIN_EXPLORE_BASE;
            total += weights[a];
        }

        float modulation[SNN_NEURON_COUNT];
        memset(modulation, 0, sizeof(modulation));
        for (int a = 0; a < ACTION_COUNT; a++)
        {
            float chosen = (a == (int)action) ? 1.0f : 0.0f;
            float credit = advantage * (chosen - weights[a] / total);
            int base = ACTION_BASE + a * BRAIN_OUTPUT_GROUP;
            for (int n = 0; n < BRAIN_OUTPUT_GROUP; n++) modulation[base + n] = credit;
        }
        NetworkApplyReadoutReward(&agent->net, modulation);
    }

    agent->lastReward = reward;
    agent->lastAction = action;
    agent->lastVoice = (float)voiceCount / (float)VOICE_MAX_SPIKES;

    if (outReward) *outReward = reward;
    if (outVoice) *outVoice = agent->lastVoice;
    return action;
}

void AgentRest(CatAgent *agent)
{
    for (int substep = 0; substep < BRAIN_SUBSTEPS; substep++)
        NetworkStep(&agent->net, NULL);

    NetworkApplyReward(&agent->net, SLEEP_CONSOLIDATE);
    agent->lastReward = 0.0f;
    agent->lastVoice = 0.0f;
}
