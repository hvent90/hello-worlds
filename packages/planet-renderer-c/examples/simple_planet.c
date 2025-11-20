#include "planet.h"
#include <raylib.h>
#include <raymath.h>

void UpdateCameraControls(Camera3D* camera) {
    float speed = 2.0f;
    if (IsKeyDown(KEY_LEFT_SHIFT)) speed = 20.0f;
    if (IsKeyDown(KEY_LEFT_CONTROL)) speed = 0.5f;

    Vector3 movement = { 0 };
    if (IsKeyDown(KEY_W)) movement.x = speed;
    if (IsKeyDown(KEY_S)) movement.x = -speed;
    if (IsKeyDown(KEY_D)) movement.y = speed;
    if (IsKeyDown(KEY_A)) movement.y = -speed;
    if (IsKeyDown(KEY_E)) movement.z = speed;
    if (IsKeyDown(KEY_Q)) movement.z = -speed;

    Vector3 rotation = { 0 };
    rotation.x = GetMouseDelta().x * 0.05f;
    rotation.y = GetMouseDelta().y * 0.05f;

    UpdateCameraPro(camera, movement, rotation, 0.0f);
}

int main(void) {
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "Planet Renderer C - Simple Planet");
    DisableCursor();

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 500.0f, 500.0f, 500.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Create Planet
    // Radius 1000
    // Min cell size 50 (determines max detail)
    // Resolution 16 (vertices per chunk edge)
    Planet* planet = Planet_Create(200.0f, 50.0f, 16, (Vector3){0, 0, 0});

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        // Update
        UpdateCameraControls(&camera);
        
        Planet_Update(planet, camera.position);

        // Draw
        BeginDrawing();
            ClearBackground(RAYWHITE);

            BeginMode3D(camera);
                Planet_Draw(planet);
                DrawGrid(100, 100.0f);
            EndMode3D();

            DrawFPS(10, 10);
            DrawText("Use WASD + Mouse to move", 10, 30, 20, DARKGRAY);
        EndDrawing();
    }

    Planet_Free(planet);
    CloseWindow();

    return 0;
}
