#include "../src/camera.h"
#include "../src/mesh.h"
#include "../src/shadow.h"
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdio.h>

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
  Vector3 lightDir = {cosf(lightPitch * DEG2RAD) * cosf(lightYaw * DEG2RAD),
                      sinf(lightPitch * DEG2RAD),
                      cosf(lightPitch * DEG2RAD) * sinf(lightYaw * DEG2RAD)};
  lightDir = Vector3Normalize(lightDir);
  Color lightColor = WHITE;
  Vector4 lightColorNormalized = ColorNormalize(lightColor);

  // Define cascade split distances
  // 1 unit = 1 meter
  float cascadeSplits[CASCADE_COUNT + 1];
  cascadeSplits[0] = 0.1f;     // 10cm - Near plane
  cascadeSplits[1] = 500.0f;   // 500m - End of cascade 0 (surface detail)
  cascadeSplits[2] = 5000.0f;  // 5km - End of cascade 1 (regional terrain)
  cascadeSplits[3] = 50000.0f; // 50km - End of cascade 2 (horizon/mountains)
  cascadeSplits[4] =
      500000.0f; // 500km - Far plane (cascade 3 - full planet from orbit)

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
  const Shader shadowShader =
      LoadShader("build/shaders/shadowmap.vs", "build/shaders/shadowmap.fs");
  if (shadowShader.id == 0) {
    printf("ERROR: Failed to load shadow shader!\n");
    CloseWindow();
    return -1;
  }

  // Set up shader uniforms
  shadowShader.locs[SHADER_LOC_VECTOR_VIEW] =
      GetShaderLocation(shadowShader, "viewPos");

  int lightDirLoc = GetShaderLocation(shadowShader, "lightDir");
  int lightColLoc = GetShaderLocation(shadowShader, "lightColor");
  int ambientLoc = GetShaderLocation(shadowShader, "ambient");
  int lightVPsLoc = GetShaderLocation(shadowShader, "lightVPs");
  int shadowMapsLoc = GetShaderLocation(shadowShader, "shadowMaps");
  int cascadeSplitsLoc = GetShaderLocation(shadowShader, "cascadeSplits");

  SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
  SetShaderValue(shadowShader, lightColLoc, &lightColorNormalized,
                 SHADER_UNIFORM_VEC4);

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
  SetShaderValue(shadowShader,
                 GetShaderLocation(shadowShader, "shadowMapResolution"),
                 &shadowMapResolution, SHADER_UNIFORM_INT);

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
    shadowMaps[i] =
        LoadShadowmapRenderTexture(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);
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
    SetShaderValue(shadowShader, shadowShader.locs[SHADER_LOC_VECTOR_VIEW],
                   &cameraPos, SHADER_UNIFORM_VEC3);
    UpdateCameraMovement(&camera, deltaTime);

    // Move light with arrow keys
    const float lightRotateSpeed = 60.0f;
    if (IsKeyDown(KEY_LEFT))
      lightYaw += lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_RIGHT))
      lightYaw -= lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_UP))
      lightPitch += lightRotateSpeed * deltaTime;
    if (IsKeyDown(KEY_DOWN))
      lightPitch -= lightRotateSpeed * deltaTime;

    // Clamp pitch to avoid flipping
    if (lightPitch > 89.0f)
      lightPitch = 89.0f;
    if (lightPitch < -89.0f)
      lightPitch = -89.0f;

    // Recalculate light direction
    lightDir = (Vector3){cosf(lightPitch * DEG2RAD) * cosf(lightYaw * DEG2RAD),
                         sinf(lightPitch * DEG2RAD),
                         cosf(lightPitch * DEG2RAD) * sinf(lightYaw * DEG2RAD)};
    lightDir = Vector3Normalize(lightDir);

    // Make light camera shadow map coverage shift based on view direction
    Vector3 viewDir =
        Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    float alignment = Vector3DotProduct(viewDir, lightDir); // -1 to 1

    // Shift shadow map coverage forward when looking away from light
    // Small multiplier (0.2) to keep most coverage near player
    float lookAheadDistance = (1.0f - alignment) * 0.2f * lightViewDistance;

    Vector3 lookAheadTarget =
        Vector3Add(camera.position, Vector3Scale(viewDir, lookAheadDistance));
    // Update light cameras for each cascade
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      // Calculate coverage area for this cascade based on split distances
      float cascadeDistance =
          cascadeSplits[i + 1]; // End distance of this cascade

      // For orthographic projection, fovy determines the height of the view
      // volume We need to ensure it covers the view frustum slice at this
      // distance A rough approximation is to use the cascade distance itself as
      // the coverage size
      lightCameras[i].fovy =
          cascadeDistance * 2.0f; // Ensure plenty of coverage

      // Position the light camera far enough back to see potential casters
      // (e.g. mountains) The "back" distance needs to scale with the cascade
      // size
      float backDistance = cascadeDistance * 2.0f;

      // For now, all cascades follow the camera (camerea-relative)
      lightCameras[i].target = lookAheadTarget;
      lightCameras[i].position =
          Vector3Add(lookAheadTarget, Vector3Scale(lightDir, -backDistance));
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
      float farPlane = cascadeRange * 4.0f; // 4x range to ensure full coverage
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
      int textureSlot = 10 + i; // Use slots 10, 11, 12, 13
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
