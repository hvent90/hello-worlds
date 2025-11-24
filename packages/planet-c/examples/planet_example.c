#include <stdio.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "../src/mesh.h"

#define SHADOWMAP_RESOLUTION 1024

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

// Draw full scene projecting shadows
static void DrawScene(Mesh planeMesh, Mesh sphereMesh, Material material) {
    DrawMesh(planeMesh, material, MatrixIdentity());
    DrawMesh(sphereMesh, material, MatrixTranslate(0.0f, 10.0f, 40.0f));
}

int main(void) {
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "Planet Renderer - Shadows");
    DisableCursor();

    Camera3D camera = {0};
    camera.position = (Vector3){0.0f, 30.0f, 100.0f};
    camera.target = (Vector3){0.0f, 0.0f, 0.0f};
    camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Set up light direction and camera
    Vector3 lightDir = Vector3Normalize((Vector3){3.95f, -1.0f, 1.35f});
    Color lightColor = WHITE;
    Vector4 lightColorNormalized = ColorNormalize(lightColor);

    Camera3D lightCamera = {0};
    lightCamera.position = Vector3Scale(lightDir, -200.0f);
    lightCamera.target = Vector3Zero();
    lightCamera.projection = CAMERA_ORTHOGRAPHIC;
    lightCamera.up = (Vector3){0.0f, 1.0f, 0.0f};
    lightCamera.fovy = 200.0f;

    SetTargetFPS(60);

    const float scale = 100.0f;

    // Create meshes
    const Mesh plane_mesh = create_plane_mesh_with_noise(scale, 100);
    Mesh sphere_mesh = GenMeshSphere(5.0f, 16, 16);

    // Load shadow map render texture
    const RenderTexture2D shadowMap = LoadShadowmapRenderTexture(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);
    if (shadowMap.id == 0) {
        printf("ERROR: Failed to load shadow map render texture!\n");
        CloseWindow();
        return -1;
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
    int lightVPLoc = GetShaderLocation(shadowShader, "lightVP");
    int shadowMapLoc = GetShaderLocation(shadowShader, "shadowMap");

    SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(shadowShader, lightColLoc, &lightColorNormalized, SHADER_UNIFORM_VEC4);

    float ambient[4] = {0.1f, 0.1f, 0.1f, 1.0f};
    SetShaderValue(shadowShader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

    int shadowMapResolution = SHADOWMAP_RESOLUTION;
    SetShaderValue(shadowShader, GetShaderLocation(shadowShader, "shadowMapResolution"), &shadowMapResolution,
                   SHADER_UNIFORM_INT);

    // Create material
    Material shadowMaterial = LoadMaterialDefault();
    shadowMaterial.shader = shadowShader;

    // Store light matrices
    Matrix lightView = {0};
    Matrix lightProj = {0};
    Matrix lightViewProj = {0};
    int textureActiveSlot = 10;

    bool wireframe = false;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F)) {
            wireframe = !wireframe;
        }

        float deltaTime = GetFrameTime();

        // Update camera position for shader
        Vector3 cameraPos = camera.position;
        SetShaderValue(shadowShader, shadowShader.locs[SHADER_LOC_VECTOR_VIEW], &cameraPos, SHADER_UNIFORM_VEC3);
        UpdateCamera(&camera, CAMERA_FIRST_PERSON);

        // Move light with arrow keys
        const float cameraSpeed = 0.05f;
        if (IsKeyDown(KEY_LEFT)) {
            if (lightDir.x < 0.6f) lightDir.x += cameraSpeed * 60.0f * deltaTime;
        }
        if (IsKeyDown(KEY_RIGHT)) {
            if (lightDir.x > -0.6f) lightDir.x -= cameraSpeed * 60.0f * deltaTime;
        }
        if (IsKeyDown(KEY_UP)) {
            if (lightDir.z < 0.6f) lightDir.z += cameraSpeed * 60.0f * deltaTime;
        }
        if (IsKeyDown(KEY_DOWN)) {
            if (lightDir.z > -0.6f) lightDir.z -= cameraSpeed * 60.0f * deltaTime;
        }

        lightDir = Vector3Normalize(lightDir);
        lightCamera.position = Vector3Scale(lightDir, -200.0f);
        SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

        if (wireframe) {
            rlEnableWireMode();
        }

        // ===== PASS 1: Render shadow map from light's perspective =====
        BeginTextureMode(shadowMap);
        ClearBackground(WHITE);

        BeginMode3D(lightCamera);
        lightView = rlGetMatrixModelview();
        lightProj = rlGetMatrixProjection();
        DrawScene(plane_mesh, sphere_mesh, shadowMaterial);
        EndMode3D();

        EndTextureMode();

        lightViewProj = MatrixMultiply(lightView, lightProj);

        // ===== PASS 2: Render main scene with shadows =====
        BeginDrawing();
        ClearBackground(BLACK);

        SetShaderValueMatrix(shadowShader, lightVPLoc, lightViewProj);
        rlEnableShader(shadowShader.id);

        rlActiveTextureSlot(textureActiveSlot);
        rlEnableTexture(shadowMap.depth.id);
        rlSetUniform(shadowMapLoc, &textureActiveSlot, SHADER_UNIFORM_INT, 1);

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
    UnloadRenderTexture(shadowMap);

    CloseWindow();

    return 0;
}
