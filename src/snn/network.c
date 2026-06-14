#include "snn/network.h"
#include "snn/neuron.h"
#include <string.h>

#define N SNN_NEURON_COUNT
#define DEFAULT_SEED 0x9E3779B9u

static uint32_t xorshift32(uint32_t *state)
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
    return (xorshift32(state) >> 8) * (1.0f / 16777216.0f);
}

void NetworkInit(Network *network, uint32_t seed)
{
    if (seed == 0u) seed = DEFAULT_SEED;
    uint32_t state = seed;

    memset(network->potential, 0, sizeof(network->potential));
    memset(network->trace, 0, sizeof(network->trace));
    memset(network->spiked, 0, sizeof(network->spiked));
    memset(network->current, 0, sizeof(network->current));

    for (int pre = 0; pre < N; pre++)
    {
        for (int post = 0; post < N; post++)
        {
            if (pre != post && randomUnit(&state) < SNN_CONNECTION_PROB)
                network->weights[pre][post] = randomUnit(&state) * SNN_WEIGHT_INIT_MAX;
            else
                network->weights[pre][post] = 0.0f;
        }
    }
}

static void accumulateCurrent(Network *network, const float *externalInput)
{
    for (int post = 0; post < N; post++)
        network->current[post] = externalInput ? externalInput[post] : 0.0f;

    for (int pre = 0; pre < N; pre++)
    {
        if (!network->spiked[pre]) continue;
        const float *outgoing = network->weights[pre];
        for (int post = 0; post < N; post++)
            network->current[post] += outgoing[post];
    }
}

static void applyStdp(Network *network, const bool *fired, float reward)
{
    for (int pre = 0; pre < N; pre++)
    {
        float preTrace = network->trace[pre];
        bool preFired = fired[pre];
        for (int post = 0; post < N; post++)
        {
            float weight = network->weights[pre][post];
            if (weight == 0.0f) continue;

            float potentiation = SNN_STDP_POTENTIATION * preTrace * (fired[post] ? 1.0f : 0.0f);
            float depression = SNN_STDP_DEPRESSION * network->trace[post] * (preFired ? 1.0f : 0.0f);
            weight += (potentiation - depression) * reward;

            if (weight < 0.0f) weight = 0.0f;
            else if (weight > SNN_WEIGHT_MAX) weight = SNN_WEIGHT_MAX;
            network->weights[pre][post] = weight;
        }
    }
}

void NetworkStep(Network *network, const float *externalInput, float reward)
{
    accumulateCurrent(network, externalInput);

    bool fired[N];
    for (int neuron = 0; neuron < N; neuron++)
        fired[neuron] = NeuronIntegrate(&network->potential[neuron], network->current[neuron]);

    for (int neuron = 0; neuron < N; neuron++)
        network->trace[neuron] = network->trace[neuron] * SNN_TRACE_DECAY +
                                 (fired[neuron] ? 1.0f : 0.0f);

    applyStdp(network, fired, reward);

    memcpy(network->spiked, fired, sizeof(fired));
}

int NetworkSpikeCount(const Network *network)
{
    int count = 0;
    for (int neuron = 0; neuron < N; neuron++)
        if (network->spiked[neuron]) count++;
    return count;
}
