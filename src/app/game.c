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
#include <math.h>

#define SAVE_PATH "spikot.save"
#define SAVE_MAGIC 0x53504B3Au

#define SIM_FRAME_INTERVAL 6
#define SHOT_WARMUP_STEPS 400
#define SHOT_PATH "export/shot.png"
#define GRAB_RADIUS 22.0f
#define ITEM_GRAB 18.0f
#define DRAG_CLICK_DIST 6.0f
#define PET_REWARD 0.7f
#define MAX_ITEMS 24
#define MAX_STAINS 16
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
        case ITEM_PLANT:
        case ITEM_LITTER: return MAT_HARD;
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

static void addStain(Stain *stains, int *count, int x, int y)
{
    for (int i = 0; i < *count; i++) if (stains[i].x == x && stains[i].y == y) return;
    if (*count < MAX_STAINS) stains[(*count)++] = (Stain){ x, y };
}

static bool removeStainAt(Stain *stains, int *count, int x, int y)
{
    for (int i = 0; i < *count; i++)
        if (stains[i].x == x && stains[i].y == y) { stains[i] = stains[--(*count)]; return true; }
    return false;
}

static bool stainAt(const Stain *stains, int count, int x, int y)
{
    for (int i = 0; i < count; i++) if (stains[i].x == x && stains[i].y == y) return true;
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
    items[4] = (RoomItem){ ITEM_LITTER, 8, 9, false, 0.0f };
    return 5;
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
        AgentAct(agent, world, &body, -1, -1, -1, -1, 0.0f, (CatSenses){ 0 }, items, itemCount, true, NULL, &voice);
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
        RenderScene(agent, &body, &view, &cat, voice, world, items, itemCount, NULL, 0, -1, false, GetTime());
    TakeScreenshot(SHOT_PATH);

    PixelCatUnload(&cat);
    free(agent); free(world);
    CloseWindow();
    return 0;
}

typedef struct {
    CatAgent *agent;
    World *world;
    CatGenome genome;
    PixelCat cat;
    CatBody body;
    CatView view;
    RoomItem items[MAX_ITEMS];
    int itemCount;
    Stain stains[MAX_STAINS];
    int stainCount;
    float familiarity[ITEM_TYPE_COUNT];
    float voice;
    int frame;
    bool showBrain;
    int dragCat, dragItem;
    bool dragMoved;
    Vector2 pressPos;
    Vector2 mouse;
    float awakeTimer, napTimer;
} Session;

static void clampUp(float *value)
{
    if (*value > 1.0f) *value = 1.0f;
}

static void sessionReset(Session *s)
{
    AgentInit(s->agent, nextSeed());
    PixelCatUnload(&s->cat);
    s->genome = CatGenomeRandom(nextSeed());
    s->cat = PixelCatCreate(s->genome);
    WorldInitRoom(s->world, nextSeed());
    CatBodyInit(&s->body, WORLD_WIDTH / 2, WORLD_HEIGHT / 2);
    s->view = viewAt(&s->body);
    s->itemCount = resetRoom(s->items);
    for (int i = 0; i < ITEM_TYPE_COUNT; i++) s->familiarity[i] = 0.0f;
    s->voice = 0.0f; s->awakeTimer = 0.0f; s->napTimer = 0.0f;
    s->stainCount = 0;
    s->dragCat = -1; s->dragItem = -1;
}

static void handleInput(Session *s)
{
    if (IsKeyPressed(KEY_R)) sessionReset(s);
    if (IsKeyPressed(KEY_B)) s->showBrain = !s->showBrain;

    stampItems(s->world, s->items, s->itemCount);

    Vector2 mouse = GetMousePosition();
    s->mouse = mouse;
    float dxCat = mouse.x - catCenterX(&s->view), dyCat = mouse.y - catCenterY(&s->view);
    bool overCat = (dxCat * dxCat + dyCat * dyCat) < GRAB_RADIUS * GRAB_RADIUS;
    int overItem = (s->dragCat < 0 && !overCat) ? itemUnderMouse(s->items, s->itemCount, mouse) : -1;
    SetMouseCursor((overCat || overItem >= 0 || s->dragCat >= 0 || s->dragItem >= 0)
                   ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (overCat)
        {
            s->dragCat = 0; s->dragMoved = false; s->pressPos = mouse;
            s->view.asleep = false; s->awakeTimer = 0.0f;
        }
        else if (overItem >= 0)
        {
            s->dragItem = overItem;
        }
        else
        {
            int tx, ty;
            if (mouseToTile(mouse, &tx, &ty) && removeStainAt(s->stains, &s->stainCount, tx, ty))
            {
                /* wiped the floor clean */
            }
            else
            {
                int pick = PalettePick(mouse);
                if (pick >= 0 && s->itemCount < MAX_ITEMS)
                {
                    s->items[s->itemCount] = (RoomItem){ (ItemType)pick, WORLD_WIDTH / 2, WORLD_HEIGHT / 2,
                                                         pick == ITEM_BOWL, 0.0f };
                    s->dragItem = s->itemCount;
                    s->itemCount++;
                }
            }
        }
    }

    if (s->dragCat == 0)
    {
        float mdx = mouse.x - s->pressPos.x, mdy = mouse.y - s->pressPos.y;
        if (mdx * mdx + mdy * mdy > DRAG_CLICK_DIST * DRAG_CLICK_DIST)
        {
            if (!s->dragMoved) AgentNeuromodPulse(s->agent, 0.0f, 0.0f, 0.5f);
            s->dragMoved = true;
        }
        int tx, ty;
        if (mouseToTile(mouse, &tx, &ty) && s->world->tiles[ty][tx] != TILE_OBSTACLE)
        {
            s->body.x = tx; s->body.y = ty;
            s->view.x = (mouse.x - GRID_ORIGIN_X - WORLD_TILE_PX * 0.5f) / WORLD_TILE_PX;
            s->view.y = (mouse.y - GRID_ORIGIN_Y - WORLD_TILE_PX * 0.5f) / WORLD_TILE_PX;
        }
    }
    else if (s->dragItem >= 0)
    {
        int tx, ty;
        if (mouseToTile(mouse, &tx, &ty)) { s->items[s->dragItem].x = tx; s->items[s->dragItem].y = ty; }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (s->dragCat == 0 && !s->dragMoved)
        {
            MoodPet(&s->view);
            ParticleHeart(catCenterX(&s->view), catCenterY(&s->view) - 10.0f);
            NetworkApplyReward(&s->agent->net, PET_REWARD);
            AgentNeuromodPulse(s->agent, 0.3f, 0.4f, 0.0f);
            s->body.social = 0.0f;
            s->body.bond += OXYTOCIN_PET;
            clampUp(&s->body.bond);
        }
        s->dragCat = -1; s->dragItem = -1;
    }
}

static CatSenses senseNearbyItem(Session *s)
{
    CatSenses senses = { 0 };
    int senseDist;
    int si = nearestSensibleItem(s->items, s->itemCount, s->body.x, s->body.y, &senseDist);
    if (si < 0) return senses;

    float prox = 1.0f - (float)senseDist / (SENSE_RANGE + 1);
    switch (itemMaterial(s->items[si].type))
    {
        case MAT_SOFT: senses.soft = prox; break;
        case MAT_HARD: senses.hard = prox; break;
        default: senses.wet = prox; break;
    }
    senses.novelty = (1.0f - s->familiarity[s->items[si].type]) * prox;
    senses.dx = s->items[si].x - s->body.x;
    senses.dy = s->items[si].y - s->body.y;
    s->familiarity[s->items[si].type] += FAMILIARITY_RATE;
    clampUp(&s->familiarity[s->items[si].type]);
    s->view.curiosity = senses.novelty;
    if (senses.novelty > NOVELTY_ALERT) { s->view.mood = EMOTION_CURIOUS; s->view.moodHold = 1.2f; }
    return senses;
}

static void raiseNeeds(CatBody *body)
{
    body->fatigue += FATIGUE_RATE;       clampUp(&body->fatigue);
    body->scratchUrge += SCRATCH_RATE;   clampUp(&body->scratchUrge);
    body->bladder += BLADDER_RATE;       clampUp(&body->bladder);
    body->boredom += BOREDOM_RATE;       clampUp(&body->boredom);
    body->grime += GRIME_RATE;           clampUp(&body->grime);
    body->social += SOCIAL_RATE;         clampUp(&body->social);
}

static void satisfyDrives(Session *s, int playerTileX, int playerTileY)
{
    CatAgent *agent = s->agent;
    CatBody *body = &s->body;

    if (agent->activeDrive == DRIVE_SOCIAL && playerTileX >= 0 &&
        abs(body->x - playerTileX) + abs(body->y - playerTileY) <= SOCIAL_NEAR_DIST)
    {
        body->social -= SOCIAL_RELIEF;
        if (body->social < 0.0f) body->social = 0.0f;
        body->bond += OXYTOCIN_NEAR;
        clampUp(&body->bond);
        AgentNeuromodPulse(agent, 0.0f, 0.15f, 0.0f);
    }

    if (agent->activeDrive == DRIVE_SCRATCH && adjacentToItem(s->items, s->itemCount, ITEM_POST, body->x, body->y))
    {
        body->scratchUrge = 0.0f;
        s->view.mood = EMOTION_HAPPY;
        s->view.moodHold = 1.2f;
        ParticleDust(catCenterX(&s->view), catCenterY(&s->view));
        AgentReinforcePlace(agent, DRIVE_SCRATCH, body->x, body->y);
    }

    if (agent->activeDrive == DRIVE_BLADDER && onItemTile(s->items, s->itemCount, ITEM_LITTER, body->x, body->y))
    {
        body->bladder = 0.0f;
        body->grime += GRIME_ON_EVENT;
        clampUp(&body->grime);
        ParticleDust(catCenterX(&s->view), catCenterY(&s->view));
        AgentReinforcePlace(agent, DRIVE_BLADDER, body->x, body->y);
        AgentNeuromodPulse(agent, 0.0f, 0.3f, 0.0f);
    }
    else if (body->bladder >= 1.0f)
    {
        addStain(s->stains, &s->stainCount, body->x, body->y);
        body->bladder = 0.0f;
        AgentNeuromodPulse(agent, 0.0f, 0.0f, 0.2f);
    }

    if (stainAt(s->stains, s->stainCount, body->x, body->y))
    {
        body->grime += STAIN_GRIME;
        clampUp(&body->grime);
    }

    if (agent->activeDrive == DRIVE_PLAY)
    {
        body->boredom -= BOREDOM_RELIEF;
        if (body->boredom < 0.0f) body->boredom = 0.0f;
    }

    if (agent->activeDrive == DRIVE_GROOM)
    {
        body->grime -= GRIME_RELIEF;
        if (body->grime < 0.0f) body->grime = 0.0f;
        AgentNeuromodPulse(agent, 0.0f, 0.05f, 0.0f);
    }

    bool atBed = onItemTile(s->items, s->itemCount, ITEM_BED, body->x, body->y);
    if (agent->activeDrive == DRIVE_FATIGUE && atBed && body->fatigue > SLEEP_FATIGUE && s->awakeTimer > MIN_AWAKE)
    {
        s->view.asleep = true;
        s->napTimer = 0.0f;
        AgentReinforcePlace(agent, DRIVE_FATIGUE, body->x, body->y);
    }
    else if (body->fatigue >= EXHAUSTED_FATIGUE && s->awakeTimer > MIN_AWAKE)
    {
        s->view.asleep = true;
        s->napTimer = 0.0f;
    }
}

static void thinkStep(Session *s)
{
    int before = s->body.foodEaten;
    int px = s->body.x, py = s->body.y;

    CatSenses senses = senseNearbyItem(s);

    int playerTileX = -1, playerTileY = -1;
    if (s->dragCat < 0)
    {
        int ptx, pty;
        if (mouseToTile(s->mouse, &ptx, &pty)) { playerTileX = ptx; playerTileY = pty; }
    }

    float dayPhase = (float)(fmod(GetTime(), DAY_LENGTH) / DAY_LENGTH);
    s->agent->circadian = 1.0f - fabsf(sinf(dayPhase * 2.0f * PI));

    AgentAct(s->agent, s->world, &s->body, -1, -1, playerTileX, playerTileY, 0.0f, senses,
             s->items, s->itemCount, true, NULL, &s->voice);

    raiseNeeds(&s->body);
    satisfyDrives(s, playerTileX, playerTileY);

    float cx = GRID_ORIGIN_X + s->body.x * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
    float cy = GRID_ORIGIN_Y + s->body.y * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
    if (s->body.foodEaten > before)
    {
        ParticleCrumbs(cx, cy);
        s->body.grime += GRIME_ON_EVENT;
        clampUp(&s->body.grime);
        for (int i = 0; i < s->itemCount; i++)
            if (s->items[i].type == ITEM_BOWL && s->items[i].x == s->body.x && s->items[i].y == s->body.y)
                { s->items[i].hasFood = false; s->items[i].refill = BOWL_REFILL; }
    }
    if (s->body.x != px || s->body.y != py)
        ParticleDust(cx, cy + WORLD_TILE_PX * 0.35f);
}

int RunGame(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(TARGET_FPS);
    SetRandomSeed((unsigned int)(GetTime() * 1000.0) + 1u);

    Session *s = calloc(1, sizeof(Session));
    if (!s) { CloseWindow(); return 1; }
    s->agent = malloc(sizeof(CatAgent));
    s->world = malloc(sizeof(World));
    if (!s->agent || !s->world) { free(s->agent); free(s->world); free(s); CloseWindow(); return 1; }

    WorldInitRoom(s->world, nextSeed());

    int savedPets = 0;
    if (!loadGame(s->agent, &s->genome, &s->body, &savedPets, s->items, &s->itemCount, s->familiarity))
    {
        AgentInit(s->agent, nextSeed());
        s->genome = CatGenomeRandom(nextSeed());
        CatBodyInit(&s->body, WORLD_WIDTH / 2, WORLD_HEIGHT / 2);
        s->itemCount = resetRoom(s->items);
    }

    s->cat = PixelCatCreate(s->genome);
    s->view = viewAt(&s->body);
    s->view.pets = savedPets;
    s->dragCat = -1; s->dragItem = -1;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        handleInput(s);

        if (++s->frame >= SIM_FRAME_INTERVAL)
        {
            s->frame = 0;
            if (s->dragCat == 0) AgentCarried(s->agent, s->body.bond);
            else if (s->view.asleep) { AgentRest(s->agent); restTick(&s->body); }
            else thinkStep(s);
        }

        refillBowls(s->items, s->itemCount, dt);
        if (s->dragCat != 0) ViewUpdate(&s->view, &s->body);
        MoodUpdate(&s->view, s->agent, s->world, &s->body, -1, -1, dt);
        updateSleep(&s->view, &s->body, &s->awakeTimer, &s->napTimer, dt);
        if (s->view.stretch > 0.0f) { s->view.stretch -= dt * 2.5f; if (s->view.stretch < 0.0f) s->view.stretch = 0.0f; }
        s->view.curiosity *= 0.95f;
        for (int i = 0; i < ITEM_TYPE_COUNT; i++) s->familiarity[i] *= FAMILIARITY_DECAY;
        ParticlesUpdate(dt);

        RenderScene(s->agent, &s->body, &s->view, &s->cat, s->voice, s->world, s->items, s->itemCount,
                    s->stains, s->stainCount, s->dragItem, s->showBrain, GetTime());
    }

    saveGame(s->agent, &s->genome, &s->body, s->view.pets, s->items, s->itemCount, s->familiarity);
    PixelCatUnload(&s->cat);
    free(s->agent); free(s->world); free(s);
    CloseWindow();
    return 0;
}
