#include "planet.h"
#include "shadow.h"
#include <raylib.h>
#include <raymath.h>
#include "rlgl.h"
#include <stdio.h>


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

void DrawCascadeDebugOverlay(CascadedShadowMap* csm, Camera camera) {
    DrawText("CSM Debug Info:", 10, 130, 20, WHITE);
    Color colors[] = {RED, GREEN, BLUE, YELLOW};
    for (int i = 0; i < CASCADE_COUNT; i++) {
        DrawText(TextFormat("Cascade %d: Split %.1fm", i, csm->cascades[i].splitDistance), 
                 10, 155 + i * 25, 20, colors[i]);
    }
}

// Calculate exact terrain height at a given position
// This duplicates the logic from Chunk_Generate to ensure the HUD matches the visual terrain
float GetTerrainHeightAtPosition(Vector3 position, float planetRadius, float terrainFrequency, float terrainAmplitude) {
    // Normalize to get direction vector (equivalent to projecting onto sphere)
    Vector3 normalized = Vector3Normalize(position);
    
    // Calculate UV/Noise coordinates
    // We assume the camera is over the generated terrain (Z+ face)
    // Project the camera position onto the cube face plane
    
    float absZ = fabsf(normalized.z);
    if (absZ < 0.0001f) absZ = 0.0001f;
    
    // Project to cube face at distance 'radius'
    float projectionScale = planetRadius / absZ;
    float px = normalized.x * projectionScale;
    float py = normalized.y * projectionScale;
    
    // Now calculate noise
    float faceSizeTotal = 2.0f * planetRadius;
    float normalizedX = (px + planetRadius) / faceSizeTotal;
    float normalizedY = (py + planetRadius) / faceSizeTotal;
    
    float noiseX = normalizedX * terrainFrequency;
    float noiseY = normalizedY * terrainFrequency;
    
    float heightNoise = MoonTerrain(noiseX, noiseY);
    float heightVariation = planetRadius * terrainAmplitude * heightNoise;
    
    return planetRadius + heightVariation;
}

int main(void) {
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "Planet Renderer C - Moon Terrain (CSM)");
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
    Planet* planet = Planet_Create(radius, 500.0f, 32, (Vector3){0, 0, 0}, 18.0f, 0.003f);
    // Moon colors: darker gray surface with subtle wireframe
    planet->surfaceColor = (Color){120, 120, 120, 255}; // Dark gray for moon surface
    planet->wireframeColor = (Color){80, 80, 80, 255}; // Darker gray wireframe

    // Store original wireframe color for toggling
    Color originalWireframeColor = planet->wireframeColor;
    bool showWireframe = true;

    // Load lighting shader
    Shader lightingShader = LoadShader("shaders/lighting.vs", "shaders/lighting.fs");
    if (lightingShader.id == 0) {
        printf("ERROR: Failed to load lighting shader!\n");
    }
    int lightDirLoc = GetShaderLocation(lightingShader, "lightDir");
    int viewPosLoc = GetShaderLocation(lightingShader, "viewPos");
    
    // CSM Uniform Locations
    int cascadeShadowMapsLoc = GetShaderLocation(lightingShader, "cascadeShadowMaps");
    int cascadeDistancesLoc = GetShaderLocation(lightingShader, "cascadeDistances");
    // We'll get matrix locations dynamically or pre-fetch them
    int cascadeLightMatricesLocs[CASCADE_COUNT];
    for(int i=0; i<CASCADE_COUNT; i++) {
        cascadeLightMatricesLocs[i] = GetShaderLocation(lightingShader, TextFormat("cascadeLightMatrices[%d]", i));
    }

    // Set light direction (from sun - pointing toward origin from upper right)
    Vector3 lightDir = Vector3Normalize((Vector3){0.5f, 0.8f, 0.3f});
    SetShaderValue(lightingShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

    // Load shadow shader
    Shader shadowShader = LoadShader("shaders/shadow.vs", "shaders/shadow.fs");
    if (shadowShader.id == 0) {
        printf("ERROR: Failed to load shadow shader!\n");
    }
    int shadowLightSpaceMatrixLoc = GetShaderLocation(shadowShader, "lightSpaceMatrix");

    // Create CSM with high resolution for sharp shadows
    CascadedShadowMap* csm = CSM_Create(lightDir, 4096);

    // Assign shader to planet (shadow map texture will be handled per cascade/frame)
    planet->lightingShader = lightingShader;
    // planet->shadowMapTexture = ...; // Not used directly anymore, we bind manually

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        // Update
        float currentSpeed = UpdateCameraFlight(&camera);

        // Toggle wireframe with F key
        if (IsKeyPressed(KEY_F)) {
            showWireframe = !showWireframe;
            planet->wireframeColor = showWireframe ? originalWireframeColor : BLANK;
        }

        Planet_Update(planet, camera.position);

        // Update CSM
        CSM_UpdateCascades(csm, camera, radius, 0.003f);

        // PASS 1: Render all cascade shadow maps
        for (int i = 0; i < CASCADE_COUNT; i++) {
            SetShaderValueMatrix(shadowShader, shadowLightSpaceMatrixLoc,
                               csm->cascades[i].lightSpaceMatrix);

            BeginTextureMode(csm->cascades[i].shadowMap);
                rlClearScreenBuffers();
                rlViewport(0, 0, csm->shadowMapResolution, csm->shadowMapResolution);
                Planet_DrawWithShader(planet, shadowShader);
            EndTextureMode();
        }

        // PASS 2: Normal rendering with shadows
        
        // Upload cascade data to shader
        for (int i = 0; i < CASCADE_COUNT; i++) {
             SetShaderValueMatrix(lightingShader, cascadeLightMatricesLocs[i], csm->cascades[i].lightSpaceMatrix);
        }

        float cascadeDistances[CASCADE_COUNT];
        for (int i = 0; i < CASCADE_COUNT; i++) {
            cascadeDistances[i] = csm->cascades[i].splitDistance;
        }
        SetShaderValueV(lightingShader, cascadeDistancesLoc,
                       cascadeDistances, SHADER_UNIFORM_FLOAT, CASCADE_COUNT);
        
        // Set shadow map samplers (texture units 1, 2, 3, 4)
        int samplers[CASCADE_COUNT] = {1, 2, 3, 4};
        SetShaderValueV(lightingShader, cascadeShadowMapsLoc, samplers, SHADER_UNIFORM_INT, CASCADE_COUNT);

        // Bind all cascade shadow maps
        for (int i = 0; i < CASCADE_COUNT; i++) {
            rlActiveTextureSlot(1 + i);
            rlEnableTexture(csm->cascades[i].shadowMap.depth.id);
        }

        // Update view position
        SetShaderValue(lightingShader, viewPosLoc, &camera.position, SHADER_UNIFORM_VEC3);

        // Draw
        BeginDrawing();
            ClearBackground(BLACK);

            BeginMode3D(camera);
                int triangles = Planet_Draw(planet);
            EndMode3D();
            
            // Reset active texture to 0 to avoid messing up other things
            rlActiveTextureSlot(0);

            DrawFPS(10, 10);

            // Display altitude
            float distFromCenter = Vector3Length(camera.position);
            // Calculate radar altitude (height above actual terrain)
            float terrainHeight = GetTerrainHeightAtPosition(camera.position, moonRadius, 18.0f, 0.003f);
            float radarAltitude = distFromCenter - terrainHeight;

            if (radarAltitude >= 1000.0f) {
                DrawText(TextFormat("Altitude: %.1f km", radarAltitude / 1000.0f), 10, 40, 20, GREEN);
            } else {
                DrawText(TextFormat("Altitude: %.0f m", radarAltitude), 10, 40, 20, GREEN);
            }
            
            // Display Speed
            float speedPerSec = currentSpeed * 60.0f;
            if (speedPerSec >= 1000.0f) {
                DrawText(TextFormat("Speed: %.1f km/s", speedPerSec / 1000.0f), 10, 70, 20, SKYBLUE);
            } else {
                DrawText(TextFormat("Speed: %.0f m/s", speedPerSec), 10, 70, 20, SKYBLUE);
            }

            // Format triangles
            char triStr[32];
            if (triangles < 1000) sprintf(triStr, "%d", triangles);
            else if (triangles < 1000000) sprintf(triStr, "%d,%03d", triangles / 1000, triangles % 1000);
            else sprintf(triStr, "%d,%03d,%03d", triangles / 1000000, (triangles / 1000) % 1000, triangles % 1000);

            DrawText(TextFormat("Triangles: %s", triStr), 10, 70, 20, YELLOW);

            DrawText("WASD: Move | Q/E: Roll | Space/Ctrl: Up/Down | Shift: Fast | Wheel: Speed | F: Wireframe", 10, 100, 16, DARKGRAY);
            
            DrawCascadeDebugOverlay(csm, camera);
        EndDrawing();
    }

    CSM_Destroy(csm);
    Planet_Free(planet);
    CloseWindow();

    return 0;
}
