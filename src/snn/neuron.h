#ifndef SPIKOT_SNN_NEURON_H
#define SPIKOT_SNN_NEURON_H

#include "config.h"
#include <stdbool.h>

static inline bool NeuronIntegrate(float *potential, float current)
{
    *potential += SNN_TIME_STEP *
                  (-(*potential - SNN_V_REST) / SNN_TAU_MEMBRANE + current);

    if (*potential > SNN_V_THRESHOLD)
    {
        *potential = SNN_V_RESET;
        return true;
    }
    return false;
}

#endif
