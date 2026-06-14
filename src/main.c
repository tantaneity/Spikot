#include "raylib.h"
#include "config.h"

static void renderFrame(void)
{
    BeginDrawing();
    ClearBackground(BACKGROUND_COLOR);

    DrawText(WINDOW_TITLE, 24, 24, 32, RAYWHITE);
    DrawText("a cat with a spiking brain, learning while you watch",
             24, 64, 18, GRAY);
    DrawFPS(WINDOW_WIDTH - 96, 24);

    EndDrawing();
}

int main(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);
    SetTargetFPS(TARGET_FPS);

    while (!WindowShouldClose())
    {
        renderFrame();
    }

    CloseWindow();
    return 0;
}
