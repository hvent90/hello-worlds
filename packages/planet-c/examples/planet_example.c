#include <stdio.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "../src/mesh.h"

#define SHADOWMAP_RESOLUTION 1024
#define CASCADE_COUNT 4

// Load render texture for shadowmap projection
static RenderTexture2D LoadShadowmapRenderTexture(int width, int height) {
    RenderTexture2D target = {0};

    target.id = rlLoadFramebuffer(); // Load an empty framebuffer
    target.texture.width = width;
    target.texture.height = height;

    if (target.id > 0) {
        rlEnableFramebuffer(target.id);

        // Create depth texture
        target.depth.id = rlLoadTextureDepth(width, height, false);
        target.depth.width = width;
        target.depth.height = height;
        target.depth.format = 19; // DEPTH_COMPONENT_24BIT?
        target.depth.mipmaps = 1;

        // Attach depth texture to FBO
        rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);

        // Check if fbo is complete with attachments (valid)
        if (rlFramebufferComplete(target.id))
            TRACELOG(LOG_INFO, "FBO: [ID %i] Framebuffer object created successfully", target.id);

        rlDisableFramebuffer();
    } else
        TRACELOG(LOG_WARNING, "FBO: Framebuffer object can not be created");

    return target;
}

float UpdateCameraMovement(Camera3D *camera, const float deltaTime) {
    // Floating pawn control variables (static to persist between frames)
    static float pitch = 0.0f;
    static float yaw = -90.0f; // Start looking toward -Z
    static float roll = 0.0f;
    static float movementSpeed = 50.0f;
    const float minSpeed = 1.0f;
    const float maxSpeed = 500000.0f;
    const float mouseSensitivity = 0.1f;
    const float rollSpeed = 60.0f; // Degrees per second

    // Mouse wheel for speed control (logarithmic scaling)
    float wheelMove = GetMouseWheelMove();
    if (wheelMove != 0.0f) {
        movementSpeed *= (1.0f + wheelMove * 0.2f);
        if (movementSpeed < minSpeed) movementSpeed = minSpeed;
        if (movementSpeed > maxSpeed) movementSpeed = maxSpeed;
    }

    // Mouse look (pitch and yaw)
    Vector2 mouseDelta = GetMouseDelta();
    yaw += mouseDelta.x * mouseSensitivity;
    pitch -= mouseDelta.y * mouseSensitivity;

    // Clamp pitch to prevent gimbal lock
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    // Roll controls (Q/E)
    if (IsKeyDown(KEY_Q)) roll -= rollSpeed * deltaTime;
    if (IsKeyDown(KEY_E)) roll += rollSpeed * deltaTime;

    // Calculate camera forward vector from pitch and yaw
    Vector3 forward = {
        cosf(pitch * DEG2RAD) * cosf(yaw * DEG2RAD),
        sinf(pitch * DEG2RAD),
        cosf(pitch * DEG2RAD) * sinf(yaw * DEG2RAD)
    };
    forward = Vector3Normalize(forward);

    // Calculate right vector (perpendicular to forward and world up)
    Vector3 worldUp = {0.0f, 1.0f, 0.0f};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, worldUp));

    // Calculate camera up vector (perpendicular to forward and right)
    Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));

    // Apply roll rotation to up vector
    if (roll != 0.0f) {
        up = Vector3RotateByAxisAngle(up, forward, roll * DEG2RAD);
        right = Vector3Normalize(Vector3CrossProduct(forward, up));
    }

    // Movement input (camera-relative)
    Vector3 movement = {0.0f, 0.0f, 0.0f};

    if (IsKeyDown(KEY_W)) movement = Vector3Add(movement, forward);
    if (IsKeyDown(KEY_S)) movement = Vector3Subtract(movement, forward);
    if (IsKeyDown(KEY_D)) movement = Vector3Add(movement, right);
    if (IsKeyDown(KEY_A)) movement = Vector3Subtract(movement, right);
    if (IsKeyDown(KEY_SPACE)) movement = Vector3Add(movement, worldUp); // World-relative up
    if (IsKeyDown(KEY_LEFT_CONTROL)) movement = Vector3Subtract(movement, worldUp); // World-relative down

    // Normalize movement vector if moving (to prevent faster diagonal movement)
    if (Vector3Length(movement) > 0.0f) {
        movement = Vector3Normalize(movement);
        movement = Vector3Scale(movement, movementSpeed * deltaTime);
        camera->position = Vector3Add(camera->position, movement);
    }

    // Update camera target and up
    camera->target = Vector3Add(camera->position, forward);
    camera->up = up;

    return movementSpeed;  // Return speed for UI display
}

// Draw full scene projecting shadows
static void DrawScene(Mesh planeMesh, Mesh sphereMesh, Material material) {
    DrawMesh(planeMesh, material, MatrixIdentity());
    DrawMesh(sphereMesh, material, MatrixTranslate(0.0f, 10.0f, 40.0f));
}

int main(void) {
    // ===== Raylib Init =====
    const int screenWidth = 1280;
    const int screenHeight = 720;
    InitWindow(screenWidth, screenHeight, "Planet Renderer - Shadows");
    SetTargetFPS(60);
    DisableCursor();
    ToggleBorderlessWindowed();

    // ===== Set up Player Camera ====
    Camera3D camera = {0};
    camera.position = (Vector3){0.0f, 30.0f, 100.0f};
    camera.target = (Vector3){0.0f, 0.0f, 0.0f};
    camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;


    // ===== Create Meshes ====
    const float scale = 1700000.0f;
    const Mesh plane_mesh = create_plane_mesh_with_noise(scale, 200);
    Mesh sphere_mesh = GenMeshSphere(5.0f, 16, 16);

    // ===== Shadow maps ===== ffffffff
    // Set up the light direction
    float lightYaw = 120.0f;
    float lightPitch = -25.0f;
    Vector3 lightDir = {
        cosf(lightPitch * DEG2RAD) * cosf(lightYaw * DEG2RAD),
        sinf(lightPitch * DEG2RAD),
        cosf(lightPitch * DEG2RAD) * sinf(lightYaw * DEG2RAD)
    };
    lightDir = Vector3Normalize(lightDir);
    Color lightColor = WHITE;
    Vector4 lightColorNormalized = ColorNormalize(lightColor);

    // Define cascade split distances
    // 1 unit = 1 meter
    float cascadeSplits[CASCADE_COUNT + 1];
    cascadeSplits[0] = 0.1f;       // 10cm - Near plane
    cascadeSplits[1] = 500.0f;     // 500m - End of cascade 0 (surface detail)
    cascadeSplits[2] = 5000.0f;    // 5km - End of cascade 1 (regional terrain)
    cascadeSplits[3] = 50000.0f;   // 50km - End of cascade 2 (horizon/mountains)
    cascadeSplits[4] = 500000.0f;  // 500km - Far plane (cascade 3 - full planet from orbit)

    // Set up the light camera
    const float lightViewDistance = 128.0f;
    Camera3D lightCameras[CASCADE_COUNT];
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
        lightCameras[i].position = Vector3Scale(lightDir, -200.0f);
        lightCameras[i].target = Vector3Zero();
        lightCameras[i].projection = CAMERA_ORTHOGRAPHIC;
        lightCameras[i].up = (Vector3){0.0f, 1.0f, 0.0f};
        lightCameras[i].fovy = lightViewDistance; // Will adjust per-cascade later
    }

    // Load shadow shader
    const Shader shadowShader = LoadShader("build/shaders/shadowmap.vs", "build/shaders/shadowmap.fs");
    if (shadowShader.id == 0) {
        printf("ERROR: Failed to load shadow shader!\n");
        CloseWindow();
        return -1;
    }

    // Set up shader uniforms
    shadowShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shadowShader, "viewPos");

    int lightDirLoc = GetShaderLocation(shadowShader, "lightDir");
    int lightColLoc = GetShaderLocation(shadowShader, "lightColor");
    int ambientLoc = GetShaderLocation(shadowShader, "ambient");
    int lightVPsLoc = GetShaderLocation(shadowShader, "lightVPs");
    int shadowMapsLoc = GetShaderLocation(shadowShader, "shadowMaps");
    int cascadeSplitsLoc = GetShaderLocation(shadowShader, "cascadeSplits");

    SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(shadowShader, lightColLoc, &lightColorNormalized, SHADER_UNIFORM_VEC4);

    // float ambient[4] = {0.1f, 0.1f, 0.1f, 1.0f};
    float ambient[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    SetShaderValue(shadowShader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

    // Set cascade splits individually to ensure correct array handling
    for (int i = 0; i < CASCADE_COUNT + 1; i++) {
        char locName[32];
        sprintf(locName, "cascadeSplits[%d]", i);
        int loc = GetShaderLocation(shadowShader, locName);
        SetShaderValue(shadowShader, loc, &cascadeSplits[i], SHADER_UNIFORM_FLOAT);
    }

    int shadowMapResolution = SHADOWMAP_RESOLUTION;
    SetShaderValue(shadowShader, GetShaderLocation(shadowShader, "shadowMapResolution"), &shadowMapResolution,
                   SHADER_UNIFORM_INT);

    // Store light matrices
    Matrix lightViews[CASCADE_COUNT] = {0};
    Matrix lightProjs[CASCADE_COUNT] = {0};
    Matrix lightViewProjs[CASCADE_COUNT] = {0};
    int textureActiveSlot = 10;

    // Create material
    Material shadowMaterial = LoadMaterialDefault();
    shadowMaterial.shader = shadowShader;

    // Load shadow map render textures
    RenderTexture2D shadowMaps[CASCADE_COUNT];
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
        shadowMaps[i] = LoadShadowmapRenderTexture(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);
        if (shadowMaps[i].id == 0) {
            printf("ERROR: Failed to load shadow map render texture!\n");
            CloseWindow();
            return -1;
        }
    }

    // ===== Misc Options =====
    bool wireframe = false;

    // ===== Main Render Loop =====
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F)) {
            wireframe = !wireframe;
        }

        float deltaTime = GetFrameTime();

        // Update camera position for shader
        Vector3 cameraPos = camera.position;
        SetShaderValue(shadowShader, shadowShader.locs[SHADER_LOC_VECTOR_VIEW], &cameraPos, SHADER_UNIFORM_VEC3);
        UpdateCameraMovement(&camera, deltaTime);

        // Move light with arrow keys
        const float lightRotateSpeed = 60.0f;
        if (IsKeyDown(KEY_LEFT)) lightYaw += lightRotateSpeed * deltaTime;
        if (IsKeyDown(KEY_RIGHT)) lightYaw -= lightRotateSpeed * deltaTime;
        if (IsKeyDown(KEY_UP)) lightPitch += lightRotateSpeed * deltaTime;
        if (IsKeyDown(KEY_DOWN)) lightPitch -= lightRotateSpeed * deltaTime;

        // Clamp pitch to avoid flipping
        if (lightPitch > 89.0f) lightPitch = 89.0f;
        if (lightPitch < -89.0f) lightPitch = -89.0f;

        // Recalculate light direction
        lightDir = (Vector3){
            cosf(lightPitch * DEG2RAD) * cosf(lightYaw * DEG2RAD),
            sinf(lightPitch * DEG2RAD),
            cosf(lightPitch * DEG2RAD) * sinf(lightYaw * DEG2RAD)
        };
        lightDir = Vector3Normalize(lightDir);

        // Make light camera shadow map coverage shift based on view direction
        Vector3 viewDir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        float alignment = Vector3DotProduct(viewDir, lightDir);  // -1 to 1

        // Shift shadow map coverage forward when looking away from light
        // Small multiplier (0.2) to keep most coverage near player
        float lookAheadDistance = (1.0f - alignment) * 0.2f * lightViewDistance;

        Vector3 lookAheadTarget = Vector3Add(camera.position, Vector3Scale(viewDir, lookAheadDistance));
        // Update light cameras for each cascade
        for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
            // Calculate coverage area for this cascade based on split distances
            float cascadeDistance = cascadeSplits[i + 1]; // End distance of this cascade

            // For orthographic projection, fovy determines the height of the view volume
            // We need to ensure it covers the view frustum slice at this distance
            // A rough approximation is to use the cascade distance itself as the coverage size
            lightCameras[i].fovy = cascadeDistance * 2.0f; // Ensure plenty of coverage

            // Position the light camera far enough back to see potential casters (e.g. mountains)
            // The "back" distance needs to scale with the cascade size
            float backDistance = cascadeDistance * 2.0f;
            
            // For now, all cascades follow the camera (camerea-relative)
            lightCameras[i].target = lookAheadTarget;
            lightCameras[i].position = Vector3Add(lookAheadTarget, Vector3Scale(lightDir, -backDistance));
        }
        SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

        if (wireframe) {
            rlEnableWireMode();
        }

        // ===== PASS 1: Render shadow map from light's perspective =====
        for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
            // Set clip planes appropriate for this cascade's range
            float cascadeRange = cascadeSplits[i + 1] - cascadeSplits[i];
            float nearPlane = 0.1f;
            float farPlane = cascadeRange * 4.0f;  // 4x range to ensure full coverage
            rlSetClipPlanes(nearPlane, farPlane);

            BeginTextureMode(shadowMaps[i]);
            ClearBackground(WHITE);

            BeginMode3D(lightCameras[i]);
            lightViews[i] = rlGetMatrixModelview();
            lightProjs[i] = rlGetMatrixProjection();
            DrawScene(plane_mesh, sphere_mesh, shadowMaterial);
            EndMode3D();
            EndTextureMode();

            lightViewProjs[i] = MatrixMultiply(lightViews[i], lightProjs[i]);
        }


        // ===== PASS 2: Render main scene with shadows =====
        // Set clip planes for Earth-scale rendering
        // Near: 1 km, Far: 100,000 km (to see the whole planet from space)
        rlSetClipPlanes(.1, 100000000.0);
        BeginDrawing();
        ClearBackground(BLACK);

        // Set all cascade matrices
        // Set all cascade matrices
        for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
            char locName[32];
            sprintf(locName, "lightVPs[%d]", i);
            int loc = GetShaderLocation(shadowShader, locName);
            SetShaderValueMatrix(shadowShader, loc, lightViewProjs[i]);
        }
        rlEnableShader(shadowShader.id);

        // Bind all cascade shadow maps to texture slots
        int textureSlots[CASCADE_COUNT] = {10, 11, 12, 13};
        for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
            int textureSlot = 10 + i;  // Use slots 10, 11, 12, 13
            rlActiveTextureSlot(textureSlot);
            rlEnableTexture(shadowMaps[i].depth.id);

            // Set uniform for each cascade's sampler
            // Explicitly query location for each array element
            char locName[32];
            sprintf(locName, "shadowMaps[%d]", i);
            int loc = GetShaderLocation(shadowShader, locName);
            rlSetUniform(loc, &textureSlot, SHADER_UNIFORM_INT, 1);
        }

        BeginMode3D(camera);
        DrawScene(plane_mesh, sphere_mesh, shadowMaterial);
        EndMode3D();

        DrawText("WASD to move, Mouse to look", 10, 10, 20, RAYWHITE);
        DrawText("Arrow keys to rotate light", 10, 30, 20, RAYWHITE);

        EndDrawing();

        if (wireframe) {
            rlDisableWireMode();
        }
    }

    UnloadShader(shadowShader);
    UnloadMesh(plane_mesh);
    UnloadMesh(sphere_mesh);

    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
        UnloadRenderTexture(shadowMaps[i]);
    }

    CloseWindow();

    return 0;
}
