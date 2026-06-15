#include "cat/pixel_cat.h"
#include "config.h"
#include <math.h>
#include <stdbool.h>

#define CANVAS CAT_CANVAS_SIZE

#define HEAD_CX 7.5f
#define HEAD_CY 6.0f
#define HEAD_RX 4.3f
#define HEAD_RY 3.3f

#define BODY_CY 12.8f
#define BODY_RX_BASE 2.6f
#define BODY_RX_GAIN 0.9f
#define BODY_RY 2.9f

#define EAR_BASE_Y 3.9f
#define EAR_SPREAD 1.6f

#define MARKING_GAIN 0.22f

static const Color NOSE_COLOR = (Color){ 240, 150, 165, 255 };
static const Color PUPIL_COLOR = (Color){ 22, 22, 30, 255 };
static const Color GLINT_COLOR = (Color){ 250, 250, 255, 255 };
static const Color OUTLINE_COLOR = (Color){ 28, 24, 42, 255 };

typedef struct {
    float earTipY;
    float earSpread;
    bool wideEyes;
    bool squintEyes;
} EmotionStyle;

static EmotionStyle styleFor(CatEmotion emotion)
{
    switch (emotion)
    {
        case EMOTION_CURIOUS: return (EmotionStyle){ -1.8f, 0.4f, false, false };
        case EMOTION_SCARED:  return (EmotionStyle){ 0.6f, -0.7f, true, false };
        case EMOTION_HAPPY:   return (EmotionStyle){ -1.0f, 0.0f, false, true };
        case EMOTION_HUNGRY:  return (EmotionStyle){ -0.2f, -0.3f, false, false };
        default:              return (EmotionStyle){ -1.0f, 0.0f, false, false };
    }
}

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

static bool inEars(float px, float py, float earAngle, float tipY, float *outIsInner)
{
    float leftTipX = 4.4f - earAngle * EAR_SPREAD;
    float leftBaseL = 3.8f;
    float leftBaseR = 5.7f;
    float rightTipX = CANVAS - leftTipX;
    float rightBaseL = CANVAS - leftBaseR;
    float rightBaseR = CANVAS - leftBaseL;

    bool left = inTriangle(px, py, leftTipX, tipY, leftBaseL, EAR_BASE_Y, leftBaseR, EAR_BASE_Y);
    bool right = inTriangle(px, py, rightTipX, tipY, rightBaseL, EAR_BASE_Y, rightBaseR, EAR_BASE_Y);

    bool innerLeft = inTriangle(px, py, leftTipX + 0.5f, tipY + 1.4f, 4.3f, EAR_BASE_Y - 0.4f, 5.5f, EAR_BASE_Y - 0.4f);
    bool innerRight = inTriangle(px, py, rightTipX - 0.5f, tipY + 1.4f, CANVAS - 5.5f, EAR_BASE_Y - 0.4f, CANVAS - 4.3f, EAR_BASE_Y - 0.4f);

    *outIsInner = (innerLeft || innerRight) ? 1.0f : 0.0f;
    return left || right;
}

static void drawFace(Image *canvas, const CatGenome *genome, const EmotionStyle *style)
{
    if (style->squintEyes)
    {
        ImageDrawPixel(canvas, 4, 6, PUPIL_COLOR);
        ImageDrawPixel(canvas, 5, 6, PUPIL_COLOR);
        ImageDrawPixel(canvas, 10, 6, PUPIL_COLOR);
        ImageDrawPixel(canvas, 11, 6, PUPIL_COLOR);
    }
    else if (style->wideEyes)
    {
        ImageDrawPixel(canvas, 4, 5, genome->eyeColor);
        ImageDrawPixel(canvas, 5, 5, genome->eyeColor);
        ImageDrawPixel(canvas, 4, 6, PUPIL_COLOR);
        ImageDrawPixel(canvas, 5, 6, PUPIL_COLOR);
        ImageDrawPixel(canvas, 10, 5, genome->eyeColor);
        ImageDrawPixel(canvas, 11, 5, genome->eyeColor);
        ImageDrawPixel(canvas, 10, 6, PUPIL_COLOR);
        ImageDrawPixel(canvas, 11, 6, PUPIL_COLOR);
        ImageDrawPixel(canvas, 4, 5, GLINT_COLOR);
        ImageDrawPixel(canvas, 10, 5, GLINT_COLOR);
    }
    else
    {
        ImageDrawPixel(canvas, 5, 6, genome->eyeColor);
        ImageDrawPixel(canvas, 10, 6, genome->eyeColor);
        ImageDrawPixel(canvas, 5, 5, GLINT_COLOR);
        ImageDrawPixel(canvas, 10, 5, GLINT_COLOR);
    }

    ImageDrawPixel(canvas, 7, 8, NOSE_COLOR);
    ImageDrawPixel(canvas, 8, 8, NOSE_COLOR);
}

static void addOutline(Image *canvas)
{
    Color *data = (Color *)canvas->data;
    bool filled[CANVAS][CANVAS];
    for (int y = 0; y < CANVAS; y++)
        for (int x = 0; x < CANVAS; x++)
            filled[y][x] = data[y * CANVAS + x].a > 0;

    for (int y = 0; y < CANVAS; y++)
        for (int x = 0; x < CANVAS; x++)
        {
            if (filled[y][x]) continue;
            bool adj = (x > 0 && filled[y][x - 1]) || (x < CANVAS - 1 && filled[y][x + 1]) ||
                       (y > 0 && filled[y - 1][x]) || (y < CANVAS - 1 && filled[y + 1][x]);
            if (adj) ImageDrawPixel(canvas, x, y, OUTLINE_COLOR);
        }
}

const char *CatEmotionName(CatEmotion emotion)
{
    switch (emotion)
    {
        case EMOTION_HAPPY: return "happy";
        case EMOTION_CURIOUS: return "curious";
        case EMOTION_SCARED: return "scared";
        case EMOTION_HUNGRY: return "hungry";
        default: return "content";
    }
}

Image CatRenderImage(CatGenome genome, CatEmotion emotion)
{
    EmotionStyle style = styleFor(emotion);
    Image canvas = GenImageColor(CANVAS, CANVAS, BLANK);

    bool tailMask[CANVAS][CANVAS] = { { false } };
    buildTailMask(tailMask, genome.tailLength);

    float bodyRx = BODY_RX_BASE + genome.bodySize * BODY_RX_GAIN;
    float earAngle = genome.earAngle + style.earSpread;

    for (int y = 0; y < CANVAS; y++)
    {
        for (int x = 0; x < CANVAS; x++)
        {
            float px = x + 0.5f;
            float py = y + 0.5f;
            Color color = BLANK;
            bool painted = false;

            float isInnerEar = 0.0f;
            if (inEars(px, py, earAngle, style.earTipY, &isInnerEar))
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
                color = genome.primary;
                painted = true;
            }

            if (painted) ImageDrawPixel(&canvas, x, y, color);
        }
    }

    addOutline(&canvas);
    drawFace(&canvas, &genome, &style);
    return canvas;
}

static Image CatRenderWalk(CatGenome genome, int frame)
{
    Image canvas = GenImageColor(CANVAS, CANVAS, BLANK);

    for (int y = 0; y < CANVAS; y++)
    {
        for (int x = 0; x < CANVAS; x++)
        {
            float px = x + 0.5f;
            float py = y + 0.5f;
            Color color = BLANK;
            bool painted = false;

            if (inTriangle(px, py, 13.0f, 1.6f, 10.6f, 4.8f, 13.8f, 4.8f))
            {
                color = genome.primary;
                painted = true;
            }
            if (inEllipse(px, py, 8.0f, 9.2f, 5.0f, 2.6f))
            {
                color = furColor(x, y, &genome);
                painted = true;
            }
            if (inEllipse(px, py, 12.0f, 7.0f, 2.9f, 2.7f))
            {
                color = genome.primary;
                painted = true;
            }

            if (painted) ImageDrawPixel(&canvas, x, y, color);
        }
    }

    int tailSteps = 3 + (int)(genome.tailLength * 4.0f);
    for (int i = 0; i <= tailSteps; i++)
    {
        float t = (float)i / (float)tailSteps;
        int tx = (int)(3.5f - t * 2.6f);
        int ty = (int)(9.0f - t * (3.0f + genome.tailLength * 3.5f));
        if (tx >= 0 && ty >= 0 && tx < CANVAS && ty < CANVAS)
            ImageDrawPixel(&canvas, tx, ty, genome.primary);
    }

    const int legX[4] = { 4, 6, 10, 12 };
    for (int i = 0; i < 4; i++)
    {
        bool down = ((i + frame) % 2) == 0;
        Color leg = furColor(legX[i], 12, &genome);
        ImageDrawPixel(&canvas, legX[i], 12, leg);
        ImageDrawPixel(&canvas, legX[i], down ? 13 : 12, leg);
    }

    addOutline(&canvas);

    ImageDrawPixel(&canvas, 13, 3, genome.secondary);
    ImageDrawPixel(&canvas, 13, 7, genome.eyeColor);
    ImageDrawPixel(&canvas, 13, 6, GLINT_COLOR);
    ImageDrawPixel(&canvas, 14, 8, NOSE_COLOR);

    return canvas;
}

static Image CatRenderLie(CatGenome genome)
{
    Image canvas = GenImageColor(CANVAS, CANVAS, BLANK);

    for (int y = 0; y < CANVAS; y++)
    {
        for (int x = 0; x < CANVAS; x++)
        {
            float px = x + 0.5f;
            float py = y + 0.5f;
            Color color = BLANK;
            bool painted = false;

            if (inTriangle(px, py, 13.0f, 5.2f, 10.8f, 7.8f, 13.8f, 7.8f))
            {
                color = genome.primary;
                painted = true;
            }
            if (inEllipse(px, py, 7.5f, 11.2f, 5.6f, 2.4f))
            {
                color = furColor(x, y, &genome);
                painted = true;
            }
            if (inEllipse(px, py, 12.0f, 9.8f, 2.7f, 2.5f))
            {
                color = genome.primary;
                painted = true;
            }

            if (painted) ImageDrawPixel(&canvas, x, y, color);
        }
    }

    int tail[4][2] = { { 2, 11 }, { 1, 10 }, { 1, 9 }, { 2, 8 } };
    for (int i = 0; i < 4; i++) ImageDrawPixel(&canvas, tail[i][0], tail[i][1], genome.primary);

    addOutline(&canvas);

    ImageDrawPixel(&canvas, 12, 9, PUPIL_COLOR);
    ImageDrawPixel(&canvas, 13, 9, PUPIL_COLOR);
    ImageDrawPixel(&canvas, 14, 10, NOSE_COLOR);

    return canvas;
}

PixelCat PixelCatCreate(CatGenome genome)
{
    PixelCat cat;
    cat.genome = genome;
    for (int emotion = 0; emotion < EMOTION_COUNT; emotion++)
    {
        Image canvas = CatRenderImage(genome, (CatEmotion)emotion);
        cat.textures[emotion] = LoadTextureFromImage(canvas);
        SetTextureFilter(cat.textures[emotion], TEXTURE_FILTER_POINT);
        UnloadImage(canvas);
    }
    for (int frame = 0; frame < 2; frame++)
    {
        Image canvas = CatRenderWalk(genome, frame);
        cat.walk[frame] = LoadTextureFromImage(canvas);
        SetTextureFilter(cat.walk[frame], TEXTURE_FILTER_POINT);
        UnloadImage(canvas);
    }
    {
        Image canvas = CatRenderLie(genome);
        cat.lie = LoadTextureFromImage(canvas);
        SetTextureFilter(cat.lie, TEXTURE_FILTER_POINT);
        UnloadImage(canvas);
    }
    return cat;
}

void PixelCatDraw(const PixelCat *cat, Vector2 position, float scale, CatEmotion emotion, bool flipX)
{
    Texture2D texture = cat->textures[emotion];
    Rectangle source = { 0.0f, 0.0f, (flipX ? -1.0f : 1.0f) * (float)texture.width, (float)texture.height };
    Rectangle dest = { position.x, position.y, texture.width * scale, texture.height * scale };
    DrawTexturePro(texture, source, dest, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
}

void PixelCatUnload(PixelCat *cat)
{
    for (int emotion = 0; emotion < EMOTION_COUNT; emotion++)
        UnloadTexture(cat->textures[emotion]);
    for (int frame = 0; frame < 2; frame++)
        UnloadTexture(cat->walk[frame]);
    UnloadTexture(cat->lie);
}
