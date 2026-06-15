#include "agent/agent.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>

#define ACTION_BASE (SNN_NEURON_COUNT - SNN_OUTPUT_NEURONS)
#define VOICE_BASE (SNN_NEURON_COUNT - BRAIN_VOICE_NEURONS)
#define VOICE_MAX_SPIKES (BRAIN_VOICE_NEURONS * BRAIN_SUBSTEPS)

static float clampf(float value, float lo, float hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

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

static float driveStrength(DriveKind drive)
{
    switch (drive)
    {
        case DRIVE_CURIOSITY: return CURIOSITY_STRENGTH;
        case DRIVE_FATIGUE: return FATIGUE_STRENGTH;
        case DRIVE_SCRATCH: return SCRATCH_STRENGTH;
        case DRIVE_BLADDER: return BLADDER_STRENGTH;
        case DRIVE_SOCIAL: return SOCIAL_STRENGTH;
        default: return INSTINCT_STRENGTH;
    }
}

static ItemType driveTargetType(DriveKind drive)
{
    switch (drive)
    {
        case DRIVE_FATIGUE: return ITEM_BED;
        case DRIVE_SCRATCH: return ITEM_POST;
        case DRIVE_BLADDER: return ITEM_LITTER;
        default: return ITEM_BOWL;
    }
}

static int driveSpatialIndex(DriveKind drive)
{
    switch (drive)
    {
        case DRIVE_HUNGER: return 0;
        case DRIVE_FATIGUE: return 1;
        case DRIVE_SCRATCH: return 2;
        case DRIVE_BLADDER: return 3;
        default: return -1;
    }
}

static bool seeItemDir(const RoomItem *items, int count, ItemType type,
                       int bx, int by, int *outDx, int *outDy)
{
    int best = WORLD_VISION_RADIUS + 1, bdx = 0, bdy = 0;
    bool found = false;
    for (int i = 0; i < count; i++)
    {
        if (items[i].type != type) continue;
        int adx = abs(items[i].x - bx), ady = abs(items[i].y - by);
        if (adx > WORLD_VISION_RADIUS || ady > WORLD_VISION_RADIUS) continue;
        int d = adx + ady;
        if (d < best) { best = d; bdx = items[i].x - bx; bdy = items[i].y - by; found = true; }
    }
    *outDx = bdx;
    *outDy = bdy;
    return found;
}

static bool memoryGradient(const CatAgent *agent, const World *world, const CatBody *body,
                           int driveIndex, int *outDx, int *outDy)
{
    static const int dirs[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
    float best = SpatialValue(&agent->spatial, driveIndex, body->x, body->y);
    int bdx = 0, bdy = 0;
    bool found = false;

    for (int k = 0; k < 4; k++)
    {
        int nx = body->x + dirs[k][0], ny = body->y + dirs[k][1];
        if (nx < 0 || nx >= WORLD_WIDTH || ny < 0 || ny >= WORLD_HEIGHT) continue;
        if (world->tiles[ny][nx] == TILE_OBSTACLE) continue;
        float v = SpatialValue(&agent->spatial, driveIndex, nx, ny);
        if (v > best + 1e-4f) { best = v; bdx = dirs[k][0]; bdy = dirs[k][1]; found = true; }
    }

    if (found && best > SPATIAL_VALUE_THRESHOLD) { *outDx = bdx; *outDy = bdy; return true; }
    return false;
}

static bool arrivedAt(const World *world, const CatBody *body, int dx, int dy)
{
    int manhattan = abs(dx) + abs(dy);
    if (manhattan == 0) return true;
    if (manhattan != 1) return false;
    int tx = body->x + dx, ty = body->y + dy;
    if (tx < 0 || tx >= WORLD_WIDTH || ty < 0 || ty >= WORLD_HEIGHT) return false;
    return world->tiles[ty][tx] == TILE_OBSTACLE;
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

static DriveKind dominantDrive(const CatBody *body, CatSenses senses, float hungerDrive, float cortisol, float *outUrgency)
{
    DriveKind best = DRIVE_NONE;
    float bestU = 0.0f;

    if (hungerDrive > bestU) { best = DRIVE_HUNGER; bestU = hungerDrive; }

    if (senses.novelty > DRIVE_CURIOSITY_GATE && (senses.dx != 0 || senses.dy != 0) && senses.novelty > bestU)
    { best = DRIVE_CURIOSITY; bestU = senses.novelty; }

    float fatigueU = (body->fatigue - DRIVE_FATIGUE_GATE) / (1.0f - DRIVE_FATIGUE_GATE);
    if (fatigueU > bestU) { best = DRIVE_FATIGUE; bestU = fatigueU; }

    float scratchU = (body->scratchUrge - DRIVE_SCRATCH_GATE) / (1.0f - DRIVE_SCRATCH_GATE);
    if (scratchU > bestU) { best = DRIVE_SCRATCH; bestU = scratchU; }

    float bladderU = (body->bladder - DRIVE_BLADDER_GATE) / (1.0f - DRIVE_BLADDER_GATE);
    if (bladderU > bestU) { best = DRIVE_BLADDER; bestU = bladderU; }

    float comfort = 1.0f - cortisol;
    float playU = comfort * 0.85f * (body->boredom - DRIVE_PLAY_GATE) / (1.0f - DRIVE_PLAY_GATE);
    if (playU > bestU) { best = DRIVE_PLAY; bestU = playU; }

    float groomU = comfort * 0.85f * (body->grime - DRIVE_GROOM_GATE) / (1.0f - DRIVE_GROOM_GATE);
    if (groomU > bestU) { best = DRIVE_GROOM; bestU = groomU; }

    float socialU = comfort * (body->social - DRIVE_SOCIAL_GATE) / (1.0f - DRIVE_SOCIAL_GATE);
    if (socialU > bestU) { best = DRIVE_SOCIAL; bestU = socialU; }

    *outUrgency = bestU;
    return best;
}

static void updateWander(CatAgent *agent, const CatBody *body)
{
    if (abs(agent->wanderX - body->x) + abs(agent->wanderY - body->y) <= WANDER_REPICK_DIST)
    {
        agent->wanderX = 1 + (int)(nextRandom(&agent->rng) % (uint32_t)(WORLD_WIDTH - 2));
        agent->wanderY = 1 + (int)(nextRandom(&agent->rng) % (uint32_t)(WORLD_HEIGHT - 2));
    }
}

static void encode(const World *world, const CatBody *body, int otherX, int otherY,
                   float heard, int foodDx, int foodDy, float scentStrength,
                   CatSenses senses, float *external)
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

    int senseBase = scentBase + BRAIN_SCENT_NEURONS;
    external[senseBase + 0] = senses.soft * BRAIN_INPUT_DRIVE;
    external[senseBase + 1] = senses.hard * BRAIN_INPUT_DRIVE;
    external[senseBase + 2] = senses.wet * BRAIN_INPUT_DRIVE;
    external[senseBase + 3] = senses.novelty * BRAIN_INPUT_DRIVE;
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

static void updateNeuromods(CatAgent *agent, const CatBody *body, CatSenses senses,
                            float reward, bool rewarded, float anticipation)
{
    Neuromods *m = &agent->mods;
    float rpe = reward - agent->rewardBaseline;

    float needPressure = body->hunger;
    if (body->fatigue > needPressure) needPressure = body->fatigue;
    if (body->scratchUrge > needPressure) needPressure = body->scratchUrge;
    float threat = reward < 0.0f ? -reward : 0.0f;
    float novelty = senses.novelty;

    m->dopamine += DA_GAIN * (rpe > 0.0f ? rpe : 0.0f) - DA_DECAY * m->dopamine;
    m->daPhasic = anticipation;
    m->serotonin += SERO_RATE * ((1.0f - needPressure) - m->serotonin) + SERO_PULSE * (rewarded ? 1.0f : 0.0f);

    float arousalDrive = needPressure * 0.6f;
    if (novelty > arousalDrive) arousalDrive = novelty;
    if (threat > arousalDrive) arousalDrive = threat;
    float circadian = agent->circadian * CIRCADIAN_AROUSAL;
    if (circadian > arousalDrive) arousalDrive = circadian;
    m->noradrenaline += NE_RATE * (arousalDrive - m->noradrenaline);
    m->acetylcholine += ACH_RATE * ((0.4f + 0.6f * novelty + 0.3f * m->dopamine) - m->acetylcholine);

    m->dopamine = clampf(m->dopamine, 0.0f, 1.0f);
    m->serotonin = clampf(m->serotonin, 0.0f, 1.0f);
    m->noradrenaline = clampf(m->noradrenaline, 0.0f, 1.0f);
    m->acetylcholine = clampf(m->acetylcholine, 0.0f, 1.0f);

    m->cortisol += CORT_RATE * (m->noradrenaline - m->cortisol);
    m->valence = clampf(m->serotonin - 0.7f * needPressure + 0.3f * m->dopamine - 0.5f * threat, -1.0f, 1.0f);
    m->arousal = clampf(0.7f * m->noradrenaline + 0.3f * m->dopamine, 0.0f, 1.0f);
}

static CatAction sampleAction(const int *counts, uint32_t *rng, float exploreBase)
{
    float weights[ACTION_COUNT];
    float total = 0.0f;
    for (int action = 0; action < ACTION_COUNT; action++)
    {
        weights[action] = (float)counts[action] + exploreBase;
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
    agent->activeDrive = DRIVE_NONE;
    agent->exploring = false;
    agent->circadian = 0.0f;
    agent->rng = seed ^ 0x5BD1E995u;
    if (agent->rng == 0u) agent->rng = 1u;
    memset(agent->actionSpikes, 0, sizeof(agent->actionSpikes));
    SpatialInit(&agent->spatial);
    AgentResetMods(agent);
    agent->traceHead = 0;
    agent->traceCount = 0;
    agent->wanderX = WORLD_WIDTH / 2;
    agent->wanderY = WORLD_HEIGHT / 2;
}

CatAction AgentAct(CatAgent *agent, World *world, CatBody *body,
                   int otherX, int otherY, int playerX, int playerY, float heard, CatSenses senses,
                   const RoomItem *items, int itemCount, bool learn,
                   float *outReward, float *outVoice)
{
    int foodDx = 0, foodDy = 0;
    bool smelled = nearestFood(world, body->x, body->y, &foodDx, &foodDy);
    float hungerDrive = (body->hunger - INSTINCT_HUNGER_GATE) / (1.0f - INSTINCT_HUNGER_GATE);
    if (hungerDrive < 0.0f) hungerDrive = 0.0f;
    bool inSmellRange = smelled && (items == NULL || abs(foodDx) + abs(foodDy) <= SMELL_RADIUS);
    float scent = inSmellRange ? hungerDrive : 0.0f;

    float inputGain = 1.0f, exploreBase = BRAIN_EXPLORE_BASE, plasticity = 1.0f;
    if (items)
    {
        inputGain = 1.0f + NE_INPUT_GAIN * agent->mods.arousal;
        exploreBase = BRAIN_EXPLORE_BASE *
            clampf(0.5f + 0.8f * agent->mods.noradrenaline - 0.5f * agent->mods.serotonin, 0.25f, 2.0f);
        plasticity = clampf(0.4f + 0.7f * agent->mods.dopamine + 0.4f * agent->mods.acetylcholine, 0.4f, 1.6f);
    }

    float external[SNN_NEURON_COUNT];
    encode(world, body, otherX, otherY, heard, foodDx, foodDy, scent, senses, external);
    if (items)
        for (int i = 0; i < SNN_NEURON_COUNT; i++) external[i] *= inputGain;

    memset(agent->actionSpikes, 0, sizeof(agent->actionSpikes));
    int voiceCount = 0;
    for (int substep = 0; substep < BRAIN_SUBSTEPS; substep++)
    {
        NetworkStep(&agent->net, external);
        accumulateOutputSpikes(&agent->net, agent->actionSpikes, &voiceCount);
    }

    float urgency;
    DriveKind drive = dominantDrive(body, senses, hungerDrive, agent->mods.cortisol, &urgency);
    agent->activeDrive = drive;
    agent->exploring = false;

    int spatialIndex = driveSpatialIndex(drive);
    int tDx = 0, tDy = 0;
    bool haveTarget = false;
    if (drive == DRIVE_CURIOSITY) { tDx = senses.dx; tDy = senses.dy; haveTarget = (tDx != 0 || tDy != 0); }
    else if (drive == DRIVE_SOCIAL && playerX >= 0)
    {
        tDx = playerX - body->x; tDy = playerY - body->y;
        haveTarget = (tDx != 0 || tDy != 0);
    }
    else if (drive == DRIVE_HUNGER && items == NULL)
    {
        if (smelled) { tDx = foodDx; tDy = foodDy; haveTarget = true; }
    }
    else if (spatialIndex >= 0 && items)
    {
        if (seeItemDir(items, itemCount, driveTargetType(drive), body->x, body->y, &tDx, &tDy)) haveTarget = true;
        else haveTarget = memoryGradient(agent, world, body, spatialIndex, &tDx, &tDy);
    }

    if (drive == DRIVE_PLAY)
    {
        static const int zoomDirs[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
        int pick = (int)(nextRandom(&agent->rng) % 4u);
        agent->actionSpikes[towardAction(zoomDirs[pick][0], zoomDirs[pick][1])] += (int)PLAY_STRENGTH;
        agent->mods.noradrenaline = clampf(agent->mods.noradrenaline + PLAY_AROUSAL, 0.0f, 1.0f);
    }
    else if (drive == DRIVE_GROOM)
    {
        agent->actionSpikes[ACTION_STAY] += (int)GROOM_STRENGTH;
    }
    else if (drive != DRIVE_NONE)
    {
        if (haveTarget)
        {
            if (!arrivedAt(world, body, tDx, tDy))
                agent->actionSpikes[towardAction(tDx, tDy)] += (int)(driveStrength(drive) * urgency);
        }
        else if (items)
        {
            if (spatialIndex >= 0 && SpatialValue(&agent->spatial, spatialIndex, body->x, body->y) > SPATIAL_VALUE_THRESHOLD)
                SpatialLearn(&agent->spatial, spatialIndex, body->x, body->y, body->x, body->y, 0.0f, true);
            updateWander(agent, body);
            agent->exploring = true;
            agent->actionSpikes[towardAction(agent->wanderX - body->x, agent->wanderY - body->y)] += (int)EXPLORE_STRENGTH;
        }
    }

    int prevX = body->x, prevY = body->y;
    CatAction action = sampleAction(agent->actionSpikes, &agent->rng, exploreBase);
    float reward = WorldStepCat(world, body, action, otherX, otherY);
    bool rewarded = reward >= REWARD_FOOD;

    if (items && spatialIndex >= 0)
    {
        bool ate = (drive == DRIVE_HUNGER && rewarded);
        SpatialLearn(&agent->spatial, spatialIndex, prevX, prevY, body->x, body->y, ate ? 1.0f : 0.0f, ate);
    }

    if (learn)
    {
        float advantage = reward - agent->rewardBaseline;
        agent->rewardBaseline += LEARNING_BASELINE_RATE * (reward - agent->rewardBaseline);

        float weights[ACTION_COUNT];
        float total = 0.0f;
        for (int a = 0; a < ACTION_COUNT; a++)
        {
            weights[a] = (float)agent->actionSpikes[a] + exploreBase;
            total += weights[a];
        }

        float modulation[SNN_NEURON_COUNT];
        memset(modulation, 0, sizeof(modulation));
        for (int a = 0; a < ACTION_COUNT; a++)
        {
            float chosen = (a == (int)action) ? 1.0f : 0.0f;
            float credit = advantage * (chosen - weights[a] / total) * plasticity;
            int base = ACTION_BASE + a * BRAIN_OUTPUT_GROUP;
            for (int n = 0; n < BRAIN_OUTPUT_GROUP; n++) modulation[base + n] = credit;
        }
        NetworkApplyReadoutReward(&agent->net, modulation);
    }

    float anticipation = 0.0f;
    if (items && spatialIndex >= 0)
    {
        float vGain = SpatialValue(&agent->spatial, spatialIndex, body->x, body->y)
                    - SpatialValue(&agent->spatial, spatialIndex, prevX, prevY);
        if (vGain > 0.0f) anticipation = vGain * DA_ANTICIPATION;
    }
    if (items) updateNeuromods(agent, body, senses, reward, rewarded, anticipation);

    if (items)
    {
        agent->trace[agent->traceHead] = (Experience){ body->x, body->y, spatialIndex, reward, rewarded };
        agent->traceHead = (agent->traceHead + 1) % REPLAY_BUFFER;
        if (agent->traceCount < REPLAY_BUFFER) agent->traceCount++;
    }

    agent->lastReward = reward;
    agent->lastAction = action;
    agent->lastVoice = (float)voiceCount / (float)VOICE_MAX_SPIKES;

    if (outReward) *outReward = reward;
    if (outVoice) *outVoice = agent->lastVoice;
    return action;
}

void AgentReinforcePlace(CatAgent *agent, DriveKind drive, int x, int y)
{
    int spatialIndex = driveSpatialIndex(drive);
    if (spatialIndex >= 0)
        SpatialLearn(&agent->spatial, spatialIndex, x, y, x, y, 1.0f, true);
}

void AgentCarried(CatAgent *agent)
{
    /* ponytail: held cat feels but doesn't act - sustained tension, no SNN step */
    Neuromods *m = &agent->mods;
    m->noradrenaline = clampf(m->noradrenaline + CARRY_AROUSAL, 0.0f, 1.0f);
    m->daPhasic = 0.0f;
    m->valence = clampf(m->serotonin - CARRY_DISCOMFORT + 0.3f * m->dopamine, -1.0f, 1.0f);
    m->arousal = clampf(0.7f * m->noradrenaline + 0.3f * m->dopamine, 0.0f, 1.0f);
}

void AgentResetMods(CatAgent *agent)
{
    agent->mods = (Neuromods){ 0.0f, 0.0f, 0.5f, 0.2f, 0.4f, 0.2f, 0.2f };
}

void AgentNeuromodPulse(CatAgent *agent, float dopamine, float serotonin, float noradrenaline)
{
    agent->mods.dopamine = clampf(agent->mods.dopamine + dopamine, 0.0f, 1.0f);
    agent->mods.serotonin = clampf(agent->mods.serotonin + serotonin, 0.0f, 1.0f);
    agent->mods.noradrenaline = clampf(agent->mods.noradrenaline + noradrenaline, 0.0f, 1.0f);
}

static const Experience *recentExperience(const CatAgent *agent, int stepsBack)
{
    int index = (agent->traceHead - 1 - stepsBack + 2 * REPLAY_BUFFER) % REPLAY_BUFFER;
    return &agent->trace[index];
}

static void replayTrace(CatAgent *agent)
{
    for (int sweep = 0; sweep < REPLAY_SWEEPS_PER_REST; sweep++)
        for (int j = 0; j + 1 < agent->traceCount; j++)
        {
            const Experience *cur = recentExperience(agent, j);
            const Experience *prev = recentExperience(agent, j + 1);
            if (cur->drive < 0) continue;
            if (abs(cur->x - prev->x) + abs(cur->y - prev->y) != 1) continue;
            SpatialLearn(&agent->spatial, cur->drive, prev->x, prev->y,
                         cur->x, cur->y, cur->reward, cur->satisfied);
        }
}

void AgentRest(CatAgent *agent)
{
    for (int substep = 0; substep < BRAIN_SUBSTEPS; substep++)
        NetworkStep(&agent->net, NULL);

    NetworkApplyReward(&agent->net, SLEEP_CONSOLIDATE);
    replayTrace(agent);

    Neuromods *m = &agent->mods;
    m->noradrenaline -= NE_SLEEP_DECAY * m->noradrenaline;
    m->acetylcholine -= ACH_SLEEP_DECAY * m->acetylcholine;
    m->serotonin += SERO_RATE * (1.0f - m->serotonin);
    m->valence = clampf(m->serotonin + 0.3f * m->dopamine, -1.0f, 1.0f);
    m->arousal = clampf(0.7f * m->noradrenaline, 0.0f, 1.0f);

    agent->lastReward = 0.0f;
    agent->lastVoice = 0.0f;
}
