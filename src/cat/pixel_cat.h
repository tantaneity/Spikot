#ifndef SPIKOT_CAT_PIXEL_CAT_H
#define SPIKOT_CAT_PIXEL_CAT_H

#include "raylib.h"
#include "cat/genome.h"

typedef enum {
    EMOTION_CONTENT,
    EMOTION_HAPPY,
    EMOTION_CURIOUS,
    EMOTION_SCARED,
    EMOTION_HUNGRY,
    EMOTION_COUNT
} CatEmotion;

typedef struct {
    CatGenome genome;
    Texture2D textures[EMOTION_COUNT];
    Texture2D walk[2];
    Texture2D lie;
} PixelCat;

const char *CatEmotionName(CatEmotion emotion);
Image CatRenderImage(CatGenome genome, CatEmotion emotion);
PixelCat PixelCatCreate(CatGenome genome);
void PixelCatDraw(const PixelCat *cat, Vector2 position, float scale, CatEmotion emotion, bool flipX);
void PixelCatUnload(PixelCat *cat);

#endif
