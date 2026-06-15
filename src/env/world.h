#ifndef SPIKOT_ENV_WORLD_H
#define SPIKOT_ENV_WORLD_H

#include "config.h"
#include <stdint.h>
#include <stdbool.h>

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
    int x;
    int y;
    float hunger;
    float fatigue;
    float scratchUrge;
    int foodEaten;
} CatBody;

typedef enum {
    ITEM_BOWL,
    ITEM_BED,
    ITEM_PLANT,
    ITEM_POST,
    ITEM_RUG,
    ITEM_TYPE_COUNT
} ItemType;

typedef struct {
    ItemType type;
    int x;
    int y;
    bool hasFood;
    float refill;
} RoomItem;

typedef struct {
    TileType tiles[WORLD_HEIGHT][WORLD_WIDTH];
    uint32_t rng;
} World;

void WorldInit(World *world, uint32_t seed);
void WorldInitOpen(World *world, uint32_t seed);
void WorldInitRoom(World *world, uint32_t seed);
void WorldClearInterior(World *world);
void WorldSpawnFood(World *world);
void CatBodyInit(CatBody *cat, int x, int y);
float WorldStepCat(World *world, CatBody *cat, CatAction action, int blockX, int blockY);

int WorldVisionSize(void);
void WorldVisionFor(const World *world, const CatBody *cat, int otherX, int otherY, float *out);

#endif
