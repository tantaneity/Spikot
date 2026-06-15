#ifndef SPIKOT_SNN_NETWORK_H
#define SPIKOT_SNN_NETWORK_H

#include "config.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float potential[SNN_NEURON_COUNT];
    float trace[SNN_NEURON_COUNT];
    bool spiked[SNN_NEURON_COUNT];
    bool inhibitory[SNN_NEURON_COUNT];
    int refractory[SNN_NEURON_COUNT];
    float current[SNN_NEURON_COUNT];
    float weights[SNN_NEURON_COUNT][SNN_NEURON_COUNT];
    float eligibility[SNN_NEURON_COUNT][SNN_NEURON_COUNT];
    float homeostasisTarget[SNN_NEURON_COUNT];
} Network;

void NetworkInit(Network *network, uint32_t seed);
void NetworkStep(Network *network, const float *externalInput);
void NetworkApplyReward(Network *network, float reward);
int NetworkSpikeCount(const Network *network);

#endif
