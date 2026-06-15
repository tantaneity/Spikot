#include "app/game.h"
#include "render/render.h"
#include "config.h"
#include "cat/genome.h"
#include "cat/pixel_cat.h"
#include "env/world.h"
#include "agent/agent.h"
#include "snn/network.h"
#include "raylib.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#define SAVE_PATH "spikot.save"
#define SAVE_MAGIC 0x53504B36u

#define SIM_FRAME_INTERVAL 6
#define SHOT_WARMUP_STEPS 400
#define SHOT_PATH "export/shot.png"
#define GRAB_RADIUS 22.0f
#define ITEM_GRAB 18.0f
#define DRAG_CLICK_DIST 6.0f
#define PET_REWARD 0.7f
#define MAX_ITEMS 24
#define BOWL_REFILL 3.0f

#define MIN_AWAKE 8.0f
#define NAP_DURATION 5.0f

static uint32_t nextSeed(void)
{
    return (uint32_t)GetRandomValue(1, 2000000000);
}

static CatView viewAt(const CatBody *body)
{
    return (CatView){ (float)body->x, (float)body->y, false, EMOTION_CONTENT, 0.0f, 0, false, 0.0f, 0.0f };
}

typedef enum { MAT_SOFT, MAT_HARD, MAT_WET } MaterialKind;

static MaterialKind itemMaterial(ItemType type)
{
    switch (type)
    {
        case ITEM_BED:
        case ITEM_RUG: return MAT_SOFT;
        case ITEM_POST:
        case ITEM_PLANT: return MAT_HARD;
        default: return MAT_WET;
    }
}

static bool onItemTile(const RoomItem *items, int count, ItemType type, int bx, int by)
{
    for (int i = 0; i < count; i++)
        if (items[i].type == type && items[i].x == bx && items[i].y == by) return true;
    return false;
}

static bool adjacentToItem(const RoomItem *items, int count, ItemType type, int bx, int by)
{
    for (int i = 0; i < count; i++)
        if (items[i].type == type && abs(items[i].x - bx) + abs(items[i].y - by) <= 1) return true;
    return false;
}

static int nearestSensibleItem(const RoomItem *items, int count, int bx, int by, int *outDist)
{
    int best = SENSE_RANGE + 1, idx = -1;
    for (int i = 0; i < count; i++)
    {
        int d = abs(items[i].x - bx) + abs(items[i].y - by);
        if (d <= SENSE_RANGE && d < best) { best = d; idx = i; }
    }
    *outDist = best;
    return idx;
}

static int resetRoom(RoomItem *items)
{
    items[0] = (RoomItem){ ITEM_RUG, WORLD_WIDTH / 2, WORLD_HEIGHT / 2, false, 0.0f };
    items[1] = (RoomItem){ ITEM_BOWL, WORLD_WIDTH - 8, 9, true, 0.0f };
    items[2] = (RoomItem){ ITEM_BED, 7, WORLD_HEIGHT - 8, false, 0.0f };
    items[3] = (RoomItem){ ITEM_PLANT, WORLD_WIDTH - 6, WORLD_HEIGHT - 6, false, 0.0f };
    return 4;
}

static void stampItems(World *world, const RoomItem *items, int count)
{
    WorldClearInterior(world);
    for (int i = 0; i < count; i++)
    {
        int x = items[i].x, y = items[i].y;
        if (x <= 0 || y <= 0 || x >= WORLD_WIDTH - 1 || y >= WORLD_HEIGHT - 1) continue;
        if (items[i].type == ITEM_PLANT || items[i].type == ITEM_POST)
            world->tiles[y][x] = TILE_OBSTACLE;
        else if (items[i].type == ITEM_BOWL && items[i].hasFood)
            world->tiles[y][x] = TILE_FOOD;
    }
}

static void refillBowls(RoomItem *items, int count, float dt)
{
    for (int i = 0; i < count; i++)
        if (items[i].type == ITEM_BOWL && !items[i].hasFood)
        {
            items[i].refill -= dt;
            if (items[i].refill <= 0.0f) items[i].hasFood = true;
        }
}

static void updateSleep(CatView *view, const CatBody *body, float *awakeTimer, float *napTimer, float dt)
{
    if (view->asleep)
    {
        *napTimer += dt;
        if (body->fatigue < WAKE_FATIGUE || body->hunger > WAKE_HUNGER)
        {
            view->asleep = false;
            view->stretch = 1.0f;
            *awakeTimer = 0.0f;
        }
    }
    else
    {
        *awakeTimer += dt;
    }
}

static void restTick(CatBody *body)
{
    body->hunger += HUNGER_RATE;
    if (body->hunger > 1.0f) body->hunger = 1.0f;
    body->fatigue -= FATIGUE_RECOVER;
    if (body->fatigue < 0.0f) body->fatigue = 0.0f;
}

static float catCenterX(const CatView *view) { return GRID_ORIGIN_X + view->x * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f; }
static float catCenterY(const CatView *view) { return GRID_ORIGIN_Y + view->y * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f; }

static int itemUnderMouse(const RoomItem *items, int count, Vector2 mouse)
{
    for (int i = count - 1; i >= 0; i--)
    {
        float cx = GRID_ORIGIN_X + items[i].x * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
        float cy = GRID_ORIGIN_Y + items[i].y * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
        float dx = mouse.x - cx, dy = mouse.y - cy;
        if (dx * dx + dy * dy < ITEM_GRAB * ITEM_GRAB) return i;
    }
    return -1;
}

static bool mouseToTile(Vector2 mouse, int *tx, int *ty)
{
    int x = (int)((mouse.x - GRID_ORIGIN_X) / WORLD_TILE_PX);
    int y = (int)((mouse.y - GRID_ORIGIN_Y) / WORLD_TILE_PX);
    if (x < 1 || x >= WORLD_WIDTH - 1 || y < 1 || y >= WORLD_HEIGHT - 1) return false;
    *tx = x; *ty = y;
    return true;
}

static bool loadGame(CatAgent *agent, CatGenome *genome, CatBody *body,
                     int *pets, RoomItem *items, int *itemCount, float *familiarity)
{
    FILE *file = fopen(SAVE_PATH, "rb");
    if (!file) return false;

    uint32_t magic = 0;
    bool ok = fread(&magic, sizeof(magic), 1, file) == 1 && magic == SAVE_MAGIC;
    if (ok) ok = fread(genome, sizeof(*genome), 1, file) == 1;
    if (ok) ok = fread(&agent->net, sizeof(agent->net), 1, file) == 1;
    if (ok) ok = fread(&agent->spatial, sizeof(agent->spatial), 1, file) == 1;
    if (ok) ok = fread(body, sizeof(*body), 1, file) == 1;
    if (ok) ok = fread(pets, sizeof(*pets), 1, file) == 1;
    if (ok) ok = fread(familiarity, sizeof(float) * ITEM_TYPE_COUNT, 1, file) == 1;
    if (ok) ok = fread(itemCount, sizeof(*itemCount), 1, file) == 1;
    if (ok && (*itemCount < 0 || *itemCount > MAX_ITEMS)) ok = false;
    if (ok && *itemCount > 0) ok = fread(items, sizeof(RoomItem) * (*itemCount), 1, file) == 1;

    fclose(file);

    if (ok)
    {
        agent->wanderX = body->x;
        agent->wanderY = body->y;
        agent->exploring = false;
        agent->activeDrive = DRIVE_NONE;
        agent->rewardBaseline = 0.0f;
        AgentResetMods(agent);
        agent->traceHead = 0;
        agent->traceCount = 0;
        if (agent->rng == 0u) agent->rng = 0x5BD1E995u;
    }
    return ok;
}

static void saveGame(const CatAgent *agent, const CatGenome *genome, const CatBody *body,
                     int pets, const RoomItem *items, int itemCount, const float *familiarity)
{
    FILE *file = fopen(SAVE_PATH, "wb");
    if (!file) return;

    uint32_t magic = SAVE_MAGIC;
    fwrite(&magic, sizeof(magic), 1, file);
    fwrite(genome, sizeof(*genome), 1, file);
    fwrite(&agent->net, sizeof(agent->net), 1, file);
    fwrite(&agent->spatial, sizeof(agent->spatial), 1, file);
    fwrite(body, sizeof(*body), 1, file);
    fwrite(&pets, sizeof(pets), 1, file);
    fwrite(familiarity, sizeof(float) * ITEM_TYPE_COUNT, 1, file);
    fwrite(&itemCount, sizeof(itemCount), 1, file);
    if (itemCount > 0) fwrite(items, sizeof(RoomItem) * itemCount, 1, file);

    fclose(file);
}

int RunShot(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(TARGET_FPS);

    CatAgent *agent = malloc(sizeof(CatAgent));
    World *world = malloc(sizeof(World));
    if (!agent || !world) { CloseWindow(); free(agent); free(world); return 1; }

    AgentInit(agent, 4242u);
    WorldInitRoom(world, 777u);
    PixelCat cat = PixelCatCreate(CatGenomeRandom(20260615u));

    CatBody body;
    CatBodyInit(&body, WORLD_WIDTH / 2, WORLD_HEIGHT / 2);

    RoomItem items[MAX_ITEMS];
    int itemCount = resetRoom(items);

    float voice = 0.0f;
    for (int step = 0; step < SHOT_WARMUP_STEPS; step++)
    {
        stampItems(world, items, itemCount);
        int before = body.foodEaten;
        AgentAct(agent, world, &body, -1, -1, 0.0f, (CatSenses){ 0 }, items, itemCount, true, NULL, &voice);
        if (body.foodEaten > before)
            for (int i = 0; i < itemCount; i++)
                if (items[i].type == ITEM_BOWL && items[i].x == body.x && items[i].y == body.y)
                    { items[i].hasFood = false; items[i].refill = BOWL_REFILL; }
        refillBowls(items, itemCount, 0.1f);
    }

    CatView view = viewAt(&body);
    MoodUpdate(&view, agent, world, &body, -1, -1, 0.0f);
    stampItems(world, items, itemCount);

    for (int frame = 0; frame < 8; frame++)
        RenderScene(agent, &body, &view, &cat, voice, world, items, itemCount, -1, false, GetTime());
    TakeScreenshot(SHOT_PATH);

    PixelCatUnload(&cat);
    free(agent); free(world);
    CloseWindow();
    return 0;
}

int RunGame(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(TARGET_FPS);
    SetRandomSeed((unsigned int)(GetTime() * 1000.0) + 1u);

    CatAgent *agent = malloc(sizeof(CatAgent));
    World *world = malloc(sizeof(World));
    if (!agent || !world) { CloseWindow(); return 1; }

    WorldInitRoom(world, nextSeed());

    CatGenome genome;
    CatBody body;
    RoomItem items[MAX_ITEMS];
    int itemCount = 0;
    int savedPets = 0;
    float familiarity[ITEM_TYPE_COUNT] = { 0 };

    if (!loadGame(agent, &genome, &body, &savedPets, items, &itemCount, familiarity))
    {
        AgentInit(agent, nextSeed());
        genome = CatGenomeRandom(nextSeed());
        CatBodyInit(&body, WORLD_WIDTH / 2, WORLD_HEIGHT / 2);
        itemCount = resetRoom(items);
    }

    PixelCat cat = PixelCatCreate(genome);
    CatView view = viewAt(&body);
    view.pets = savedPets;

    float voice = 0.0f;
    int frame = 0;
    bool showBrain = false;
    int dragCat = -1, dragItem = -1;
    bool dragMoved = false;
    Vector2 pressPos = { 0 };
    float awakeTimer = 0.0f, napTimer = 0.0f;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        if (IsKeyPressed(KEY_R))
        {
            AgentInit(agent, nextSeed());
            PixelCatUnload(&cat);
            genome = CatGenomeRandom(nextSeed());
            cat = PixelCatCreate(genome);
            WorldInitRoom(world, nextSeed());
            CatBodyInit(&body, WORLD_WIDTH / 2, WORLD_HEIGHT / 2);
            view = viewAt(&body);
            itemCount = resetRoom(items);
            for (int i = 0; i < ITEM_TYPE_COUNT; i++) familiarity[i] = 0.0f;
            voice = 0.0f; awakeTimer = 0.0f; napTimer = 0.0f;
            dragCat = -1; dragItem = -1;
        }
        if (IsKeyPressed(KEY_B)) showBrain = !showBrain;

        stampItems(world, items, itemCount);

        Vector2 mouse = GetMousePosition();
        float dxCat = mouse.x - catCenterX(&view), dyCat = mouse.y - catCenterY(&view);
        bool overCat = (dxCat * dxCat + dyCat * dyCat) < GRAB_RADIUS * GRAB_RADIUS;
        int overItem = (dragCat < 0 && !overCat) ? itemUnderMouse(items, itemCount, mouse) : -1;
        SetMouseCursor((overCat || overItem >= 0 || dragCat >= 0 || dragItem >= 0)
                       ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (overCat)
            {
                dragCat = 0; dragMoved = false; pressPos = mouse;
                view.asleep = false; awakeTimer = 0.0f;
            }
            else if (overItem >= 0)
            {
                dragItem = overItem;
            }
            else
            {
                int pick = PalettePick(mouse);
                if (pick >= 0 && itemCount < MAX_ITEMS)
                {
                    items[itemCount] = (RoomItem){ (ItemType)pick, WORLD_WIDTH / 2, WORLD_HEIGHT / 2,
                                                   pick == ITEM_BOWL, 0.0f };
                    dragItem = itemCount;
                    itemCount++;
                }
            }
        }

        if (dragCat == 0)
        {
            float mdx = mouse.x - pressPos.x, mdy = mouse.y - pressPos.y;
            if (mdx * mdx + mdy * mdy > DRAG_CLICK_DIST * DRAG_CLICK_DIST)
            {
                if (!dragMoved) AgentNeuromodPulse(agent, 0.0f, 0.0f, 0.5f);
                dragMoved = true;
            }
            int tx, ty;
            if (mouseToTile(mouse, &tx, &ty) && world->tiles[ty][tx] != TILE_OBSTACLE)
            {
                body.x = tx; body.y = ty;
                view.x = (mouse.x - GRID_ORIGIN_X - WORLD_TILE_PX * 0.5f) / WORLD_TILE_PX;
                view.y = (mouse.y - GRID_ORIGIN_Y - WORLD_TILE_PX * 0.5f) / WORLD_TILE_PX;
            }
        }
        else if (dragItem >= 0)
        {
            int tx, ty;
            if (mouseToTile(mouse, &tx, &ty)) { items[dragItem].x = tx; items[dragItem].y = ty; }
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            if (dragCat == 0 && !dragMoved)
            {
                MoodPet(&view);
                ParticleHeart(catCenterX(&view), catCenterY(&view) - 10.0f);
                NetworkApplyReward(&agent->net, PET_REWARD);
                AgentNeuromodPulse(agent, 0.3f, 0.4f, 0.0f);
            }
            dragCat = -1; dragItem = -1;
        }

        if (++frame >= SIM_FRAME_INTERVAL)
        {
            frame = 0;
            if (dragCat == 0)
            {
                /* carried: cat rests in hand */
            }
            else if (view.asleep)
            {
                AgentRest(agent);
                restTick(&body);
            }
            else
            {
                int before = body.foodEaten;
                int px = body.x, py = body.y;

                CatSenses senses = { 0 };
                int senseDist;
                int si = nearestSensibleItem(items, itemCount, body.x, body.y, &senseDist);
                if (si >= 0)
                {
                    float prox = 1.0f - (float)senseDist / (SENSE_RANGE + 1);
                    switch (itemMaterial(items[si].type))
                    {
                        case MAT_SOFT: senses.soft = prox; break;
                        case MAT_HARD: senses.hard = prox; break;
                        default: senses.wet = prox; break;
                    }
                    senses.novelty = (1.0f - familiarity[items[si].type]) * prox;
                    senses.dx = items[si].x - body.x;
                    senses.dy = items[si].y - body.y;
                    familiarity[items[si].type] += FAMILIARITY_RATE;
                    if (familiarity[items[si].type] > 1.0f) familiarity[items[si].type] = 1.0f;
                    view.curiosity = senses.novelty;
                    if (senses.novelty > NOVELTY_ALERT) { view.mood = EMOTION_CURIOUS; view.moodHold = 1.2f; }
                }

                AgentAct(agent, world, &body, -1, -1, 0.0f, senses, items, itemCount, true, NULL, &voice);

                body.fatigue += FATIGUE_RATE;
                if (body.fatigue > 1.0f) body.fatigue = 1.0f;
                body.scratchUrge += SCRATCH_RATE;
                if (body.scratchUrge > 1.0f) body.scratchUrge = 1.0f;

                if (agent->activeDrive == DRIVE_SCRATCH && adjacentToItem(items, itemCount, ITEM_POST, body.x, body.y))
                {
                    body.scratchUrge = 0.0f;
                    view.mood = EMOTION_HAPPY;
                    view.moodHold = 1.2f;
                    ParticleDust(catCenterX(&view), catCenterY(&view));
                    AgentReinforcePlace(agent, DRIVE_SCRATCH, body.x, body.y);
                }

                bool atBed = onItemTile(items, itemCount, ITEM_BED, body.x, body.y);
                if (agent->activeDrive == DRIVE_FATIGUE && atBed && body.fatigue > SLEEP_FATIGUE && awakeTimer > MIN_AWAKE)
                {
                    view.asleep = true;
                    napTimer = 0.0f;
                    AgentReinforcePlace(agent, DRIVE_FATIGUE, body.x, body.y);
                }
                else if (body.fatigue >= EXHAUSTED_FATIGUE && awakeTimer > MIN_AWAKE)
                {
                    view.asleep = true;
                    napTimer = 0.0f;
                }

                float cx = GRID_ORIGIN_X + body.x * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
                float cy = GRID_ORIGIN_Y + body.y * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
                if (body.foodEaten > before)
                {
                    ParticleCrumbs(cx, cy);
                    for (int i = 0; i < itemCount; i++)
                        if (items[i].type == ITEM_BOWL && items[i].x == body.x && items[i].y == body.y)
                            { items[i].hasFood = false; items[i].refill = BOWL_REFILL; }
                }
                if (body.x != px || body.y != py)
                    ParticleDust(cx, cy + WORLD_TILE_PX * 0.35f);
            }
        }

        refillBowls(items, itemCount, dt);
        if (dragCat != 0) ViewUpdate(&view, &body);
        MoodUpdate(&view, agent, world, &body, -1, -1, dt);
        updateSleep(&view, &body, &awakeTimer, &napTimer, dt);
        if (view.stretch > 0.0f) { view.stretch -= dt * 2.5f; if (view.stretch < 0.0f) view.stretch = 0.0f; }
        view.curiosity *= 0.95f;
        for (int i = 0; i < ITEM_TYPE_COUNT; i++) familiarity[i] *= FAMILIARITY_DECAY;
        ParticlesUpdate(dt);

        RenderScene(agent, &body, &view, &cat, voice, world, items, itemCount, dragItem, showBrain, GetTime());
    }

    saveGame(agent, &genome, &body, view.pets, items, itemCount, familiarity);
    PixelCatUnload(&cat);
    free(agent); free(world);
    CloseWindow();
    return 0;
}
