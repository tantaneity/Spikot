#include "env/world.h"
#include <stdbool.h>
#include <stdlib.h>

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

    wallVertical(world, WORLD_WIDTH / 2, 1, WORLD_HEIGHT - 2, WORLD_HEIGHT / 2 - 1);
    wallHorizontal(world, WORLD_HEIGHT / 2, 1, WORLD_WIDTH - 2, WORLD_WIDTH / 2 - 1);

    clearArea(world, WORLD_WIDTH / 2, WORLD_HEIGHT / 2, 2);

    for (int i = 0; i < WORLD_CLUTTER_COUNT; i++) spawnOnEmpty(world, TILE_OBSTACLE);
    for (int i = 0; i < WORLD_FOOD_COUNT; i++) spawnOnEmpty(world, TILE_FOOD);

    clearArea(world, CAT_A_START_X, CAT_A_START_Y, 1);
    clearArea(world, CAT_B_START_X, CAT_B_START_Y, 1);
}

void WorldInitRoom(World *world, uint32_t seed)
{
    if (seed == 0u) seed = DEFAULT_SEED;
    world->rng = seed;

    for (int y = 0; y < WORLD_HEIGHT; y++)
        for (int x = 0; x < WORLD_WIDTH; x++)
        {
            bool border = (x == 0 || y == 0 || x == WORLD_WIDTH - 1 || y == WORLD_HEIGHT - 1);
            world->tiles[y][x] = border ? TILE_OBSTACLE : TILE_EMPTY;
        }
}

void WorldClearInterior(World *world)
{
    for (int y = 1; y < WORLD_HEIGHT - 1; y++)
        for (int x = 1; x < WORLD_WIDTH - 1; x++)
            world->tiles[y][x] = TILE_EMPTY;
}

void WorldSpawnFood(World *world)
{
    spawnOnEmpty(world, TILE_FOOD);
}

void WorldInitOpen(World *world, uint32_t seed)
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

    for (int i = 0; i < WORLD_FOOD_COUNT; i++) spawnOnEmpty(world, TILE_FOOD);

    clearArea(world, WORLD_WIDTH / 2, WORLD_HEIGHT / 2, 1);
}

void CatBodyInit(CatBody *cat, int x, int y)
{
    cat->x = x;
    cat->y = y;
    cat->hunger = 0.0f;
    cat->fatigue = 0.0f;
    cat->scratchUrge = 0.0f;
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

static int nearestVisibleFood(const World *world, int cx, int cy)
{
    int best = WORLD_VISION_RADIUS + 1;
    for (int dy = -WORLD_VISION_RADIUS; dy <= WORLD_VISION_RADIUS; dy++)
    {
        for (int dx = -WORLD_VISION_RADIUS; dx <= WORLD_VISION_RADIUS; dx++)
        {
            int x = cx + dx;
            int y = cy + dy;
            if (inBounds(x, y) && world->tiles[y][x] == TILE_FOOD)
            {
                int distance = abs(dx) + abs(dy);
                if (distance < best) best = distance;
            }
        }
    }
    return best;
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

    int distanceBefore = nearestVisibleFood(world, cat->x, cat->y);
    cat->x = targetX;
    cat->y = targetY;
    int distanceAfter = nearestVisibleFood(world, cat->x, cat->y);
    float shaped = REWARD_SHAPE_GAIN * (float)(distanceBefore - distanceAfter);

    if (world->tiles[targetY][targetX] == TILE_FOOD)
    {
        world->tiles[targetY][targetX] = TILE_EMPTY;
        cat->foodEaten++;
        cat->hunger -= HUNGER_FOOD_RELIEF;
        if (cat->hunger < 0.0f) cat->hunger = 0.0f;
        return REWARD_FOOD + shaped;
    }

    return REWARD_STEP + shaped;
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
