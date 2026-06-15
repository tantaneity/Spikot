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
    uint32_t rng;
} CatAgent;

void AgentInit(CatAgent *agent, uint32_t seed);
CatAction AgentAct(CatAgent *agent, World *world, float *outReward);

#endif
