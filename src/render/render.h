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
    bool asleep;
    float stretch;
    float curiosity;
} CatView;

void ViewUpdate(CatView *view, const CatBody *body);
void MoodUpdate(CatView *view, const CatAgent *agent, const World *world,
                const CatBody *body, int otherX, int otherY, float dt);
void MoodPet(CatView *view);

void ParticleHeart(float x, float y);
void ParticleCrumbs(float x, float y);
void ParticleDust(float x, float y);
void ParticlesUpdate(float dt);

int PalettePick(Vector2 mouse);

void RenderScene(const CatAgent *agent, const CatBody *body, const CatView *view, const PixelCat *cat,
                 float voice, const World *world, const RoomItem *items, int itemCount,
                 const Stain *stains, int stainCount, int heldItem, bool showBrain, double time);

#endif
