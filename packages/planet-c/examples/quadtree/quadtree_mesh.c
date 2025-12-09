#include <float.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
// clang-format off
#include "shadow.h"
#include "camera.h"
#include "quadtree.h"
#include "mesh.h"
// clang-format on

const float MAX_SCALE = 1700000.0f;
#define MAX_DEPTH 8

typedef struct {
  Mesh mesh;
  Vector3 position; // World-space position of this mesh patch
  float scale;      // Size of this mesh patch
} QuadTreeMeshData;

void cleanup_mesh_data(void *user_data) {
  if (user_data == NULL) {
    return;
  }
  QuadTreeMeshData *mesh_data = (QuadTreeMeshData *)user_data;
  UnloadMesh(mesh_data->mesh);
  free(mesh_data);
}

// Compute stable up vector for a given direction
// When direction is nearly vertical, use world forward as reference to avoid
// singularity
Vector3 compute_stable_up(Vector3 direction) {
  const float threshold = 0.99f;
  Vector3 world_up = {0.0f, 1.0f, 0.0f};
  Vector3 world_forward = {0.0f, 0.0f, 1.0f};

  // Check if direction is nearly parallel to world up
  float dot = fabsf(Vector3DotProduct(direction, world_up));
  Vector3 reference = (dot > threshold) ? world_forward : world_up;

  // Right = direction x reference (perpendicular to both)
  Vector3 right = Vector3Normalize(Vector3CrossProduct(direction, reference));

  // Up = right x direction (perpendicular to direction)
  return Vector3CrossProduct(right, direction);
}

// ===== Frustum Fitting for CSM =====

typedef struct {
  float minX, maxX;
  float minY, maxY;
  float minZ, maxZ;
} FrustumBounds;

// Compute 8 corners of view frustum between nearDist and farDist
void get_frustum_corners(Camera3D cam, float nearDist, float farDist,
                         float aspectRatio, Vector3 corners[8]) {
  Vector3 forward =
      Vector3Normalize(Vector3Subtract(cam.target, cam.position));
  Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
  Vector3 up = Vector3CrossProduct(right, forward);

  float fovRad = cam.fovy * DEG2RAD;
  float tanHalfFov = tanf(fovRad * 0.5f);

  // Near plane dimensions
  float nearHeight = nearDist * tanHalfFov * 2.0f;
  float nearWidth = nearHeight * aspectRatio;

  // Far plane dimensions
  float farHeight = farDist * tanHalfFov * 2.0f;
  float farWidth = farHeight * aspectRatio;

  Vector3 nearCenter = Vector3Add(cam.position, Vector3Scale(forward, nearDist));
  Vector3 farCenter = Vector3Add(cam.position, Vector3Scale(forward, farDist));

  // Near plane corners (0-3)
  corners[0] = Vector3Add(
      nearCenter, Vector3Add(Vector3Scale(up, nearHeight * 0.5f),
                             Vector3Scale(right, -nearWidth * 0.5f)));
  corners[1] = Vector3Add(
      nearCenter, Vector3Add(Vector3Scale(up, nearHeight * 0.5f),
                             Vector3Scale(right, nearWidth * 0.5f)));
  corners[2] = Vector3Add(
      nearCenter, Vector3Add(Vector3Scale(up, -nearHeight * 0.5f),
                             Vector3Scale(right, -nearWidth * 0.5f)));
  corners[3] = Vector3Add(
      nearCenter, Vector3Add(Vector3Scale(up, -nearHeight * 0.5f),
                             Vector3Scale(right, nearWidth * 0.5f)));

  // Far plane corners (4-7)
  corners[4] =
      Vector3Add(farCenter, Vector3Add(Vector3Scale(up, farHeight * 0.5f),
                                       Vector3Scale(right, -farWidth * 0.5f)));
  corners[5] =
      Vector3Add(farCenter, Vector3Add(Vector3Scale(up, farHeight * 0.5f),
                                       Vector3Scale(right, farWidth * 0.5f)));
  corners[6] =
      Vector3Add(farCenter, Vector3Add(Vector3Scale(up, -farHeight * 0.5f),
                                       Vector3Scale(right, -farWidth * 0.5f)));
  corners[7] =
      Vector3Add(farCenter, Vector3Add(Vector3Scale(up, -farHeight * 0.5f),
                                       Vector3Scale(right, farWidth * 0.5f)));
}

// Transform frustum corners to light space and compute axis-aligned bounding
// box
FrustumBounds compute_light_frustum_bounds(Vector3 corners[8], Vector3 lightDir,
                                           Vector3 lightUp) {
  Vector3 lightRight = Vector3CrossProduct(lightDir, lightUp);

  FrustumBounds bounds = {.minX = FLT_MAX,
                          .maxX = -FLT_MAX,
                          .minY = FLT_MAX,
                          .maxY = -FLT_MAX,
                          .minZ = FLT_MAX,
                          .maxZ = -FLT_MAX};

  for (int i = 0; i < 8; i++) {
    // Project corner onto light-space axes
    float x = Vector3DotProduct(corners[i], lightRight);
    float y = Vector3DotProduct(corners[i], lightUp);
    float z = Vector3DotProduct(corners[i], lightDir);

    bounds.minX = fminf(bounds.minX, x);
    bounds.maxX = fmaxf(bounds.maxX, x);
    bounds.minY = fminf(bounds.minY, y);
    bounds.maxY = fmaxf(bounds.maxY, y);
    bounds.minZ = fminf(bounds.minZ, z);
    bounds.maxZ = fmaxf(bounds.maxZ, z);
  }

  return bounds;
}

// Helper to recreate mesh data for a node that becomes a leaf after merge
void *recreate_parent_mesh_data(QuadTreeNode *node) {
  if (node == NULL || node->user_data != NULL) {
    return node->user_data; // Already has data or invalid node
  }

  // Calculate position by traversing up to root and summing offsets
  Vector3 position = {0.0f, 0.0f, 0.0f};
  float scale = MAX_SCALE; // Start with root scale

  QuadTreeNode *current = node;
  QuadTreeNode *path[MAX_DEPTH]; // Max depth we support
  int depth = 0;

  // Build path from node to root
  while (current->parent != NULL && depth < MAX_DEPTH) {
    path[depth++] = current;
    current = current->parent;
  }

  // Traverse from root down, calculating position and scale
  for (int i = depth - 1; i >= 0; i--) {
    QuadTreeNode *parent_node = path[i]->parent;
    QuadTreeNode *child_node = path[i];

    // Find child index
    short index = -1;
    for (unsigned short j = 0; j < 4; j++) {
      if (parent_node->children[j] == child_node) {
        index = j;
        break;
      }
    }

    if (index != -1) {
      scale *= 0.5f;
      const float offset = scale * 0.5f;
      const float x_offset = (index % 2 == 0) ? -offset : offset;
      const float z_offset = (index < 2) ? -offset : offset;
      position = Vector3Add(position, (Vector3){x_offset, 0, z_offset});
    }
  }

  // Create mesh for this node
  Mesh mesh = create_plane_mesh_with_noise_ws((Vector3){node->x, 0, node->y},
                                              scale, 50);

  QuadTreeMeshData *data = malloc(sizeof(QuadTreeMeshData));
  if (data == NULL) {
    UnloadMesh(mesh);
    return NULL;
  }

  data->mesh = mesh;
  data->position = position;
  data->scale = scale;

  return data;
}

void *create_child_mesh_data(QuadTreeNode *parent, QuadTreeNode *child,
                             void *parent_user_data) {
  if (parent_user_data == NULL) {
    return NULL;
  }

  QuadTreeMeshData *parent_data = (QuadTreeMeshData *)parent_user_data;

  // 1. Determine Child Index
  short index = -1;
  for (unsigned short i = 0; i < 4; i++) {
    if (parent->children[i] == child) {
      index = i;
      break;
    }
  }

  if (index == -1) {
    // Child not found in parent's children array
    return NULL;
  }

  // 2. Calculate Child Position Offset
  const float child_scale = parent_data->scale * 0.5f;
  const float offset = child_scale * 0.5f;
  const float x_offset = (index % 2 == 0) ? -offset : offset;
  const float z_offset = (index < 2) ? -offset : offset;
  const float y_offset = 0;

  Vector3 position = Vector3Add(parent_data->position,
                                (Vector3){x_offset, y_offset, z_offset});

  // 3. Generate Child Mesh
  Mesh mesh = create_plane_mesh_with_noise_ws((Vector3){child->x, 0, child->y},
                                              child_scale, 50);

  // 4. Allocate and Populate return structure
  QuadTreeMeshData *child_data = malloc(sizeof(QuadTreeMeshData));
  if (child_data == NULL) {
    UnloadMesh(mesh);
    return NULL;
  }

  child_data->mesh = mesh;
  child_data->position = position;
  child_data->scale = child_scale;

  return child_data;
}

// Draw all quadtree meshes recursively (only draws leaf nodes)
void draw_quadtree_meshes(QuadTreeNode *node, Material material) {
  if (node == NULL) {
    return;
  }

  // Check if this is a leaf node (no children)
  bool is_leaf = true;
  for (unsigned short i = 0; i < 4; i++) {
    if (node->children[i] != NULL) {
      is_leaf = false;
      break;
    }
  }

  // If leaf node, draw its mesh
  if (is_leaf && node->user_data != NULL) {
    QuadTreeMeshData *mesh_data = (QuadTreeMeshData *)node->user_data;

    Matrix transform = MatrixTranslate(node->x, 0, node->y);
    DrawMesh(mesh_data->mesh, material, transform);

    // Debug bounding boxs
    // Vector3 bounds = (Vector3){mesh_data->scale, 0.0f, mesh_data->scale};
    // Vector3 top_left =
    //    Vector3Subtract(mesh_data->position,
    //                    Vector3Multiply(bounds, (Vector3){0.5f, 0.5f, 0.5f}));
    // BoundingBox bounding_box =
    //
    //                    (BoundingBox){top_left, Vector3Add(top_left, bounds)};
    // DrawBoundingBox(bounding_box, GREEN);
  }

  // If not leaf, recursively draw children
  if (!is_leaf) {
    for (unsigned short i = 0; i < 4; i++) {
      draw_quadtree_meshes(node->children[i], material);
    }
  }
}

int main(void) {
  // ===== Raylib Init =====
  const int screenWidth = 1280;
  const int screenHeight = 720;
  InitWindow(screenWidth, screenHeight, "Planet Renderer - Shadows");
  SetTargetFPS(60);
  ToggleBorderlessWindowed();

  // ===== Misc =====
  int enable_wireframe = false;

  // ===== Initialize Quadtree =====
  QuadTreeMeshData *mesh_data = malloc(sizeof(QuadTreeMeshData));
  if (mesh_data == NULL) {
    return 0;
  }
  mesh_data->mesh = create_plane_mesh_with_noise_ws((Vector3){0.0f, 0.0f, 0.0f},
                                                    MAX_SCALE, 50);
  mesh_data->position = (Vector3){0.0f, 0.0f, 0.0f};
  mesh_data->scale = MAX_SCALE;
  QuadTreeNode *root_node = create_quadtree_node(
      -MAX_SCALE * 0.5f, -MAX_SCALE * 0.5f, MAX_SCALE, MAX_SCALE, 0, NULL);
  root_node->user_data = mesh_data;

  // ===== Set up 3D Camera =====
  Camera3D camera = {0};
  camera.position = (Vector3){0.0f, 100.0f, 100.0f};
  camera.target = (Vector3){0.0f, 0.0f, 0.0f};
  camera.up = (Vector3){0.0f, 1.0f, 0.0f};
  camera.fovy = 45.0f;
  camera.projection = CAMERA_PERSPECTIVE;
  // rlSetClipPlanes(0.1, 3400000);

  // ===== Create Material =====
  Material material = LoadMaterialDefault();

  // ===== Mode State =====
  typedef enum { MODE_MANUAL, MODE_AUTOMATIC } Mode;
  Mode current_mode = MODE_AUTOMATIC;

  // ===== Camera Rotation State =====
  float camera_yaw = 0.0f;   // Horizontal rotation
  float camera_pitch = 0.0f; // Vertical rotation
  DisableCursor();           // Hide and lock cursor for FPS-style control

  // ===== Shadow maps =====
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
      LoadShader("shaders/shadowmap.vs", "shaders/shadowmap.fs");
  if (shadowShader.id == 0) {
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

  // Debug cascade colors toggle
  int debugCascadesLoc = GetShaderLocation(shadowShader, "debugCascades");
  int debugCascades = 0;
  SetShaderValue(shadowShader, debugCascadesLoc, &debugCascades,
                 SHADER_UNIFORM_INT);

  // Store light matrices
  Matrix lightViews[CASCADE_COUNT] = {0};
  Matrix lightProjs[CASCADE_COUNT] = {0};
  Matrix lightViewProjs[CASCADE_COUNT] = {0};

  // Cache shader uniform locations (avoid per-frame GetShaderLocation calls)
  int lightVPLocs[CASCADE_COUNT];
  int shadowMapLocs[CASCADE_COUNT];
  for (int i = 0; i < CASCADE_COUNT; i++) {
    char locName[32];
    sprintf(locName, "lightVPs[%d]", i);
    lightVPLocs[i] = GetShaderLocation(shadowShader, locName);
    sprintf(locName, "shadowMaps[%d]", i);
    shadowMapLocs[i] = GetShaderLocation(shadowShader, locName);
  }

  // Create material
  Material shadowMaterial = LoadMaterialDefault();
  shadowMaterial.shader = shadowShader;

  // Load shadow map render textures
  RenderTexture2D shadowMaps[CASCADE_COUNT];
  for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
    shadowMaps[i] =
        LoadShadowmapRenderTexture(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);
    if (shadowMaps[i].id == 0) {
      CloseWindow();
      return -1;
    }
  }

  // Giant test ball for orbital shadow verification
  // Position it high above the planet surface so it casts a visible shadow
  const float testBallRadius = 50000.0f;      // 50km radius ball
  const float testBallAltitude = 200000.0f;   // 200km above surface
  const Vector3 testBallPosition = {0.0f, testBallAltitude, 0.0f};
  Mesh testBallMesh = GenMeshSphere(testBallRadius, 32, 32);
  UploadMesh(&testBallMesh, false);
  int showTestBall = 1;  // Toggle with 'B' key

  while (!WindowShouldClose()) {
    double t0 = GetTime();
    // ===== Wireframe =====
    if (IsKeyPressed(KEY_F)) {
      enable_wireframe = !enable_wireframe;
      if (enable_wireframe) {
        rlEnableWireMode();
      } else {
        rlDisableWireMode();
      }
    }

    // ===== Toggle cascade debug colors (C key) =====
    if (IsKeyPressed(KEY_C)) {
      debugCascades = !debugCascades;
      SetShaderValue(shadowShader, debugCascadesLoc, &debugCascades,
                     SHADER_UNIFORM_INT);
    }

    // ===== Toggle test ball visibility (B key) =====
    if (IsKeyPressed(KEY_B)) {
      showTestBall = !showTestBall;
    }

    float deltaTime = GetFrameTime();

    // Update camera position for shader
    Vector3 cameraPos = camera.position;
    SetShaderValue(shadowShader, shadowShader.locs[SHADER_LOC_VECTOR_VIEW],
                   &cameraPos, SHADER_UNIFORM_VEC3);
    UpdateCameraMovement(&camera, deltaTime);

    // ===== LOD Processing =====
    if (current_mode == MODE_AUTOMATIC) {
      Vector2 cam_pos_2d = (Vector2){camera.position.x, camera.position.z};
      process_leaf_nodes(root_node, cam_pos_2d, MAX_DEPTH,
                         create_child_mesh_data, cleanup_mesh_data);
      merge_distant_leaves(root_node, cam_pos_2d, cleanup_mesh_data,
                           recreate_parent_mesh_data);
    }

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

    // Compute stable up vector for light cameras (avoids singularity when light
    // is vertical)
    Vector3 lightUp = compute_stable_up(lightDir);
    Vector3 lightRight = Vector3CrossProduct(lightDir, lightUp);

    // Planet configuration for cascade 3
    const Vector3 planetCenter = {0.0f, 0.0f, 0.0f};
    const float planetRadius = MAX_SCALE * 0.5f;

    // Aspect ratio for frustum calculation
    float aspectRatio = (float)screenWidth / (float)screenHeight;

    // Store custom orthographic bounds for frustum-fitted cascades
    FrustumBounds cascadeBounds[CASCADE_COUNT];

    // Update light cameras for each cascade
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      if (i < 3) {
        // Cascades 0-2: Frustum-fitted for optimal shadow map usage
        float nearDist = cascadeSplits[i];
        float farDist = cascadeSplits[i + 1];

        // Compute view frustum corners for this cascade slice
        Vector3 corners[8];
        get_frustum_corners(camera, nearDist, farDist, aspectRatio, corners);

        // Transform to light space and get tight bounds
        FrustumBounds bounds =
            compute_light_frustum_bounds(corners, lightDir, lightUp);

        // Add padding (10%) to avoid edge clipping artifacts
        float padX = (bounds.maxX - bounds.minX) * 0.1f;
        float padY = (bounds.maxY - bounds.minY) * 0.1f;
        bounds.minX -= padX;
        bounds.maxX += padX;
        bounds.minY -= padY;
        bounds.maxY += padY;

        // Extend back along light direction to capture shadow casters
        float depth = bounds.maxZ - bounds.minZ;
        bounds.minZ -= depth * 2.0f; // Extend back to catch casters

        cascadeBounds[i] = bounds;

        // Compute center of bounds in world space
        float centerX = (bounds.minX + bounds.maxX) * 0.5f;
        float centerY = (bounds.minY + bounds.maxY) * 0.5f;
        float centerZ = (bounds.minZ + bounds.maxZ) * 0.5f;

        Vector3 boundsCenter =
            Vector3Add(Vector3Scale(lightRight, centerX),
                       Vector3Add(Vector3Scale(lightUp, centerY),
                                  Vector3Scale(lightDir, centerZ)));

        // Position light camera looking at the center of bounds
        lightCameras[i].target = boundsCenter;
        lightCameras[i].position =
            Vector3Subtract(boundsCenter, Vector3Scale(lightDir, depth * 2.0f));

        // Set fovy to bounds height (will be overridden with custom projection)
        lightCameras[i].fovy = bounds.maxY - bounds.minY;
      } else {
        // Cascade 3: Planet-relative (fixed to world origin for day/night
        // terminator)
        lightCameras[i].fovy = planetRadius * 2.2f;
        float backDistance = planetRadius * 2.5f;
        lightCameras[i].target = planetCenter;
        lightCameras[i].position =
            Vector3Add(planetCenter, Vector3Scale(lightDir, -backDistance));

        // Set dummy bounds for cascade 3 (not used for frustum fitting)
        cascadeBounds[i] = (FrustumBounds){0};
      }

      lightCameras[i].up = lightUp;
    }
    SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);

    double update_time = GetTime() - t0;

    // ===== PASS 1: Render shadow map from light's perspective =====
    const double shadow_map_start = GetTime();
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      BeginTextureMode(shadowMaps[i]);
      ClearBackground(WHITE);

      BeginMode3D(lightCameras[i]);
      lightViews[i] = rlGetMatrixModelview();

      if (i < 3) {
        // Cascades 0-2: Use custom tight-fit orthographic projection
        FrustumBounds b = cascadeBounds[i];
        float depth = b.maxZ - b.minZ;

        // Build orthographic projection from computed bounds
        // Note: bounds are in light space, so we use half-widths from center
        float halfWidth = (b.maxX - b.minX) * 0.5f;
        float halfHeight = (b.maxY - b.minY) * 0.5f;

        // Custom ortho projection: left, right, bottom, top, near, far
        Matrix customProj =
            MatrixOrtho(-halfWidth, halfWidth, -halfHeight, halfHeight,
                        0.1f,          // near
                        depth * 3.0f); // far (covers full depth + extra)

        rlSetMatrixProjection(customProj);
        lightProjs[i] = customProj;
      } else {
        // Cascade 3: Use raylib's automatic projection (planet-relative)
        // Light is positioned 2.5 radii behind planet, so nearest surface is ~1.5 radii away
        // Use tight near/far ratio for better depth precision (1:3 instead of 1:500000)
        float nearPlane = planetRadius * 1.0f;
        float farPlane = planetRadius * 4.0f;
        rlSetClipPlanes(nearPlane, farPlane);
        lightProjs[i] = rlGetMatrixProjection();
      }

      draw_quadtree_meshes(root_node, shadowMaterial);

      // Draw test ball in shadow pass (it needs to cast shadows)
      if (showTestBall) {
        Matrix ballTransform = MatrixTranslate(testBallPosition.x,
                                               testBallPosition.y,
                                               testBallPosition.z);
        DrawMesh(testBallMesh, shadowMaterial, ballTransform);
      }

      EndMode3D();
      EndTextureMode();

      lightViewProjs[i] = MatrixMultiply(lightViews[i], lightProjs[i]);
    }
    double shadow_map_time = GetTime() - shadow_map_start;

    // ===== PASS 2: Render main scene with shadows =====
    // Set clip planes for Earth-scale rendering
    // Near: 1 km, Far: 100,000 km (to see the whole planet from space)
    rlSetClipPlanes(.1, 100000000.0);

    BeginDrawing();
    ClearBackground(BLACK);

    // Set all cascade light view-projection matrices (using cached locations)
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      SetShaderValueMatrix(shadowShader, lightVPLocs[i], lightViewProjs[i]);
    }

    // Bind all cascade shadow maps to texture slots (using cached locations)
    for (unsigned short i = 0; i < CASCADE_COUNT; i++) {
      int textureSlot = 10 + i; // Use slots 10, 11, 12, 13
      rlActiveTextureSlot(textureSlot);
      rlEnableTexture(shadowMaps[i].depth.id);
      rlSetUniform(shadowMapLocs[i], &textureSlot, SHADER_UNIFORM_INT, 1);
    }

    double t_draw = GetTime();
    BeginMode3D(camera);

    draw_quadtree_meshes(root_node, shadowMaterial);

    // Draw test ball in main scene (receives shadows and is visible)
    if (showTestBall) {
      Matrix ballTransform = MatrixTranslate(testBallPosition.x,
                                             testBallPosition.y,
                                             testBallPosition.z);
      DrawMesh(testBallMesh, shadowMaterial, ballTransform);
    }

    EndMode3D();
    double draw_time = GetTime() - t_draw;

    // ===== Draw UI Text =====
    DrawText(current_mode == MODE_MANUAL ? "Mode: Manual (M)"
                                         : "Mode: Automatic (T)",
             10, 10, 20, WHITE);
    DrawText("WASD: Move | Space/Ctrl: Up/Down | Shift: Speed Boost", 10, 40,
             20, WHITE);
    DrawText("Mouse: Look Around | F: Wireframe", 10, 70, 20, WHITE);
    DrawText(TextFormat("C: Cascade Debug %s | B: Test Ball %s",
                        debugCascades ? "ON" : "OFF",
                        showTestBall ? "ON" : "OFF"),
             10, 100, 20, WHITE);
    DrawText(TextFormat("Camera Pos: (%.1f, %.1f, %.1f)", camera.position.x,
                        camera.position.y, camera.position.z),
             10, 130, 20, YELLOW);

    DrawText(TextFormat("Update Time: %.2f ms", update_time * 1000.0), 10, 160, 20, GREEN);
    DrawText(TextFormat("Shadow Pass: %.2f ms", shadow_map_time * 1000.0), 10, 190, 20, GREEN);
    DrawText(TextFormat("Draw 3D Time: %.2f ms", draw_time * 1000.0), 10, 220, 20, GREEN);
    DrawText(TextFormat("Frame Time: %.2f ms (%.0f FPS)", deltaTime * 1000.0, 1.0f / deltaTime), 10, 250, 20, GREEN);

    EndDrawing();
  }

  // Cleanup
  UnloadMesh(testBallMesh);
  delete_quadtree_node(root_node, cleanup_mesh_data);
  UnloadMaterial(material);
  CloseWindow();

  return 0;
}
