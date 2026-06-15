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
    return (CatView){ (float)body->x, (float)body->y, false, EMOTION_CONTENT, 0.0f, 0, false };
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
        if (*napTimer > NAP_DURATION || body->hunger > WAKE_HUNGER) { view->asleep = false; *awakeTimer = 0.0f; }
    }
    else
    {
        *awakeTimer += dt;
        if (body->hunger < SLEEP_HUNGER && *awakeTimer > MIN_AWAKE) { view->asleep = true; *napTimer = 0.0f; }
    }
}

static void restTick(CatBody *body)
{
    body->hunger += HUNGER_RATE;
    if (body->hunger > 1.0f) body->hunger = 1.0f;
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
        AgentAct(agent, world, &body, -1, -1, 0.0f, true, NULL, &voice);
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

    AgentInit(agent, nextSeed());
    WorldInitRoom(world, nextSeed());
    PixelCat cat = PixelCatCreate(CatGenomeRandom(nextSeed()));

    CatBody body;
    CatBodyInit(&body, WORLD_WIDTH / 2, WORLD_HEIGHT / 2);
    CatView view = viewAt(&body);

    RoomItem items[MAX_ITEMS];
    int itemCount = resetRoom(items);

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
            WorldInitRoom(world, nextSeed());
            CatBodyInit(&body, WORLD_WIDTH / 2, WORLD_HEIGHT / 2);
            view = viewAt(&body);
            itemCount = resetRoom(items);
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
            if (mdx * mdx + mdy * mdy > DRAG_CLICK_DIST * DRAG_CLICK_DIST) dragMoved = true;
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
                HeartsSpawn(catCenterX(&view), catCenterY(&view) - 10.0f);
                NetworkApplyReward(&agent->net, PET_REWARD);
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
                AgentAct(agent, world, &body, -1, -1, 0.0f, true, NULL, &voice);
                if (body.foodEaten > before)
                    for (int i = 0; i < itemCount; i++)
                        if (items[i].type == ITEM_BOWL && items[i].x == body.x && items[i].y == body.y)
                            { items[i].hasFood = false; items[i].refill = BOWL_REFILL; }
            }
        }

        refillBowls(items, itemCount, dt);
        if (dragCat != 0) ViewUpdate(&view, &body);
        MoodUpdate(&view, agent, world, &body, -1, -1, dt);
        updateSleep(&view, &body, &awakeTimer, &napTimer, dt);
        HeartsUpdate(dt);

        RenderScene(agent, &body, &view, &cat, voice, world, items, itemCount, dragItem, showBrain, GetTime());
    }

    PixelCatUnload(&cat);
    free(agent); free(world);
    CloseWindow();
    return 0;
}
