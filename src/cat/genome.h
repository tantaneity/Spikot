#ifndef SPIKOT_CAT_GENOME_H
#define SPIKOT_CAT_GENOME_H

#include "raylib.h"
#include <stdint.h>

typedef struct {
    uint32_t seed;
    float bodySize;
    float earAngle;
    float tailLength;
    float furDensity;
    Color primary;
    Color secondary;
    Color eyeColor;
} CatGenome;

CatGenome CatGenomeRandom(uint32_t seed);

#endif
