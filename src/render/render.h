#ifndef SPIKOT_RENDER_RENDER_H
#define SPIKOT_RENDER_RENDER_H

#include "raylib.h"
#include "cat/pixel_cat.h"
#include "env/world.h"
#include "agent/agent.h"

#define GRID_ORIGIN_X 40
#define GRID_ORIGIN_Y 40

typedef struct {
    float x;
    float y;
    bool faceLeft;
    CatEmotion mood;
    float moodHold;
    int pets;
} CatView;

void ViewUpdate(CatView *view, const CatBody *body);
void MoodUpdate(CatView *view, const CatAgent *agent, const World *world,
                const CatBody *body, int otherX, int otherY, float dt);
void MoodPet(CatView *view);

void HeartsSpawn(float x, float y);
void HeartsUpdate(float dt);

void RenderScene(const CatAgent *agentA, const CatBody *bodyA, const CatView *viewA, const PixelCat *catA, float voiceA,
                 const CatAgent *agentB, const CatBody *bodyB, const CatView *viewB, const PixelCat *catB, float voiceB,
                 const World *world, bool showBrain, double time);

#endif
