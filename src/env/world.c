#include "env/world.h"
#include <stdbool.h>

#define DEFAULT_SEED 0x9E3779B9u
#define VISION_DIAMETER (2 * WORLD_VISION_RADIUS + 1)

static uint32_t nextRandom(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static int randomIndex(uint32_t *state, int bound)
{
    return (int)(nextRandom(state) % (uint32_t)bound);
}

static void spawnOnEmpty(World *world, TileType type)
{
    for (;;)
    {
        int x = randomIndex(&world->rng, WORLD_WIDTH);
        int y = randomIndex(&world->rng, WORLD_HEIGHT);
        bool isCat = (x == world->catX && y == world->catY);
        if (!isCat && world->tiles[y][x] == TILE_EMPTY)
        {
            world->tiles[y][x] = type;
            return;
        }
    }
}

void WorldInit(World *world, uint32_t seed)
{
    if (seed == 0u) seed = DEFAULT_SEED;
    world->rng = seed;
    world->catX = WORLD_WIDTH / 2;
    world->catY = WORLD_HEIGHT / 2;
    world->hunger = 0.0f;
    world->foodEaten = 0;

    for (int y = 0; y < WORLD_HEIGHT; y++)
        for (int x = 0; x < WORLD_WIDTH; x++)
            world->tiles[y][x] = TILE_EMPTY;

    for (int i = 0; i < WORLD_OBSTACLE_COUNT; i++) spawnOnEmpty(world, TILE_OBSTACLE);
    for (int i = 0; i < WORLD_FOOD_COUNT; i++) spawnOnEmpty(world, TILE_FOOD);
}

static void actionDelta(CatAction action, int *dx, int *dy)
{
    *dx = 0;
    *dy = 0;
    switch (action)
    {
        case ACTION_UP: *dy = -1; break;
        case ACTION_DOWN: *dy = 1; break;
        case ACTION_LEFT: *dx = -1; break;
        case ACTION_RIGHT: *dx = 1; break;
        default: break;
    }
}

static bool inBounds(int x, int y)
{
    return x >= 0 && x < WORLD_WIDTH && y >= 0 && y < WORLD_HEIGHT;
}

float WorldStep(World *world, CatAction action)
{
    world->hunger += HUNGER_RATE;
    if (world->hunger > 1.0f) world->hunger = 1.0f;

    int dx, dy;
    actionDelta(action, &dx, &dy);
    int targetX = world->catX + dx;
    int targetY = world->catY + dy;

    if (action == ACTION_STAY) return REWARD_STEP;

    if (!inBounds(targetX, targetY) || world->tiles[targetY][targetX] == TILE_OBSTACLE)
        return REWARD_OBSTACLE;

    world->catX = targetX;
    world->catY = targetY;

    if (world->tiles[targetY][targetX] == TILE_FOOD)
    {
        world->tiles[targetY][targetX] = TILE_EMPTY;
        world->foodEaten++;
        world->hunger -= HUNGER_FOOD_RELIEF;
        if (world->hunger < 0.0f) world->hunger = 0.0f;
        spawnOnEmpty(world, TILE_FOOD);
        return REWARD_FOOD;
    }

    return REWARD_STEP;
}

int WorldVisionSize(void)
{
    return VISION_DIAMETER * VISION_DIAMETER;
}

void WorldVision(const World *world, float *out)
{
    int index = 0;
    for (int dy = -WORLD_VISION_RADIUS; dy <= WORLD_VISION_RADIUS; dy++)
    {
        for (int dx = -WORLD_VISION_RADIUS; dx <= WORLD_VISION_RADIUS; dx++)
        {
            int x = world->catX + dx;
            int y = world->catY + dy;
            if (!inBounds(x, y) || world->tiles[y][x] == TILE_OBSTACLE)
                out[index] = -1.0f;
            else if (world->tiles[y][x] == TILE_FOOD)
                out[index] = 1.0f;
            else
                out[index] = 0.0f;
            index++;
        }
    }
}
