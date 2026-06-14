#include "raylib.h"
#include "config.h"
#include "cat/genome.h"
#include "cat/pixel_cat.h"
#include "snn/network.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define EXPORT_FLAG "--export"
#define EXPORT_COUNT 8
#define EXPORT_DIR "export"

#define SNN_TEST_FLAG "--snn-test"
#define SNN_TEST_TICKS 800
#define SNN_TEST_REPORT_EVERY 100
#define SNN_TEST_INPUT_NEURONS 24
#define SNN_TEST_INPUT_PROB 0.6f
#define SNN_TEST_INPUT_DRIVE 1.4f

static uint32_t testRand(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static double weightSum(const Network *network)
{
    double total = 0.0;
    for (int pre = 0; pre < SNN_NEURON_COUNT; pre++)
        for (int post = 0; post < SNN_NEURON_COUNT; post++)
            total += network->weights[pre][post];
    return total;
}

static int runSnnTest(void)
{
    Network *network = malloc(sizeof(Network));
    if (!network)
    {
        fprintf(stderr, "failed to allocate network\n");
        return 1;
    }

    NetworkInit(network, 12345u);
    double before = weightSum(network);

    float external[SNN_NEURON_COUNT];
    uint32_t state = 999u;
    long totalSpikes = 0;

    for (int tick = 0; tick < SNN_TEST_TICKS; tick++)
    {
        for (int j = 0; j < SNN_NEURON_COUNT; j++) external[j] = 0.0f;
        for (int j = 0; j < SNN_TEST_INPUT_NEURONS; j++)
            if ((testRand(&state) >> 8) * (1.0f / 16777216.0f) < SNN_TEST_INPUT_PROB)
                external[j] = SNN_TEST_INPUT_DRIVE;

        NetworkStep(network, external, 1.0f);
        int spikes = NetworkSpikeCount(network);
        totalSpikes += spikes;

        if (tick % SNN_TEST_REPORT_EVERY == 0)
            printf("tick %4d   spikes %3d\n", tick, spikes);
    }

    double after = weightSum(network);
    printf("---\n");
    printf("neurons        %d\n", SNN_NEURON_COUNT);
    printf("ticks          %d\n", SNN_TEST_TICKS);
    printf("avg spikes/tick %.2f\n", (double)totalSpikes / SNN_TEST_TICKS);
    printf("weight sum     %.4f -> %.4f  (delta %+.4f)\n", before, after, after - before);

    free(network);
    return 0;
}

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
    if (argc > 1 && strcmp(argv[1], SNN_TEST_FLAG) == 0)
        return runSnnTest();

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
