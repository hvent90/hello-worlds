#include "planet.h"
#include <raylib.h>
#include <raymath.h>
#include "rlgl.h"

void UpdateCameraControls(Camera3D* camera) {
    // Earth-scale speeds (in meters)
    float speed = 1000.0f;  // 1 km/frame at 60fps = 60 km/s
    if (IsKeyDown(KEY_LEFT_SHIFT)) speed = 10000.0f;  // 10 km/frame = 600 km/s
    if (IsKeyDown(KEY_LEFT_CONTROL)) speed = 100.0f;  // 100 m/frame = 6 km/s

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

    // Set clip planes for Earth-scale rendering
    // Near: 1 km, Far: 100,000 km (to see the whole planet from space)
    rlSetClipPlanes(.1, 100000000.0);

    Camera3D camera = { 0 };
    // Earth radius is ~6,371 km. Spawn at ~10,000 km altitude
    float earthRadius = 6371000.0f;  // meters
    camera.position = (Vector3){ earthRadius * 1.5f, earthRadius * 1.5f, earthRadius * 1.5f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Create Earth-scale Planet
    // Radius: 6,371 km (Earth)
    // Min cell size: 50 km (determines max detail)
    // Resolution: 32 (vertices per chunk edge for smoother appearance)
    Planet* planet = Planet_Create(earthRadius, 50000.0f, 32, (Vector3){0, 0, 0});
    planet->surfaceColor = BLUE;
    planet->wireframeColor = GREEN;

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
                // Grid at Earth scale (100 km spacing, 20 lines)
                // DrawGrid(20, 100000.0f);
            EndMode3D();

            DrawFPS(10, 10);
            
            // Calculate altitude (distance from surface)
            float distanceFromCenter = Vector3Length(camera.position);
            float altitude = distanceFromCenter - earthRadius;
            
            // Display altitude in appropriate units
            if (altitude >= 1000.0f) {
                DrawText(TextFormat("Altitude: %.1f km", altitude / 1000.0f), 10, 30, 20, LIME);
            } else {
                DrawText(TextFormat("Altitude: %.0f m", altitude), 10, 30, 20, LIME);
            }
            
            DrawText("WASD + Mouse: Move | Shift: Sprint | Ctrl: Slow", 10, 60, 16, DARKGRAY);
        EndDrawing();
    }

    Planet_Free(planet);
    CloseWindow();

    return 0;
}
