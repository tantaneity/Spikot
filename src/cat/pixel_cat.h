#ifndef SPIKOT_CAT_PIXEL_CAT_H
#define SPIKOT_CAT_PIXEL_CAT_H

#include "raylib.h"
#include "cat/genome.h"

typedef struct {
    CatGenome genome;
    Texture2D texture;
} PixelCat;

Image CatRenderImage(CatGenome genome);
PixelCat PixelCatCreate(CatGenome genome);
void PixelCatDraw(const PixelCat *cat, Vector2 position, float scale);
void PixelCatUnload(PixelCat *cat);

#endif
