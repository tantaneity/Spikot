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
#define CAT_A_SEED_OFFSET 101u
#define CAT_B_SEED_OFFSET 202u
#define SHOT_WARMUP_STEPS 400
#define SHOT_PATH "export/shot.png"
#define GRAB_RADIUS 20.0f
#define DRAG_CLICK_DIST 6.0f
#define PET_REWARD 0.7f

static uint32_t nextSeed(void)
{
    return (uint32_t)GetRandomValue(1, 2000000000);
}

static CatView viewAt(const CatBody *body)
{
    return (CatView){ (float)body->x, (float)body->y, false, EMOTION_CONTENT, 0.0f, 0, false };
}

static void updateSleep(CatView *view, const CatBody *body,
                        float *awakeTimer, float *napTimer, float dt)
{
    if (view->asleep)
    {
        *napTimer += dt;
        if (*napTimer > NAP_DURATION || body->hunger > WAKE_HUNGER)
        {
            view->asleep = false;
            *awakeTimer = 0.0f;
        }
    }
    else
    {
        *awakeTimer += dt;
        if (body->hunger < SLEEP_HUNGER && *awakeTimer > MIN_AWAKE)
        {
            view->asleep = true;
            *napTimer = 0.0f;
        }
    }
}

static void wake(CatView *view, float *awakeTimer)
{
    view->asleep = false;
    *awakeTimer = 0.0f;
}

static void restTick(CatBody *body)
{
    body->hunger += HUNGER_RATE;
    if (body->hunger > 1.0f) body->hunger = 1.0f;
}

int RunShot(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(TARGET_FPS);

    CatAgent *agentA = malloc(sizeof(CatAgent));
    CatAgent *agentB = malloc(sizeof(CatAgent));
    World *world = malloc(sizeof(World));
    if (!agentA || !agentB || !world) { CloseWindow(); free(agentA); free(agentB); free(world); return 1; }

    AgentInit(agentA, 4242u);
    AgentInit(agentB, 7777u);
    WorldInit(world, 777u);
    PixelCat catA = PixelCatCreate(CatGenomeRandom(20260615u));
    PixelCat catB = PixelCatCreate(CatGenomeRandom(31415926u));

    CatBody bodyA, bodyB;
    CatBodyInit(&bodyA, CAT_A_START_X, CAT_A_START_Y);
    CatBodyInit(&bodyB, CAT_B_START_X, CAT_B_START_Y);

    float voiceA = 0.0f, voiceB = 0.0f;
    for (int step = 0; step < SHOT_WARMUP_STEPS; step++)
    {
        float nva = 0.0f, nvb = 0.0f;
        AgentAct(agentA, world, &bodyA, bodyB.x, bodyB.y, voiceB, true, NULL, &nva);
        AgentAct(agentB, world, &bodyB, bodyA.x, bodyA.y, voiceA, true, NULL, &nvb);
        voiceA = nva; voiceB = nvb;
    }

    CatView viewA = viewAt(&bodyA);
    CatView viewB = viewAt(&bodyB);
    MoodUpdate(&viewA, agentA, world, &bodyA, bodyB.x, bodyB.y, 0.0f);
    MoodUpdate(&viewB, agentB, world, &bodyB, bodyA.x, bodyA.y, 0.0f);

    for (int frame = 0; frame < 8; frame++)
        RenderScene(agentA, &bodyA, &viewA, &catA, voiceA,
                    agentB, &bodyB, &viewB, &catB, voiceB, world, false, GetTime());
    TakeScreenshot(SHOT_PATH);

    PixelCatUnload(&catA);
    PixelCatUnload(&catB);
    free(agentA); free(agentB); free(world);
    CloseWindow();
    return 0;
}

int RunGame(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(TARGET_FPS);
    SetRandomSeed((unsigned int)(GetTime() * 1000.0) + 1u);

    CatAgent *agentA = malloc(sizeof(CatAgent));
    CatAgent *agentB = malloc(sizeof(CatAgent));
    World *world = malloc(sizeof(World));
    if (!agentA || !agentB || !world) { CloseWindow(); return 1; }

    uint32_t seed = nextSeed();
    AgentInit(agentA, seed + CAT_A_SEED_OFFSET);
    AgentInit(agentB, seed + CAT_B_SEED_OFFSET);
    WorldInit(world, nextSeed());

    PixelCat catA = PixelCatCreate(CatGenomeRandom(nextSeed()));
    PixelCat catB = PixelCatCreate(CatGenomeRandom(nextSeed()));

    CatBody bodyA, bodyB;
    CatBodyInit(&bodyA, CAT_A_START_X, CAT_A_START_Y);
    CatBodyInit(&bodyB, CAT_B_START_X, CAT_B_START_Y);

    float voiceA = 0.0f, voiceB = 0.0f;
    int frame = 0;
    bool showBrain = false;
    int dragging = -1;
    bool dragMoved = false;
    Vector2 pressPos = { 0 };
    float awakeTimerA = 0.0f, napTimerA = 0.0f;
    float awakeTimerB = 0.0f, napTimerB = 0.0f;

    CatView viewA = viewAt(&bodyA);
    CatView viewB = viewAt(&bodyB);

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        if (IsKeyPressed(KEY_R))
        {
            AgentInit(agentA, nextSeed());
            AgentInit(agentB, nextSeed());
            WorldInit(world, nextSeed());
            CatBodyInit(&bodyA, CAT_A_START_X, CAT_A_START_Y);
            CatBodyInit(&bodyB, CAT_B_START_X, CAT_B_START_Y);
            viewA = viewAt(&bodyA);
            viewB = viewAt(&bodyB);
            voiceA = 0.0f; voiceB = 0.0f;
            awakeTimerA = 0.0f; napTimerA = 0.0f;
            awakeTimerB = 0.0f; napTimerB = 0.0f;
            dragging = -1;
        }
        if (IsKeyPressed(KEY_B)) showBrain = !showBrain;

        Vector2 mouse = GetMousePosition();
        float ax = GRID_ORIGIN_X + viewA.x * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
        float ay = GRID_ORIGIN_Y + viewA.y * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
        float bx = GRID_ORIGIN_X + viewB.x * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
        float by = GRID_ORIGIN_Y + viewB.y * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f;
        float distA = (mouse.x - ax) * (mouse.x - ax) + (mouse.y - ay) * (mouse.y - ay);
        float distB = (mouse.x - bx) * (mouse.x - bx) + (mouse.y - by) * (mouse.y - by);
        int hovered = -1;
        if (distA < GRAB_RADIUS * GRAB_RADIUS && distA <= distB) hovered = 0;
        else if (distB < GRAB_RADIUS * GRAB_RADIUS) hovered = 1;
        SetMouseCursor((hovered >= 0 || dragging >= 0) ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hovered >= 0)
        {
            dragging = hovered;
            dragMoved = false;
            pressPos = mouse;
            wake(hovered == 0 ? &viewA : &viewB, hovered == 0 ? &awakeTimerA : &awakeTimerB);
        }
        if (dragging >= 0)
        {
            float mdx = mouse.x - pressPos.x, mdy = mouse.y - pressPos.y;
            if (mdx * mdx + mdy * mdy > DRAG_CLICK_DIST * DRAG_CLICK_DIST) dragMoved = true;

            int tx = (int)((mouse.x - GRID_ORIGIN_X) / WORLD_TILE_PX);
            int ty = (int)((mouse.y - GRID_ORIGIN_Y) / WORLD_TILE_PX);
            if (tx >= 0 && tx < WORLD_WIDTH && ty >= 0 && ty < WORLD_HEIGHT &&
                world->tiles[ty][tx] != TILE_OBSTACLE)
            {
                CatBody *body = (dragging == 0) ? &bodyA : &bodyB;
                CatView *view = (dragging == 0) ? &viewA : &viewB;
                body->x = tx;
                body->y = ty;
                view->x = (mouse.x - GRID_ORIGIN_X - WORLD_TILE_PX * 0.5f) / WORLD_TILE_PX;
                view->y = (mouse.y - GRID_ORIGIN_Y - WORLD_TILE_PX * 0.5f) / WORLD_TILE_PX;
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            if (dragging >= 0 && !dragMoved)
            {
                CatView *view = (dragging == 0) ? &viewA : &viewB;
                CatAgent *agent = (dragging == 0) ? agentA : agentB;
                MoodPet(view);
                HeartsSpawn(GRID_ORIGIN_X + view->x * WORLD_TILE_PX + WORLD_TILE_PX * 0.5f,
                            GRID_ORIGIN_Y + view->y * WORLD_TILE_PX);
                NetworkApplyReward(&agent->net, PET_REWARD);
            }
            dragging = -1;
        }

        if (++frame >= SIM_FRAME_INTERVAL)
        {
            frame = 0;
            float nva = voiceA, nvb = voiceB;
            if (dragging != 0)
            {
                if (viewA.asleep) { AgentRest(agentA); restTick(&bodyA); }
                else AgentAct(agentA, world, &bodyA, bodyB.x, bodyB.y, voiceB, true, NULL, &nva);
            }
            if (dragging != 1)
            {
                if (viewB.asleep) { AgentRest(agentB); restTick(&bodyB); }
                else AgentAct(agentB, world, &bodyB, bodyA.x, bodyA.y, voiceA, true, NULL, &nvb);
            }
            voiceA = nva; voiceB = nvb;
        }

        if (dragging != 0) ViewUpdate(&viewA, &bodyA);
        if (dragging != 1) ViewUpdate(&viewB, &bodyB);
        MoodUpdate(&viewA, agentA, world, &bodyA, bodyB.x, bodyB.y, dt);
        MoodUpdate(&viewB, agentB, world, &bodyB, bodyA.x, bodyA.y, dt);
        updateSleep(&viewA, &bodyA, &awakeTimerA, &napTimerA, dt);
        updateSleep(&viewB, &bodyB, &awakeTimerB, &napTimerB, dt);
        HeartsUpdate(dt);

        RenderScene(agentA, &bodyA, &viewA, &catA, voiceA,
                    agentB, &bodyB, &viewB, &catB, voiceB, world, showBrain, GetTime());
    }

    PixelCatUnload(&catA);
    PixelCatUnload(&catB);
    free(agentA); free(agentB); free(world);
    CloseWindow();
    return 0;
}
