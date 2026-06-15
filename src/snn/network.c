#include "snn/network.h"
#include "snn/neuron.h"
#include <string.h>

#define N SNN_NEURON_COUNT
#define DEFAULT_SEED 0x9E3779B9u
#define OUTPUT_BEGIN (SNN_NEURON_COUNT - SNN_OUTPUT_NEURONS)

typedef enum { LAYER_INPUT, LAYER_HIDDEN, LAYER_OUTPUT } Layer;

static Layer layerOf(int index)
{
    if (index < SNN_INPUT_NEURONS) return LAYER_INPUT;
    if (index >= OUTPUT_BEGIN) return LAYER_OUTPUT;
    return LAYER_HIDDEN;
}

static float connectionProb(int pre, int post)
{
    Layer from = layerOf(pre);
    Layer to = layerOf(post);

    if (from == LAYER_INPUT)
    {
        if (to == LAYER_HIDDEN) return SNN_CONN_IN_HID;
        if (to == LAYER_OUTPUT) return SNN_CONN_IN_OUT;
        return 0.0f;
    }
    if (from == LAYER_HIDDEN)
    {
        if (to == LAYER_HIDDEN) return SNN_CONN_HID_HID;
        if (to == LAYER_OUTPUT) return SNN_CONN_HID_OUT;
        return 0.0f;
    }
    if (to == LAYER_HIDDEN) return SNN_CONN_OUT_HID;
    return 0.0f;
}

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
    memset(network->refractory, 0, sizeof(network->refractory));
    memset(network->current, 0, sizeof(network->current));
    memset(network->eligibility, 0, sizeof(network->eligibility));

    for (int neuron = 0; neuron < N; neuron++)
        network->inhibitory[neuron] =
            (layerOf(neuron) == LAYER_HIDDEN) && (randomUnit(&state) < SNN_INHIBITORY_FRACTION);

    for (int pre = 0; pre < N; pre++)
    {
        bool inhibitsOutput = network->inhibitory[pre];
        for (int post = 0; post < N; post++)
        {
            bool blocked = inhibitsOutput && layerOf(post) == LAYER_OUTPUT;
            if (!blocked && pre != post && randomUnit(&state) < connectionProb(pre, post))
                network->weights[pre][post] = randomUnit(&state) * SNN_WEIGHT_INIT_MAX;
            else
                network->weights[pre][post] = 0.0f;
        }
    }

    for (int post = 0; post < N; post++)
    {
        float incoming = 0.0f;
        for (int pre = 0; pre < N; pre++) incoming += network->weights[pre][post];
        network->homeostasisTarget[post] = incoming;
    }

    int edge = 0;
    for (int pre = 0; pre < N; pre++)
    {
        network->rowStart[pre] = edge;
        for (int post = 0; post < N; post++)
            if (network->weights[pre][post] != 0.0f && edge < SNN_MAX_EDGES)
                network->edgePost[edge++] = post;
    }
    network->rowStart[N] = edge;
}

static void normalizeIncoming(Network *network)
{
    float incoming[N];
    for (int post = 0; post < N; post++) incoming[post] = 0.0f;

    for (int pre = 0; pre < N; pre++)
        for (int e = network->rowStart[pre]; e < network->rowStart[pre + 1]; e++)
            incoming[network->edgePost[e]] += network->weights[pre][network->edgePost[e]];

    for (int pre = 0; pre < N; pre++)
    {
        for (int e = network->rowStart[pre]; e < network->rowStart[pre + 1]; e++)
        {
            int post = network->edgePost[e];
            float target = network->homeostasisTarget[post];
            if (target <= 0.0f || incoming[post] <= 0.0f) continue;

            float weight = network->weights[pre][post] * (target / incoming[post]);
            if (weight > SNN_WEIGHT_MAX) weight = SNN_WEIGHT_MAX;
            network->weights[pre][post] = weight;
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
        float sign = network->inhibitory[pre] ? -SNN_INHIBITORY_GAIN : 1.0f;
        const float *outgoing = network->weights[pre];
        for (int e = network->rowStart[pre]; e < network->rowStart[pre + 1]; e++)
        {
            int post = network->edgePost[e];
            network->current[post] += outgoing[post] * sign;
        }
    }
}

static void accumulateEligibility(Network *network, const bool *fired)
{
    for (int pre = 0; pre < N; pre++)
    {
        float preTrace = network->trace[pre];
        float preFired = fired[pre] ? 1.0f : 0.0f;
        for (int e = network->rowStart[pre]; e < network->rowStart[pre + 1]; e++)
        {
            int post = network->edgePost[e];
            float potentiation = SNN_STDP_POTENTIATION * preTrace * (fired[post] ? 1.0f : 0.0f);
            float depression = SNN_STDP_DEPRESSION * network->trace[post] * preFired;
            network->eligibility[pre][post] =
                network->eligibility[pre][post] * SNN_ELIGIBILITY_DECAY + (potentiation - depression);
        }
    }
}

void NetworkApplyReward(Network *network, float reward)
{
    if (reward == 0.0f) return;

    for (int pre = 0; pre < N; pre++)
    {
        for (int e = network->rowStart[pre]; e < network->rowStart[pre + 1]; e++)
        {
            int post = network->edgePost[e];
            float weight = network->weights[pre][post] +
                           SNN_LEARNING_RATE * reward * network->eligibility[pre][post];
            if (weight < 0.0f) weight = 0.0f;
            else if (weight > SNN_WEIGHT_MAX) weight = SNN_WEIGHT_MAX;
            network->weights[pre][post] = weight;
        }
    }

    normalizeIncoming(network);
}

void NetworkApplyReadoutReward(Network *network, const float *modulation)
{
    for (int pre = 0; pre < N; pre++)
    {
        for (int e = network->rowStart[pre]; e < network->rowStart[pre + 1]; e++)
        {
            int post = network->edgePost[e];
            float credit = modulation[post];
            if (credit == 0.0f) continue;

            float weight = network->weights[pre][post] +
                           SNN_READOUT_RATE * credit * network->eligibility[pre][post];
            if (weight < 0.0f) weight = 0.0f;
            else if (weight > SNN_WEIGHT_MAX) weight = SNN_WEIGHT_MAX;
            network->weights[pre][post] = weight;
        }
    }

    normalizeIncoming(network);
}

void NetworkStep(Network *network, const float *externalInput)
{
    accumulateCurrent(network, externalInput);

    bool fired[N];
    for (int neuron = 0; neuron < N; neuron++)
    {
        if (network->refractory[neuron] > 0)
        {
            network->refractory[neuron]--;
            network->potential[neuron] = SNN_V_RESET;
            fired[neuron] = false;
            continue;
        }
        fired[neuron] = NeuronIntegrate(&network->potential[neuron], network->current[neuron]);
        if (fired[neuron]) network->refractory[neuron] = SNN_REFRACTORY_TICKS;
    }

    for (int neuron = 0; neuron < N; neuron++)
        network->trace[neuron] = network->trace[neuron] * SNN_TRACE_DECAY +
                                 (fired[neuron] ? 1.0f : 0.0f);

    accumulateEligibility(network, fired);

    memcpy(network->spiked, fired, sizeof(fired));
}

int NetworkSpikeCount(const Network *network)
{
    int count = 0;
    for (int neuron = 0; neuron < N; neuron++)
        if (network->spiked[neuron]) count++;
    return count;
}
