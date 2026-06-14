#include "raylib.h"
#include "config.h"
#include "cat/genome.h"
#include "cat/pixel_cat.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define EXPORT_FLAG "--export"
#define EXPORT_COUNT 8
#define EXPORT_DIR "export"

static uint32_t nextSeed(void)
{
    return (uint32_t)GetRandomValue(1, 2000000000);
}

static void drawGenomePanel(const CatGenome *genome)
{
    DrawText(WINDOW_TITLE, 24, 24, 32, RAYWHITE);
    DrawText("space: new cat   esc: quit", 24, 62, 18, GRAY);

    char line[128];
    snprintf(line, sizeof(line), "seed       %u", genome->seed);
    DrawText(line, 24, WINDOW_HEIGHT - 140, 18, LIGHTGRAY);
    snprintf(line, sizeof(line), "body %.2f  ears %.2f  tail %.2f  fur %.2f",
             genome->bodySize, genome->earAngle, genome->tailLength, genome->furDensity);
    DrawText(line, 24, WINDOW_HEIGHT - 116, 18, LIGHTGRAY);

    DrawRectangle(24, WINDOW_HEIGHT - 84, 28, 28, genome->primary);
    DrawRectangle(60, WINDOW_HEIGHT - 84, 28, 28, genome->secondary);
    DrawRectangle(96, WINDOW_HEIGHT - 84, 28, 28, genome->eyeColor);
}

static int runExport(void)
{
    SetTraceLogLevel(LOG_WARNING);
    for (int i = 0; i < EXPORT_COUNT; i++)
    {
        CatGenome genome = CatGenomeRandom(nextSeed());
        Image image = CatRenderImage(genome);
        ImageResizeNN(&image, CAT_CANVAS_SIZE * CAT_EXPORT_SCALE, CAT_CANVAS_SIZE * CAT_EXPORT_SCALE);
        char path[128];
        snprintf(path, sizeof(path), "%s/cat_%d_%u.png", EXPORT_DIR, i, genome.seed);
        ExportImage(image, path);
        UnloadImage(image);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], EXPORT_FLAG) == 0)
    {
        InitWindow(1, 1, EXPORT_FLAG);
        int code = runExport();
        CloseWindow();
        return code;
    }

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(TARGET_FPS);
    SetRandomSeed((unsigned int)GetTime() + 1u);

    PixelCat cat = PixelCatCreate(CatGenomeRandom(nextSeed()));

    float catSize = CAT_CANVAS_SIZE * CAT_RENDER_SCALE;
    Vector2 catPosition = {
        (WINDOW_WIDTH - catSize) * 0.5f,
        (WINDOW_HEIGHT - catSize) * 0.5f
    };

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_SPACE))
        {
            PixelCatUnload(&cat);
            cat = PixelCatCreate(CatGenomeRandom(nextSeed()));
        }

        BeginDrawing();
        ClearBackground(BACKGROUND_COLOR);
        PixelCatDraw(&cat, catPosition, CAT_RENDER_SCALE);
        drawGenomePanel(&cat.genome);
        DrawFPS(WINDOW_WIDTH - 96, 24);
        EndDrawing();
    }

    PixelCatUnload(&cat);
    CloseWindow();
    return 0;
}
