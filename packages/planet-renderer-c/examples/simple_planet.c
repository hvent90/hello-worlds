#include "planet.h"
#include <raylib.h>
#include <raymath.h>
#include "rlgl.h"
#include <stdio.h>

// Simple lighting shader - vertex shader
const char* lightingVS = "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "out vec3 fragNormal;\n"
    "out vec3 fragPosition;\n"
    "void main() {\n"
    "    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));\n"
    "    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

// Simple lighting shader - fragment shader
const char* lightingFS = "#version 330\n"
    "in vec3 fragNormal;\n"
    "in vec3 fragPosition;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 lightDir;\n"
    "uniform vec3 viewPos;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 normal = normalize(fragNormal);\n"
    "    vec3 lightDirection = normalize(lightDir);\n"
    "    \n"
    "    // Ambient lighting\n"
    "    float ambient = 0.0;\n"
    "    \n"
    "    // Diffuse lighting\n"
    "    float diff = max(dot(normal, lightDirection), 0.0);\n"
    "    \n"
    "    // Combine lighting\n"
    "    float lighting = ambient + diff * 0.7;\n"
    "    \n"
    "    finalColor = vec4(colDiffuse.rgb * lighting, colDiffuse.a);\n"
    "}\n";

float UpdateCameraFlight(Camera3D* camera) {
    // Earth-scale speeds (in meters)
    static float speedMultiplier = 1.0f;
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        speedMultiplier *= (wheel > 0 ? 1.1f : 0.9f);
    }

    float speed = 1000.0f * speedMultiplier;  // Base 1 km/frame
    if (IsKeyDown(KEY_LEFT_SHIFT)) speed = 10000.0f * speedMultiplier;  // 10x boost
    if (IsKeyDown(KEY_LEFT_CONTROL)) speed = 100.0f * speedMultiplier;  // 0.1x slow
    
    // Rotation speed
    float rotSpeed = 0.003f; // Increased sensitivity
    float rollSpeed = 0.05f;

    // 1. Get current basis vectors
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    Vector3 up = Vector3Normalize(camera->up);
    Vector3 right = Vector3CrossProduct(forward, up);
    
    // Re-orthogonalize up to ensure stability
    up = Vector3CrossProduct(right, forward);

    // 2. Handle Rotation Inputs
    Vector2 mouseDelta = GetMouseDelta();
    float yawInput = -mouseDelta.x * rotSpeed;
    float pitchInput = -mouseDelta.y * rotSpeed;
    float rollInput = 0.0f;
    if (IsKeyDown(KEY_Q)) rollInput -= rollSpeed;
    if (IsKeyDown(KEY_E)) rollInput += rollSpeed;

    // Create quaternions for each rotation axis relative to current basis
    Quaternion qYaw = QuaternionFromAxisAngle(up, yawInput);
    Quaternion qPitch = QuaternionFromAxisAngle(right, pitchInput);
    Quaternion qRoll = QuaternionFromAxisAngle(forward, rollInput);

    // Combine rotations: Roll * Pitch * Yaw
    Quaternion qRot = QuaternionMultiply(qPitch, qYaw);
    qRot = QuaternionMultiply(qRoll, qRot);

    // Apply rotation to basis vectors
    forward = Vector3RotateByQuaternion(forward, qRot);
    up = Vector3RotateByQuaternion(up, qRot);
    right = Vector3CrossProduct(forward, up);

    // 3. Handle Movement Inputs
    Vector3 move = { 0 };
    if (IsKeyDown(KEY_W)) move = Vector3Add(move, forward);
    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, forward);
    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
    if (IsKeyDown(KEY_SPACE)) move = Vector3Add(move, up);
    if (IsKeyDown(KEY_LEFT_CONTROL)) move = Vector3Subtract(move, up);

    // Normalize move vector to prevent faster diagonal movement, then apply speed
    if (Vector3Length(move) > 0) {
        move = Vector3Normalize(move);
        move = Vector3Scale(move, speed);
        camera->position = Vector3Add(camera->position, move);
    }

    // 4. Update Camera State
    camera->up = up;
    // IMPORTANT: Scale forward vector significantly to avoid floating point precision issues
    // at Earth-scale coordinates (10^7). 1.0f is too small and gets lost in precision.
    camera->target = Vector3Add(camera->position, Vector3Scale(forward, 1000.0f));
    
    return speed;
}

int main(void) {
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "Planet Renderer C - Moon Terrain");
    DisableCursor();

    // Set clip planes for Earth-scale rendering
    // Near: 1 km, Far: 100,000 km (to see the whole planet from space)
    rlSetClipPlanes(.1, 100000000.0);

    float earthRadius = 6371000.0f;  // meters
    float moonRadius = 1737400.0f; // meters

    Camera3D camera = { 0 };
    // Earth radius is ~6,371 km. Spawn at ~10,000 km altitude
    float radius = moonRadius;
    camera.position = (Vector3){ radius * 1.3f, radius * 1.3f, radius * 1.3f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Create Earth-scale Moon
    // Radius: 6,371 km (using Earth radius for now, Moon is ~1,737 km)
    // Min cell size: 50 km (determines max detail)
    // Resolution: 32 (vertices per chunk edge for smoother appearance)
    Planet* planet = Planet_Create(radius, 500.0f, 32, (Vector3){0, 0, 0});
    // Moon colors: darker gray surface with subtle wireframe
    planet->surfaceColor = (Color){120, 120, 120, 255}; // Dark gray for moon surface
    planet->wireframeColor = (Color){80, 80, 80, 255}; // Darker gray wireframe

    // Load lighting shader
    Shader lightingShader = LoadShaderFromMemory(lightingVS, lightingFS);
    int lightDirLoc = GetShaderLocation(lightingShader, "lightDir");
    int viewPosLoc = GetShaderLocation(lightingShader, "viewPos");

    // Set light direction (from sun - pointing toward origin from upper right)
    Vector3 lightDir = Vector3Normalize((Vector3){0.5f, 0.8f, 0.3f});
    SetShaderValue(lightingShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

    // Assign shader to planet
    planet->lightingShader = lightingShader;

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        // Update
        float currentSpeed = UpdateCameraFlight(&camera);
        
        Planet_Update(planet, camera.position);

        // Draw
        BeginDrawing();
            ClearBackground(BLACK);

            BeginMode3D(camera);
                int triangles = Planet_Draw(planet);
                // Grid at Earth scale (100 km spacing, 20 lines)
                // DrawGrid(20, 100000.0f);
            EndMode3D();

            DrawFPS(10, 10);
            
            // Calculate altitude (distance from surface)
            float distanceFromCenter = Vector3Length(camera.position);
            float altitude = distanceFromCenter - radius;
            
            // Display altitude in appropriate units
            if (altitude >= 1000.0f) {
                DrawText(TextFormat("Altitude: %.1f km", altitude / 1000.0f), 10, 30, 20, LIME);
            } else {
                DrawText(TextFormat("Altitude: %.0f m", altitude), 10, 30, 20, LIME);
            }
            
            // Display Speed
            float speedPerSec = currentSpeed * 60.0f; // Assuming 60fps target for "per second" estimation or use GetFPS()
            if (speedPerSec >= 1000.0f) {
                DrawText(TextFormat("Speed: %.1f km/s", speedPerSec / 1000.0f), 10, 50, 20, SKYBLUE);
            } else {
                DrawText(TextFormat("Speed: %.0f m/s", speedPerSec), 10, 50, 20, SKYBLUE);
            }

            // Format triangles with commas
            // Simple manual formatting for now
            char triStr[32];
            if (triangles < 1000) sprintf(triStr, "%d", triangles);
            else if (triangles < 1000000) sprintf(triStr, "%d,%03d", triangles / 1000, triangles % 1000);
            else sprintf(triStr, "%d,%03d,%03d", triangles / 1000000, (triangles / 1000) % 1000, triangles % 1000);

            DrawText(TextFormat("Triangles: %s", triStr), 10, 70, 20, YELLOW);
            
            DrawText("WASD: Move | Q/E: Roll | Space/Ctrl: Up/Down | Shift: Fast | Wheel: Speed", 10, 100, 16, DARKGRAY);
        EndDrawing();
    }

    Planet_Free(planet);
    CloseWindow();

    return 0;
}
