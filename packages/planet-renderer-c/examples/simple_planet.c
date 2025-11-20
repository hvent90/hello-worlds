#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <math.h>

#include "rlgl.h"
#include "../include/planet.h"

int main(void) {
    // Initialization
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "Planet Renderer - Raylib C");

    DisableCursor();

    // Define the camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 300.0f, 300.0f, 300.0f };
    // For Earth-scale, use: camera.position = (Vector3){ 6357000.0f * 1.5f, 6357000.0f * 1.5f, 6357000.0f * 1.5f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Create planet
    // Example 1: Small-scale planet (good for testing)
    float radius = 100.0f;
    float minCellSize = 10.0f;  // Increased from 5.0f to prevent excessive subdivision at close range
    int minCellResolution = 32;

    SetTargetFPS(60);

    bool showWireframe = false;
    bool showInfo = true;

    // Main game loop
    while (!WindowShouldClose()) {
        // Update
        UpdateCamera(&camera, CAMERA_FREE);

        // Toggle wireframe with F key
        if (IsKeyPressed(KEY_F)) {
            showWireframe = !showWireframe;
        }

        // Toggle info with I key
        if (IsKeyPressed(KEY_I)) {
            showInfo = !showInfo;
        }

        // Draw
        BeginDrawing();
            ClearBackground(BLACK);

            BeginMode3D(camera);
                // Draw planet
                if (showWireframe) {
                    // Note: Wireframe mode in raylib is global
                    rlEnableWireMode();
                }

                if (showWireframe) {
                    rlDisableWireMode();
                }

                // Draw grid for reference
                DrawGrid(20, 10.0f);

                // Draw coordinate axes
                DrawLine3D((Vector3){0, 0, 0}, (Vector3){50, 0, 0}, RED);
                DrawLine3D((Vector3){0, 0, 0}, (Vector3){0, 50, 0}, GREEN);
                DrawLine3D((Vector3){0, 0, 0}, (Vector3){0, 0, 50}, BLUE);

            EndMode3D();

            // Draw UI
            if (showInfo) {
                DrawText("Planet Renderer Demo", 10, 10, 20, WHITE);
                DrawText(TextFormat("FPS: %d", GetFPS()), 10, 40, 20, LIME);
                DrawText(TextFormat("Camera: (%.1f, %.1f, %.1f)",
                         camera.position.x, camera.position.y, camera.position.z), 10, 100, 20, WHITE);

                DrawText("Controls:", 10, 180, 16, LIGHTGRAY);
                DrawText("  WASD + Mouse: Move camera", 10, 200, 14, LIGHTGRAY);
                DrawText("  F: Toggle wireframe", 10, 220, 14, LIGHTGRAY);
                DrawText("  I: Toggle info", 10, 240, 14, LIGHTGRAY);
                DrawText("  ESC: Exit", 10, 260, 14, LIGHTGRAY);
            }

        EndDrawing();
    }

    // Cleanup
    CloseWindow();

    return 0;
}
