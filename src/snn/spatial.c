#include "snn/spatial.h"
#include <stdlib.h>
#include <string.h>

static float placeActivity(int cell, int x, int y)
{
    int px = cell % WORLD_WIDTH;
    int py = cell / WORLD_WIDTH;
    float distance = (float)(abs(px - x) + abs(py - y));
    float activity = 1.0f - distance / PLACE_SIGMA;
    return activity > 0.0f ? activity : 0.0f;
}

void SpatialInit(SpatialMemory *memory)
{
    memset(memory, 0, sizeof(*memory));
}

float SpatialValue(const SpatialMemory *memory, int drive, int x, int y)
{
    float numerator = 0.0f, denominator = 0.0f;
    for (int cell = 0; cell < PLACE_CELLS; cell++)
    {
        float activity = placeActivity(cell, x, y);
        if (activity > 0.0f)
        {
            numerator += activity * memory->weight[drive][cell];
            denominator += activity;
        }
    }
    return denominator > 0.0f ? numerator / denominator : 0.0f;
}

void SpatialLearn(SpatialMemory *memory, int drive, int prevX, int prevY,
                  int curX, int curY, float reward, bool satisfied)
{
    float bootstrap = satisfied ? 0.0f : SpatialValue(memory, drive, curX, curY);
    float target = reward + SPATIAL_GAMMA * bootstrap;
    float predicted = SpatialValue(memory, drive, prevX, prevY);
    float delta = target - predicted;

    float denominator = 0.0f;
    for (int cell = 0; cell < PLACE_CELLS; cell++)
        denominator += placeActivity(cell, prevX, prevY);
    if (denominator <= 0.0f) return;

    for (int cell = 0; cell < PLACE_CELLS; cell++)
    {
        float activity = placeActivity(cell, prevX, prevY);
        if (activity <= 0.0f) continue;
        float updated = memory->weight[drive][cell] + SPATIAL_LR * delta * (activity / denominator);
        if (updated > SPATIAL_WEIGHT_MAX) updated = SPATIAL_WEIGHT_MAX;
        else if (updated < 0.0f) updated = 0.0f;
        memory->weight[drive][cell] = updated;
    }
}
