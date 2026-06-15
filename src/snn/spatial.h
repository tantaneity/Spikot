#ifndef SPIKOT_SNN_SPATIAL_H
#define SPIKOT_SNN_SPATIAL_H

#include "config.h"
#include <stdbool.h>

#define PLACE_CELLS (WORLD_WIDTH * WORLD_HEIGHT)
#define SPATIAL_DRIVE_COUNT 4

typedef struct {
    float weight[SPATIAL_DRIVE_COUNT][PLACE_CELLS];
} SpatialMemory;

void SpatialInit(SpatialMemory *memory);
float SpatialValue(const SpatialMemory *memory, int drive, int x, int y);
void SpatialLearn(SpatialMemory *memory, int drive, int prevX, int prevY,
                  int curX, int curY, float reward, bool satisfied);

#endif
