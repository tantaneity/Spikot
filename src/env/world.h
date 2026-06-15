#ifndef SPIKOT_ENV_WORLD_H
#define SPIKOT_ENV_WORLD_H

#include "config.h"
#include <stdint.h>

typedef enum {
    TILE_EMPTY,
    TILE_FOOD,
    TILE_OBSTACLE
} TileType;

typedef enum {
    ACTION_STAY,
    ACTION_UP,
    ACTION_DOWN,
    ACTION_LEFT,
    ACTION_RIGHT,
    ACTION_COUNT
} CatAction;

typedef struct {
    TileType tiles[WORLD_HEIGHT][WORLD_WIDTH];
    int catX;
    int catY;
    float hunger;
    int foodEaten;
    uint32_t rng;
} World;

void WorldInit(World *world, uint32_t seed);
float WorldStep(World *world, CatAction action);

int WorldVisionSize(void);
void WorldVision(const World *world, float *out);

#endif
