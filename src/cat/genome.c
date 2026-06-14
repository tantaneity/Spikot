#include "cat/genome.h"

#define DEFAULT_SEED 0x9E3779B9u
#define HUE_MAX 360.0f
#define EYE_HUE_COUNT 3

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

static float randomRange(uint32_t *state, float low, float high)
{
    return low + (high - low) * randomUnit(state);
}

CatGenome CatGenomeRandom(uint32_t seed)
{
    if (seed == 0u) seed = DEFAULT_SEED;

    uint32_t state = seed;
    CatGenome genome;
    genome.seed = seed;

    genome.bodySize = randomRange(&state, 0.7f, 1.0f);
    genome.earAngle = randomRange(&state, 0.0f, 1.0f);
    genome.tailLength = randomRange(&state, 0.45f, 1.0f);
    genome.furDensity = randomRange(&state, 0.0f, 0.85f);

    float baseHue = randomRange(&state, 0.0f, HUE_MAX);
    genome.primary = ColorFromHSV(baseHue,
                                  randomRange(&state, 0.25f, 0.6f),
                                  randomRange(&state, 0.65f, 0.92f));
    genome.secondary = ColorFromHSV(baseHue,
                                    randomRange(&state, 0.35f, 0.75f),
                                    randomRange(&state, 0.28f, 0.5f));

    const float eyeHues[EYE_HUE_COUNT] = { 110.0f, 45.0f, 200.0f };
    genome.eyeColor = ColorFromHSV(eyeHues[xorshift32(&state) % EYE_HUE_COUNT],
                                   0.7f, 0.95f);
    return genome;
}
