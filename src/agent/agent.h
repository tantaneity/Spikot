#ifndef SPIKOT_AGENT_AGENT_H
#define SPIKOT_AGENT_AGENT_H

#include "snn/network.h"
#include "snn/spatial.h"
#include "env/world.h"
#include <stdint.h>

typedef enum {
    DRIVE_NONE,
    DRIVE_HUNGER,
    DRIVE_CURIOSITY,
    DRIVE_FATIGUE,
    DRIVE_SCRATCH
} DriveKind;

typedef struct {
    float dopamine;
    float serotonin;
    float noradrenaline;
    float acetylcholine;
    float valence;
    float arousal;
} Neuromods;

typedef struct {
    Network net;
    SpatialMemory spatial;
    Neuromods mods;
    float lastReward;
    int actionSpikes[ACTION_COUNT];
    CatAction lastAction;
    float lastVoice;
    float rewardBaseline;
    DriveKind activeDrive;
    bool exploring;
    int wanderX;
    int wanderY;
    uint32_t rng;
} CatAgent;

typedef struct {
    float soft;
    float hard;
    float wet;
    float novelty;
    int dx;
    int dy;
} CatSenses;

void AgentInit(CatAgent *agent, uint32_t seed);
CatAction AgentAct(CatAgent *agent, World *world, CatBody *body,
                   int otherX, int otherY, float heard, CatSenses senses,
                   const RoomItem *items, int itemCount, bool learn,
                   float *outReward, float *outVoice);
void AgentReinforcePlace(CatAgent *agent, DriveKind drive, int x, int y);
void AgentNeuromodPulse(CatAgent *agent, float dopamine, float serotonin, float noradrenaline);
void AgentResetMods(CatAgent *agent);
void AgentRest(CatAgent *agent);

#endif
