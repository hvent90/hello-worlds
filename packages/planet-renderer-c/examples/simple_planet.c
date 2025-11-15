#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <math.h>

#include "rlgl.h"
#include "../include/planet.h"

// Simple perlin-like noise (very basic)
float SimpleNoise(float x, float y, float z) {
    return sinf(x * 0.01f) * cosf(y * 0.01f) * sinf(z * 0.01f);
}

// Height generator callback
float MyHeightGenerator(Vector3 worldPosition, float radius, void* userData) {
    // Simple noise-based height
    float noise = SimpleNoise(worldPosition.x, worldPosition.y, worldPosition.z);
    float baseHeight = radius * 0.05f; // 5% of radius
    return baseHeight * (noise + 0.5f);
}

// Color generator callback
Color MyColorGenerator(Vector3 worldPosition, float height, void* userData) {
    // Color based on height
    float normalizedHeight = height / (((float*)userData)[0] * 0.05f);

    if (normalizedHeight < 0.3f) {
        return BLUE; // Water
    } else if (normalizedHeight < 0.5f) {
        return BEIGE; // Sand
    } else if (normalizedHeight < 0.8f) {
        return GREEN; // Grass
    } else {
        return WHITE; // Snow
    }
}

int main(void) {
    // Initialization
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "Planet Renderer - Raylib C");

    // Define the camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 300.0f, 300.0f, 300.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Create planet
    float radius = 100.0f;
    float minCellSize = 10.0f;  // Increased from 5.0f to prevent excessive subdivision at close range
    int minCellResolution = 32;

    Planet* planet = Planet_Create(
        radius,
        minCellSize,
        minCellResolution,
        MyHeightGenerator,
        MyColorGenerator,
        &radius // Pass radius as user data for color generator
    );

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

        // Update planet LOD based on camera position
        Planet_Update(planet, camera.position);

        // Draw
        BeginDrawing();
            ClearBackground(BLACK);

            BeginMode3D(camera);
                // Draw planet
                if (showWireframe) {
                    // Note: Wireframe mode in raylib is global
                    rlEnableWireMode();
                }

                Planet_Render(planet);

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
                DrawText(TextFormat("Active Chunks: %d", planet->chunkCount), 10, 70, 20, YELLOW);
                DrawText(TextFormat("Camera: (%.1f, %.1f, %.1f)",
                         camera.position.x, camera.position.y, camera.position.z), 10, 100, 20, WHITE);

                DrawText("Controls:", 10, 140, 16, LIGHTGRAY);
                DrawText("  WASD + Mouse: Move camera", 10, 160, 14, LIGHTGRAY);
                DrawText("  F: Toggle wireframe", 10, 180, 14, LIGHTGRAY);
                DrawText("  I: Toggle info", 10, 200, 14, LIGHTGRAY);
                DrawText("  ESC: Exit", 10, 220, 14, LIGHTGRAY);
            }

        EndDrawing();
    }

    // Cleanup
    Planet_Destroy(planet);
    CloseWindow();

    return 0;
}
