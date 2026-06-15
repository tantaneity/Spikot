#ifndef SPIKOT_AGENT_AGENT_H
#define SPIKOT_AGENT_AGENT_H

#include "snn/network.h"
#include "env/world.h"
#include <stdint.h>

typedef struct {
    Network net;
    float lastReward;
    int actionSpikes[ACTION_COUNT];
    CatAction lastAction;
    float lastVoice;
    float rewardBaseline;
    uint32_t rng;
} CatAgent;

typedef struct {
    float soft;
    float hard;
    float wet;
    float novelty;
} CatSenses;

void AgentInit(CatAgent *agent, uint32_t seed);
CatAction AgentAct(CatAgent *agent, World *world, CatBody *body,
                   int otherX, int otherY, float heard, CatSenses senses, bool learn,
                   float *outReward, float *outVoice);
void AgentRest(CatAgent *agent);

#endif
