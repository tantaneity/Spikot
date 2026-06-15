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
        if (world->tiles[y][x] == TILE_EMPTY)
        {
            world->tiles[y][x] = type;
            return;
        }
    }
}

static void wallVertical(World *world, int x, int y0, int y1, int gapY)
{
    for (int y = y0; y <= y1; y++)
        if (y < gapY || y >= gapY + WORLD_DOORWAY)
            world->tiles[y][x] = TILE_OBSTACLE;
}

static void wallHorizontal(World *world, int y, int x0, int x1, int gapX)
{
    for (int x = x0; x <= x1; x++)
        if (x < gapX || x >= gapX + WORLD_DOORWAY)
            world->tiles[y][x] = TILE_OBSTACLE;
}

static void clearArea(World *world, int cx, int cy, int radius)
{
    for (int y = cy - radius; y <= cy + radius; y++)
        for (int x = cx - radius; x <= cx + radius; x++)
            if (x >= 0 && x < WORLD_WIDTH && y >= 0 && y < WORLD_HEIGHT)
                world->tiles[y][x] = TILE_EMPTY;
}

void WorldInit(World *world, uint32_t seed)
{
    if (seed == 0u) seed = DEFAULT_SEED;
    world->rng = seed;

    for (int y = 0; y < WORLD_HEIGHT; y++)
        for (int x = 0; x < WORLD_WIDTH; x++)
            world->tiles[y][x] = TILE_EMPTY;

    wallHorizontal(world, 0, 0, WORLD_WIDTH - 1, -1);
    wallHorizontal(world, WORLD_HEIGHT - 1, 0, WORLD_WIDTH - 1, -1);
    wallVertical(world, 0, 0, WORLD_HEIGHT - 1, -1);
    wallVertical(world, WORLD_WIDTH - 1, 0, WORLD_HEIGHT - 1, -1);

    wallVertical(world, 11, 1, WORLD_HEIGHT - 2, 16);
    wallVertical(world, 21, 1, WORLD_HEIGHT - 2, 7);
    wallHorizontal(world, 11, 1, WORLD_WIDTH - 2, 6);
    wallHorizontal(world, 22, 1, WORLD_WIDTH - 2, 26);

    clearArea(world, WORLD_WIDTH / 2, WORLD_HEIGHT / 2, 3);

    for (int i = 0; i < WORLD_CLUTTER_COUNT; i++) spawnOnEmpty(world, TILE_OBSTACLE);
    for (int i = 0; i < WORLD_FOOD_COUNT; i++) spawnOnEmpty(world, TILE_FOOD);

    world->tiles[WORLD_HEIGHT / 2][WORLD_WIDTH / 2 - 3] = TILE_EMPTY;
    world->tiles[WORLD_HEIGHT / 2][WORLD_WIDTH / 2 + 3] = TILE_EMPTY;
}

void CatBodyInit(CatBody *cat, int x, int y)
{
    cat->x = x;
    cat->y = y;
    cat->hunger = 0.0f;
    cat->foodEaten = 0;
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

float WorldStepCat(World *world, CatBody *cat, CatAction action, int blockX, int blockY)
{
    cat->hunger += HUNGER_RATE;
    if (cat->hunger > 1.0f) cat->hunger = 1.0f;

    if (action == ACTION_STAY) return REWARD_STEP;

    int dx, dy;
    actionDelta(action, &dx, &dy);
    int targetX = cat->x + dx;
    int targetY = cat->y + dy;

    bool blockedByOther = (targetX == blockX && targetY == blockY);
    if (!inBounds(targetX, targetY) || world->tiles[targetY][targetX] == TILE_OBSTACLE || blockedByOther)
        return REWARD_OBSTACLE;

    cat->x = targetX;
    cat->y = targetY;

    if (world->tiles[targetY][targetX] == TILE_FOOD)
    {
        world->tiles[targetY][targetX] = TILE_EMPTY;
        cat->foodEaten++;
        cat->hunger -= HUNGER_FOOD_RELIEF;
        if (cat->hunger < 0.0f) cat->hunger = 0.0f;
        spawnOnEmpty(world, TILE_FOOD);
        return REWARD_FOOD;
    }

    return REWARD_STEP;
}

int WorldVisionSize(void)
{
    return VISION_DIAMETER * VISION_DIAMETER;
}

void WorldVisionFor(const World *world, const CatBody *cat, int otherX, int otherY, float *out)
{
    int index = 0;
    for (int dy = -WORLD_VISION_RADIUS; dy <= WORLD_VISION_RADIUS; dy++)
    {
        for (int dx = -WORLD_VISION_RADIUS; dx <= WORLD_VISION_RADIUS; dx++)
        {
            int x = cat->x + dx;
            int y = cat->y + dy;
            bool isOther = (x == otherX && y == otherY);
            if (!inBounds(x, y) || world->tiles[y][x] == TILE_OBSTACLE || isOther)
                out[index] = -1.0f;
            else if (world->tiles[y][x] == TILE_FOOD)
                out[index] = 1.0f;
            else
                out[index] = 0.0f;
            index++;
        }
    }
}
