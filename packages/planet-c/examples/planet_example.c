#include <stdio.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "../src/mesh.h"


int main(void) {
  const int screenWidth = 1280;
  const int screenHeight = 720;

  InitWindow(screenWidth, screenHeight, "Planet Renderer");
  DisableCursor();

  rlSetClipPlanes(0.1, 10000000.0);

  Camera3D camera = { 0 };
  camera.position = (Vector3){ 0.0f, 10.0f, 100.0f };
  camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
  camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
  camera.fovy = 45.0f;
  camera.projection = CAMERA_PERSPECTIVE;

  // Set up a camera from the light's perspective
  Vector3 lightDir = (Vector3){ -0.5f, 1.0f, -0.5f };
  lightDir = Vector3Normalize(lightDir);
  Camera3D lightCamera = { 0 };
  lightCamera.position = Vector3Scale(lightDir, -200.0f);  // Position behind light
  lightCamera.target = (Vector3){0.0f, 0.0f, 0.0f};        // Looking at center
  lightCamera.up = (Vector3){0.0f, 1.0f, 0.0f};
  lightCamera.fovy = 45.0f;
  lightCamera.projection = CAMERA_ORTHOGRAPHIC;  // Orthographic for directional light

  SetTargetFPS(60);

  const float scale = 100.0f;

  const Mesh plane_mesh = create_plane_mesh_with_noise(scale, 100);
  const RenderTexture2D shadowMap = LoadRenderTexture(2048, 2048);
  const Shader shadowShader = LoadShader("build/shaders/shadow.vs", "build/shaders/shadow.fs");
  if (shadowShader.id == 0) {
    printf("ERROR: Failed to load shadow shader!\n");
  }
  const Shader depthShader = LoadShader("build/shaders/shadow_depth.vs", "build/shaders/shadow_depth.fs");
  if (depthShader.id == 0) {
    printf("ERROR: Failed to load depth shader!\n");
  }

  Material shadowMaterial = LoadMaterialDefault();
  shadowMaterial.shader = shadowShader;

  Material depthMaterial = LoadMaterialDefault();
  depthMaterial.shader = depthShader;

  // Compute light matrix for shadow calculations
  Matrix lightView = MatrixLookAt(lightCamera.position, lightCamera.target, lightCamera.up);
  Matrix lightProjection = MatrixOrtho(-200.0f, 200.0f, -200.0f, 200.0f, 0.1f, 1000.0f);
  Matrix lightMatrix = MatrixMultiply(lightView, lightProjection);

  int lightMatrixLoc = GetShaderLocation(shadowMaterial.shader, "lightMatrix");
  SetShaderValueMatrix(shadowMaterial.shader, lightMatrixLoc, lightMatrix);

  int shadowMapLoc = GetShaderLocation(shadowMaterial.shader, "shadowMap");
  int shadowMapSlot = 1;
  SetShaderValue(shadowMaterial.shader, shadowMapLoc, &shadowMapSlot, SHADER_UNIFORM_INT);

  bool wireframe = false;

  while (!WindowShouldClose()) {
    if (IsKeyPressed(KEY_F)) {
      wireframe = !wireframe;
    }

    if (wireframe) {
      rlEnableWireMode();
    }

    UpdateCamera(&camera, CAMERA_FIRST_PERSON);

    BeginDrawing();
    ClearBackground(BLACK);

    // ===== STEP 1: RENDER SHADOW MAP (light's perspective) =====
    BeginTextureMode(shadowMap);
      rlClearScreenBuffers();
      rlViewport(0, 0, 2048, 2048);
      BeginMode3D(lightCamera);
        DrawMesh(plane_mesh, depthMaterial, MatrixIdentity());
      EndMode3D();
    EndTextureMode();
    rlViewport(0, 0, screenWidth, screenHeight);

    // Bind shadow map to texture slot 1
    rlActiveTextureSlot(1);
    rlEnableTexture(shadowMap.depth.id);
    rlActiveTextureSlot(0);

    // ===== STEP 2: RENDER MAIN SCENE (camera's perspective) =====
    BeginMode3D(camera);
      DrawMesh(plane_mesh, shadowMaterial, MatrixIdentity());
    EndMode3D();

    // ===== STEP 3: DRAW UI TEXT =====
    DrawText("WASD to move, Mouse to look", 10, 10, 20, RAYWHITE);
    EndDrawing();

    if (wireframe) {
      rlDisableWireMode();
    }
  }

  CloseWindow();

  return 0;
}
