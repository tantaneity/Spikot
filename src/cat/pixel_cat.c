#include "cat/pixel_cat.h"
#include "config.h"
#include <math.h>
#include <stdbool.h>

#define CANVAS CAT_CANVAS_SIZE

#define HEAD_CX 7.5f
#define HEAD_CY 6.4f
#define HEAD_RX 4.2f
#define HEAD_RY 3.3f

#define BODY_CY 12.5f
#define BODY_RX_BASE 3.1f
#define BODY_RX_GAIN 1.3f
#define BODY_RY 3.5f

#define EAR_BASE_Y 3.6f
#define EAR_SPREAD 1.8f

#define MARKING_GAIN 0.42f

static const Color NOSE_COLOR = (Color){ 240, 150, 165, 255 };
static const Color PUPIL_COLOR = (Color){ 22, 22, 30, 255 };

static bool inEllipse(float x, float y, float cx, float cy, float rx, float ry)
{
    float dx = (x - cx) / rx;
    float dy = (y - cy) / ry;
    return dx * dx + dy * dy <= 1.0f;
}

static float edgeSign(float px, float py, float ax, float ay, float bx, float by)
{
    return (px - bx) * (ay - by) - (ax - bx) * (py - by);
}

static bool inTriangle(float px, float py,
                       float ax, float ay, float bx, float by, float cx, float cy)
{
    float d1 = edgeSign(px, py, ax, ay, bx, by);
    float d2 = edgeSign(px, py, bx, by, cx, cy);
    float d3 = edgeSign(px, py, cx, cy, ax, ay);
    bool hasNegative = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    bool hasPositive = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(hasNegative && hasPositive);
}

static float hashCell(int x, int y, uint32_t seed)
{
    uint32_t h = seed + (uint32_t)(x * 374761393) + (uint32_t)(y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFFu) / (float)0x1000000;
}

static Color furColor(int x, int y, const CatGenome *genome)
{
    if (hashCell(x, y, genome->seed) < genome->furDensity * MARKING_GAIN)
        return genome->secondary;
    return genome->primary;
}

static void buildTailMask(bool mask[CANVAS][CANVAS], float tailLength)
{
    int steps = (int)(tailLength * 10.0f) + 4;
    for (int i = 0; i <= steps; i++)
    {
        float t = (float)i / (float)steps;
        float tx = 11.4f + 2.7f * sinf(t * 1.7f);
        float ty = 13.0f - t * (2.0f + tailLength * 8.5f);
        for (int oy = -1; oy <= 0; oy++)
            for (int ox = 0; ox <= 1; ox++)
            {
                int px = (int)(tx + ox);
                int py = (int)(ty + oy);
                if (px >= 0 && px < CANVAS && py >= 0 && py < CANVAS)
                    mask[py][px] = true;
            }
    }
}

static bool inEars(float px, float py, float earAngle,
                   float *outIsInner)
{
    float leftTipX = 4.3f - earAngle * EAR_SPREAD;
    float leftBaseL = 3.4f;
    float leftBaseR = 6.2f;
    float rightTipX = CANVAS - leftTipX;
    float rightBaseL = CANVAS - leftBaseR;
    float rightBaseR = CANVAS - leftBaseL;

    bool left = inTriangle(px, py, leftTipX, -0.4f, leftBaseL, EAR_BASE_Y, leftBaseR, EAR_BASE_Y);
    bool right = inTriangle(px, py, rightTipX, -0.4f, rightBaseL, EAR_BASE_Y, rightBaseR, EAR_BASE_Y);

    float innerLeftTipX = leftTipX + 0.5f;
    float innerRightTipX = rightTipX - 0.5f;
    bool innerLeft = inTriangle(px, py, innerLeftTipX, 1.1f, 4.3f, EAR_BASE_Y - 0.4f, 5.5f, EAR_BASE_Y - 0.4f);
    bool innerRight = inTriangle(px, py, innerRightTipX, 1.1f, CANVAS - 5.5f, EAR_BASE_Y - 0.4f, CANVAS - 4.3f, EAR_BASE_Y - 0.4f);

    *outIsInner = (innerLeft || innerRight) ? 1.0f : 0.0f;
    return left || right;
}

Image CatRenderImage(CatGenome genome)
{
    Image canvas = GenImageColor(CANVAS, CANVAS, BLANK);

    bool tailMask[CANVAS][CANVAS] = { { false } };
    buildTailMask(tailMask, genome.tailLength);

    float bodyRx = BODY_RX_BASE + genome.bodySize * BODY_RX_GAIN;

    for (int y = 0; y < CANVAS; y++)
    {
        for (int x = 0; x < CANVAS; x++)
        {
            float px = x + 0.5f;
            float py = y + 0.5f;
            Color color = BLANK;
            bool painted = false;

            float isInnerEar = 0.0f;
            if (inEars(px, py, genome.earAngle, &isInnerEar))
            {
                color = (isInnerEar > 0.5f) ? genome.secondary : genome.primary;
                painted = true;
            }

            if (tailMask[y][x])
            {
                color = furColor(x, y, &genome);
                painted = true;
            }

            if (inEllipse(px, py, HEAD_CX, BODY_CY, bodyRx, BODY_RY))
            {
                color = furColor(x, y, &genome);
                painted = true;
            }

            if (inEllipse(px, py, HEAD_CX, HEAD_CY, HEAD_RX, HEAD_RY))
            {
                color = furColor(x, y, &genome);
                painted = true;
            }

            if (painted) ImageDrawPixel(&canvas, x, y, color);
        }
    }

    ImageDrawPixel(&canvas, 6, 6, genome.eyeColor);
    ImageDrawPixel(&canvas, 9, 6, genome.eyeColor);
    ImageDrawPixel(&canvas, 6, 7, PUPIL_COLOR);
    ImageDrawPixel(&canvas, 9, 7, PUPIL_COLOR);
    ImageDrawPixel(&canvas, 7, 8, NOSE_COLOR);
    ImageDrawPixel(&canvas, 8, 8, NOSE_COLOR);

    return canvas;
}

PixelCat PixelCatCreate(CatGenome genome)
{
    Image canvas = CatRenderImage(genome);
    PixelCat cat;
    cat.genome = genome;
    cat.texture = LoadTextureFromImage(canvas);
    SetTextureFilter(cat.texture, TEXTURE_FILTER_POINT);
    UnloadImage(canvas);
    return cat;
}

void PixelCatDraw(const PixelCat *cat, Vector2 position, float scale)
{
    DrawTextureEx(cat->texture, position, 0.0f, scale, WHITE);
}

void PixelCatUnload(PixelCat *cat)
{
    UnloadTexture(cat->texture);
}
